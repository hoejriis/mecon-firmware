#pragma once
#include <stdint.h>

// The Companion's delayed contacts save after a write error: try again a few times, further apart each time, then
// wait for the next change. A file system that keeps failing must not keep a sleep-capable board awake.
namespace LazySaveRetry {
static const uint8_t ATTEMPTS = 3;

// Delay before retry number `attempt` (1 for the first retry), or 0 once the retries are spent.
inline uint32_t delayMs(uint8_t attempt) {
  static const uint32_t delays[ATTEMPTS] = {5000, 15000, 45000};
  return (attempt >= 1 && attempt <= ATTEMPTS) ? delays[attempt - 1] : 0;
}
}
