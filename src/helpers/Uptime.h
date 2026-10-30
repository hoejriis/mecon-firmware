#pragma once
#include <stdint.h>

// Time since boot without the 49.7-day wrap of a 32-bit millisecond counter (#165). Each call adds the milliseconds
// since the previous one, so it must be called at least once per wrap: every loop pass calls it. One accumulator for
// the whole firmware (a function-local static of an inline function is one object across every unit that uses it).
// The 64-bit total is not updated atomically: call it only from the loop task, or from a job the loop task waits on
// (helpers/NrfWorker.h), never from an interrupt or another task.
namespace Uptime {

inline uint64_t ms(uint32_t now_ms) {
  static uint32_t last = 0;
  static uint64_t total = 0;
  total += (uint32_t)(now_ms - last);   // unsigned difference: right across a wrap
  last = now_ms;
  return total;
}

inline uint64_t secs(uint32_t now_ms) { return ms(now_ms) / 1000; }

}  // namespace Uptime
