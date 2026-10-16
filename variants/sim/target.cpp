#include "target.h"
#include "sim_config.h"
#include "sim_clock.h"
#include <random>
#include <stdlib.h>

SimBoard board;
SimRadio radio_driver;
SimRTCClock rtc_clock;
SensorManager sensors;

void SimBoard::reboot() { fflush(NULL); exit(75); }
void SimBoard::powerOff() { fflush(NULL); exit(0); }

SimRTCClock::SimRTCClock() { _base = 0; _set_at_us = 0; _init = false; }
uint32_t SimRTCClock::getCurrentTime() {
  if (!_init) { _init = true; _base = g_sim.rtc_epoch; } return _base + (uint32_t)((sim_clock()->nowUs() - _set_at_us) / 1000000); }
void SimRTCClock::setCurrentTime(uint32_t t) { _init = true; _base = t; _set_at_us = sim_clock()->nowUs(); }

bool radio_init() {
  radio_driver.begin();
  return true;
}

namespace {
class SimRNG : public mesh::RNG {
  std::mt19937 g;
public:
  explicit SimRNG(uint32_t seed) : g(seed) {}
  void random(uint8_t* dest, size_t sz) override { for (size_t i = 0; i < sz; i++) dest[i] = g() & 0xFF; }
};
}

mesh::LocalIdentity radio_new_identity() {
  static SimRNG rng(g_sim.seed * 2654435761u + 12345u);   // deterministic per node seed
  return mesh::LocalIdentity(&rng);
}

