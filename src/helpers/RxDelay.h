#pragma once
#include <math.h>
#include <stdint.h>

// The wait before a received packet is handled (Dispatcher::calcRxDelay): longer for a weaker packet, so that the
// better-placed nodes relay first. Upstream computes it with double pow; float powf gives the same milliseconds (to one
// at an integer boundary, test/test_rx_delay) and links no double-precision pow, which a 224 KB target cannot spare.
// score is packetScore()'s 0..1, air_time the packet's estimated airtime in ms.
namespace RxDelay {
inline int ms(float score, uint32_t air_time) {
  return (int)((powf(10.0f, 0.85f - score) - 1.0f) * air_time);
}
}  // namespace RxDelay
