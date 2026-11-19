#pragma once
// Received packets for the observations stream (docs/contract/TELEMETRY.md): the dispatcher hands every packet
// here as the radio heard it, and the device interface glue, when present, registers itself as the sink. No
// dependencies, so every role's mesh code can include it. With no sink, or no subscriber, a call costs one test.
#include <stdint.h>

namespace Iface {

// What handling a received packet came to, where the code that decided it knows (TELEMETRY.md, `observations`).
enum Outcome : uint8_t {
  OUTCOME_UNKNOWN = 0,    // delivered here, or ignored for a reason that is not one of these, or not handled yet
  OUTCOME_FORWARDED,      // queued to be sent on
  OUTCOME_DUPLICATE,      // its hash was already seen
  OUTCOME_NOT_NEXT_HOP,   // a direct packet whose next hop is another node
  OUTCOME_FILTERED,       // not forwarded because this node's rules do not allow it
  OUTCOME_NO_ROUTE,       // a flood packet whose path has no room for this node's hash
  OUTCOME_UNPARSABLE      // the frame is not a valid packet
};

// What the dispatcher knows about a packet beyond its bytes.
struct ObserveInfo {
  const uint8_t* fp = nullptr;   // the packet hash, 8 bytes; null when the frame is not a valid packet
  bool freq_known = false;       // the radio's frequency and bandwidth as applied
  float freq_mhz = 0, bw_khz = 0;
  bool mod_known = false;        // the spreading factor and coding rate (5..8) as applied
  uint8_t sf = 0, cr = 0;
  uint8_t outcome = 0;           // what handling the packet came to (Outcome); 0: not known
};

typedef void (*ObserveFn)(void* user, float snr, float rssi, const uint8_t* raw, int len, const ObserveInfo& info);
typedef bool (*ObserveWantFn)(void* user);   // is any subscriber active: the dispatcher keeps what it needs only then

struct ObserveSink {
  ObserveFn fn = nullptr;
  ObserveWantFn want = nullptr;
  void* user = nullptr;
};

inline ObserveSink& observeSink() {
  static ObserveSink sink;
  return sink;
}

inline bool observing() {
  ObserveSink& s = observeSink();
  return s.fn && (!s.want || s.want(s.user));
}

inline void observe(float snr, float rssi, const uint8_t* raw, int len, const ObserveInfo& info) {
  ObserveSink& s = observeSink();
  if (s.fn) s.fn(s.user, snr, rssi, raw, len, info);
}

}  // namespace Iface
