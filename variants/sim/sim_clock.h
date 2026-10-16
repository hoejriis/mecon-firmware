// The node's time source is an interface so the real-time implementation can be
// swapped for a broker- or runner-driven virtual clock without touching the shims.
#pragma once
#include <stdint.h>

class SimClock {
public:
  virtual ~SimClock() {}
  virtual uint64_t nowUs() = 0;               // monotonic microseconds since process start
  virtual void sleepUs(uint64_t us) = 0;      // block (or advance) by this much
};

// Wall clock multiplied by g_sim.time_scale.
SimClock* sim_real_clock();
void sim_set_clock(SimClock* c);
SimClock* sim_clock();
