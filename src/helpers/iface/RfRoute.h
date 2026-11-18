#pragma once
// How a manager sends a LoRa management request to a target whose path it does not know (#542). A flood is rebroadcast by
// every repeater in range, after its own delay, and a rebroadcast that lands while the target's direct reply is on the air
// destroys a frame of it; the first frame, sent soonest, most often. So such a request goes zero-hop first: nothing
// forwards it, and a neighbour (most managed targets) answers without any rebroadcast. A zero-hop attempt that heard nothing
// back within WAIT_MS sends the next request by flood. The route that got an answer is kept per target: zero-hop until a
// zero-hop attempt goes unanswered, flood until FLOOD_TTL_MS has passed, when zero-hop is tried again.
#include <stdint.h>
#include <string.h>

namespace Iface {

class RfRoute {
public:
  static const int N = 4;                         // targets remembered; the least recently used goes
  static const uint32_t WAIT_MS = 20000;          // a zero-hop attempt with no reply by then counts as unanswered
  static const uint32_t FLOOD_TTL_MS = 1800000;   // a target answered by flood is tried zero-hop again after this
  static const int KEY = 6;

  enum Mode : uint8_t { UNKNOWN = 0, ZERO_HOP, FLOOD };

  // Whether the next request to `key` goes by flood. Call sent() after sending it either way.
  bool flood(const uint8_t* key, uint32_t now) {
    Entry* e = find(key);
    if (!e) return false;
    if (e->mode == FLOOD && (uint32_t)(now - e->mode_ms) >= FLOOD_TTL_MS) e->mode = UNKNOWN;
    if (e->pending && !e->pending_flood && (uint32_t)(now - e->pending_ms) >= WAIT_MS) {
      e->pending = false;   // the zero-hop attempt went unanswered: flood from here
      e->mode = FLOOD;
      e->mode_ms = now;
    }
    return e->mode == FLOOD;
  }

  // A request went to `key`, by flood or zero-hop. The first one sent since the last reply is the one a reply is credited to.
  void sent(const uint8_t* key, bool by_flood, uint32_t now) {
    Entry* e = find(key);
    if (!e) e = take(key);
    e->used_ms = now;
    if (!e->pending) {
      e->pending = true;
      e->pending_flood = by_flood;
      e->pending_ms = now;
    }
  }

  // A management frame of this node's went on the air. A zero-hop attempt's wait runs from its last frame on the air, not
  // from when it was queued: frames held for airtime are late, not unanswered, and must not send the target to flood (#578).
  void onAir(uint32_t now) {
    for (Entry& e : _e)
      if (e.used && e.pending && !e.pending_flood) e.pending_ms = now;
  }

  // A reply frame came from `key`: the route of the attempt it answers is the route to keep.
  void heard(const uint8_t* key, uint32_t now) {
    Entry* e = find(key);
    if (!e || !e->pending) return;
    e->mode = e->pending_flood ? FLOOD : ZERO_HOP;
    e->mode_ms = now;
    e->pending = false;
  }

  Mode mode(const uint8_t* key) const {
    for (int i = 0; i < N; i++)
      if (_e[i].used && memcmp(_e[i].key, key, KEY) == 0) return (Mode)_e[i].mode;
    return UNKNOWN;
  }

private:
  struct Entry {
    uint8_t key[KEY];
    bool used, pending, pending_flood;
    uint8_t mode;
    uint32_t pending_ms, mode_ms, used_ms;
  };
  Entry _e[N] = {};

  Entry* find(const uint8_t* key) {
    for (int i = 0; i < N; i++)
      if (_e[i].used && memcmp(_e[i].key, key, KEY) == 0) return &_e[i];
    return nullptr;
  }
  Entry* take(const uint8_t* key) {
    Entry* e = &_e[0];
    for (int i = 0; i < N; i++) {
      if (!_e[i].used) { e = &_e[i]; break; }
      if ((int32_t)(_e[i].used_ms - e->used_ms) < 0) e = &_e[i];
    }
    memset(e, 0, sizeof(*e));
    memcpy(e->key, key, KEY);
    e->used = true;
    return e;
  }
};

}  // namespace Iface
