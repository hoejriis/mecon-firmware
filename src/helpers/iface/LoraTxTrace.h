#pragma once
// What became of the LoRa management frames this node queued (#542): queued, on air (the Dispatcher's logTx), failed
// (logTxFail) or held for airtime (onDutyHold), with a message's first frame counted apart from the rest. In night run 2
// every incomplete reply lacked frame 0, and the receiver's radio never decoded it: these say whether it left the sender.
#include <stdint.h>
#include <string.h>

namespace Iface {

class LoraTxTrace {
public:
  static const int SLOTS = 8;   // frames in flight at once: one reply or request of up to 8 frames
  struct Counts { uint32_t first_queued, first_sent, rest_queued, rest_sent, failed, held, untracked; };

  // A frame handed to the Dispatcher, by its packet hash. The oldest slot goes when all are taken, and is counted.
  void queued(const uint8_t hash[8], bool first) {
    Slot& s = _s[_next];
    if (s.used) _c.untracked++;
    memcpy(s.hash, hash, 8); s.first = first; s.used = true;
    _next = (_next + 1) % SLOTS;
    (first ? _c.first_queued : _c.rest_queued)++;
  }
  void sent(const uint8_t hash[8]) { if (Slot* s = find(hash)) { (s->first ? _c.first_sent : _c.rest_sent)++; s->used = false; } }
  void failed(const uint8_t hash[8]) { if (Slot* s = find(hash)) { _c.failed++; s->used = false; } }
  void held(const uint8_t hash[8]) { if (find(hash)) _c.held++; }
  const Counts& counts() const { return _c; }
  bool any() const { return _c.first_queued || _c.rest_queued; }

private:
  struct Slot { uint8_t hash[8]; bool first, used; };
  Slot* find(const uint8_t hash[8]) {
    for (Slot& s : _s) if (s.used && memcmp(s.hash, hash, 8) == 0) return &s;
    return nullptr;
  }
  Slot _s[SLOTS] = {};
  int _next = 0;
  Counts _c = {};
};

}  // namespace Iface
