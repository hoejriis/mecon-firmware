#pragma once
// USB/BLE framing (DEVICE_INTERFACE.md section 7): 0x7F | xid | index | count | up to 168 message bytes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "IfaceJson.h"   // MAX_MSG

namespace Iface {

const uint8_t FRAME_CODE = 0x7F;
const size_t FRAME_HDR = 4;
const size_t FRAME_PAYLOAD = 168;
const size_t FRAME_MAX = FRAME_HDR + FRAME_PAYLOAD;
const unsigned FRAME_MAX_COUNT = 25;      // 25 * 168 = 4200 >= MAX_MSG
const uint32_t REASSEMBLY_TIMEOUT_MS = 5000;
const int QUEUE_MESSAGES = 8;

// Frames needed for a message of len bytes; 0 when len is 0 or over MAX_MSG.
inline unsigned frameCount(size_t len) {
  return (len == 0 || len > MAX_MSG) ? 0 : (unsigned)((len + FRAME_PAYLOAD - 1) / FRAME_PAYLOAD);
}

// Splits one contiguous message into frames, in order.
class Fragmenter {
public:
  bool begin(const uint8_t* msg, size_t len, uint8_t xid) {
    _n = frameCount(len);
    if (!_n) return false;
    _msg = msg; _len = len; _xid = xid; _i = 0;
    return true;
  }
  bool done() const { return _i >= _n; }
  // Writes the next frame (out needs FRAME_MAX bytes) and returns its length, 0 when done.
  size_t next(uint8_t* out) {
    if (done()) return 0;
    size_t off = (size_t)_i * FRAME_PAYLOAD;
    size_t take = _len - off < FRAME_PAYLOAD ? _len - off : FRAME_PAYLOAD;
    out[0] = FRAME_CODE; out[1] = _xid; out[2] = (uint8_t)_i; out[3] = (uint8_t)_n;
    memcpy(out + FRAME_HDR, _msg + off, take);
    _i++;
    return FRAME_HDR + take;
  }
private:
  const uint8_t* _msg = nullptr; size_t _len = 0; uint8_t _xid = 0; unsigned _i = 0, _n = 0;
};

enum RxResult {
  RX_INCOMPLETE,   // frame accepted (or a duplicate ignored); more needed
  RX_COMPLETE,     // data()/size() hold the whole message until the next feed()
  RX_MALFORMED     // frame rejected; reassembly state unchanged
};

// One inbound message at a time. Frames of the message may arrive in any order; every frame but the last
// carries exactly FRAME_PAYLOAD bytes, which is what lets a frame be placed without waiting for its
// neighbours. About 4.1 KB: allocate it per transport only when the transport is enabled.
class Reassembler {
public:
  Reassembler() { reset(); }

  RxResult feed(const uint8_t* f, size_t len, uint32_t now_ms);
  // Discards an incomplete message older than the timeout. True when it did (see takeDiscard).
  bool tick(uint32_t now_ms);
  // Set when an incomplete message was dropped, by timeout or because a new xid arrived; reading clears it.
  bool takeDiscard(uint8_t* xid) { if (!_discarded) return false; *xid = _discarded_xid; _discarded = false; return true; }
  bool active() const { return _active; }
  const uint8_t* data() const { return _buf; }
  size_t size() const { return _total; }

private:
  void reset() { _active = false; _discarded = false; _mask = 0; _total = 0; _last_len = 0; _have_last = false; }
  void drop() { _discarded = true; _discarded_xid = _xid; _active = false; _mask = 0; _have_last = false; }
  uint8_t _buf[MAX_MSG];
  bool _active, _discarded, _have_last;
  uint8_t _xid, _count, _discarded_xid;
  uint32_t _mask, _start;
  size_t _total, _last_len;
};

// Bounded outbound queue for one transport: at most QUEUE_MESSAGES messages in caller-provided byte storage.
// Frames of one message are handed out contiguously, so messages never interleave.
class OutQueue {
public:
  enum Kind { EVENT, RESULT };
  enum Push { PUSH_OK, PUSH_DROPPED, PUSH_BUSY, PUSH_BAD };
  OutQueue(uint8_t* store, size_t cap) : _s(store), _cap(cap) {}

  // Full means 8 messages queued or no room for the bytes. A full queue drops an event (counted) and refuses a
  // result with PUSH_BUSY: the request that would have produced it is answered busy instead.
  Push push(Kind k, const uint8_t* msg, size_t len);
  // Next frame of the message at the head (out needs FRAME_MAX bytes); 0 when empty.
  size_t nextFrame(uint8_t* out);
  int count() const { return _count; }
  uint32_t droppedEvents() const { return _dropped; }
  uint32_t refusedResults() const { return _refused; }

private:
  void copyOut(size_t from, uint8_t* to, size_t n) const;
  uint8_t* _s; size_t _cap;
  size_t _head = 0, _used = 0;       // ring position of the head message and bytes held
  uint16_t _lens[QUEUE_MESSAGES];
  int _count = 0, _first = 0;        // _first: index into _lens of the head
  unsigned _next_frame = 0;
  uint8_t _xid = 0;
  uint32_t _dropped = 0, _refused = 0;
};

}  // namespace Iface
