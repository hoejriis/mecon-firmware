#pragma once
// The screen stream (TELEMETRY.md): a copy of the display's last frame, taken once when the stream is due and then sent
// in parts, one per pass, so a capture never holds the RF loop. Reading the buffer does not wake or change the display.
// A frame that shows the pairing PIN is withheld, and says so: pixels cannot be redacted reliably.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <helpers/ui/DisplayDriver.h>
#include "IfaceJson.h"
#include "Provenance.h"

namespace Iface {

class ScreenFeed {
public:
  static const size_t FRAME_MAX = 1024, PART = 512;     // a part's base64 stays well under every link's event limit
  static const size_t DATA_CAP = 900, EVENT_CAP = 1300;   // a part's data, and its event with the signature
  static const uint32_t PART_MS = 200;

  ScreenFeed() = default;
  ScreenFeed(const ScreenFeed&) = delete;
  ScreenFeed& operator=(const ScreenFeed&) = delete;
  ~ScreenFeed() { free(_buf); }

  // A driver with no readable buffer, or one larger than a frame here, produces no stream.
  void attach(DisplayDriver* d) { _d = d && d->frameBytes() > 0 && d->frameBytes() <= FRAME_MAX ? d : nullptr; }
  bool available() const { return _d != nullptr; }

  // The stream is due: take the copy now. A frame still being sent is finished first; the next period brings the next one.
  void start(uint32_t now) {
    if (!_d || _pending) return;
    _frame++;
    _part = 1;
    _off = 0;
    _next = now;
    _pending = true;
    _withheld = nullptr;
    _bytes = _d->frameBytes();
    _on = _d->isOn();
    _age_s = _d->frameDrawn() ? (now - _d->frameDrawnMs()) / 1000 : 0;
    if (!_d->frameDrawn()) { _withheld = "no-frame"; return; }
    if (_d->secretDrawn()) { _withheld = "pairing-pin"; return; }
    _buf = static_cast<uint8_t*>(malloc(FRAME_MAX + b64Size()));
    if (!_buf || !_d->readFrame(_buf, FRAME_MAX)) { release(); _withheld = "no-memory"; }
  }

  bool due(uint32_t now) const { return _pending && (int32_t)(now - _next) >= 0; }

  // Nobody is listening any more: the frame goes, and its copy with it.
  void cancel() { release(); _pending = false; }

  // The next part's data as one JSON object; 0 when it could not be written, which ends the frame (a part that is not
  // sent is not retried: the manager discards the frame, and the next period is the retry).
  size_t next(uint32_t now, char* out, size_t cap) {
    JsonWriter w(out, cap);
    w.beginObject();
    w.key("frame"); w.num(_frame);
    w.key("part"); w.num(_part);
    const size_t n = _withheld ? 0 : (_bytes - _off < PART ? _bytes - _off : PART);
    const bool last = _withheld || _off + n >= _bytes;
    w.key("last"); w.boolean(last);
    w.key("width"); w.num(_d->width());
    w.key("height"); w.num(_d->height());
    w.key("format"); w.str("mono1");
    w.key("on"); w.boolean(_on);
    if (_withheld) {
      w.key("withheld"); w.str(_withheld);
    } else {
      w.key("age_s"); w.num(_age_s);
      w.key("offset"); w.num((int64_t)_off);
      char* b64 = reinterpret_cast<char*>(_buf + FRAME_MAX);
      base64Encode(b64, _buf + _off, n);
      w.key("data"); w.str(b64);
    }
    w.endObject();
    _off += n;
    _part++;
    _next = now + PART_MS;
    if (last || !w.ok()) cancel();
    return w.ok() ? w.size() : 0;
  }

private:
  static size_t b64Size() { return (PART + 2) / 3 * 4 + 1; }
  void release() { free(_buf); _buf = nullptr; }

  DisplayDriver* _d = nullptr;
  uint8_t* _buf = nullptr;   // the frame, then room for one part's base64: held only while a frame is being sent
  const char* _withheld = nullptr;
  uint32_t _frame = 0, _part = 0, _next = 0, _age_s = 0;
  size_t _bytes = 0, _off = 0;
  bool _on = false, _pending = false;
};

}  // namespace Iface
