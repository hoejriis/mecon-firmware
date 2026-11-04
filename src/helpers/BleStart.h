#pragma once
#include <stdint.h>

// Whether this boot's BLE stack came up. Every BLE interface reports its start here and status.get says so: a failed
// start was silent before, and from outside it looks the same as a phone that has not connected (#165).
namespace BleStart {

enum State : uint8_t { NOT_TRIED, STARTED, FAILED };

inline State& state() {
  static State s = NOT_TRIED;
  return s;
}
// One step of the start. A step that failed stays failed until the next boot; a later step cannot clear it.
inline void report(bool ok) {
  if (!ok) state() = FAILED;
  else if (state() == NOT_TRIED) state() = STARTED;
}

}  // namespace BleStart
