#pragma once

// The one audit log of the device and the call that records a refusal in it. Split out of Posture.h so the
// command-line engine (src/helpers/cli/) and the posture code can both use it without including each other.

#include <stdint.h>
#include "AuditRing.h"

namespace Posture {

inline AuditRing& audit() {
  static AuditRing a;   // function-local: the examples construct their mesh object before main()
  return a;
}

inline void recordRefusal(uint32_t ts, uint8_t caller, uint8_t what) { audit().add(ts, EV_REFUSED, caller, what); }

}  // namespace Posture
