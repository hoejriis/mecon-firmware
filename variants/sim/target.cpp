#include "target.h"
#include "sim_config.h"
#include "sim_clock.h"
#include <random>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

SimBoard board;
SimRadio radio_driver;
SimRTCClock rtc_clock;
SimSensorManager sensors;

bool SimSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (!(requester_permissions & TELEM_PERM_ENVIRONMENT)) return false;
  FILE* f = fopen((g_sim.dir + ".sensors").c_str(), "r");
  if (!f) return false;
  int channel;
  char quantity[24];
  float value;
  bool any = false;
  while (fscanf(f, "%d %23s %f", &channel, quantity, &value) == 3) {
    if (!strcmp(quantity, "temperature")) telemetry.addTemperature(channel, value);
    else if (!strcmp(quantity, "humidity")) telemetry.addRelativeHumidity(channel, value);
    else if (!strcmp(quantity, "pressure")) telemetry.addBarometricPressure(channel, value);
    else if (!strcmp(quantity, "voltage")) telemetry.addVoltage(channel, value);
    else continue;
    any = true;
  }
  fclose(f);
  return any;
}

static SimLocationProvider sim_gps;

LocationProvider* SimSensorManager::getLocationProvider() {
  FILE* f = fopen((g_sim.dir + ".gps").c_str(), "r");
  if (!f) return nullptr;
  fclose(f);
  return &sim_gps;
}

SimLocationProvider::State SimLocationProvider::read() const {
  State s;
  FILE* f = fopen((g_sim.dir + ".gps").c_str(), "r");
  if (!f) return s;
  char kind[8] = {0};
  if (fscanf(f, "%7s", kind) == 1) {
    if (!strcmp(kind, "fix")) {
      s.fix = fscanf(f, "%ld %ld %ld %ld", &s.lat, &s.lon, &s.alt, &s.sats) == 4;
      if (s.fix && fscanf(f, "%ld", &s.age) != 1) s.age = -1;
    } else if (fscanf(f, "%ld", &s.sats) != 1) {
      s.sats = 0;
    }
  }
  fclose(f);
  return s;
}

bool SimLocationProvider::fixAgeMs(uint32_t& out) const {
  const State s = read();
  if (s.age < 0) return false;
  out = (uint32_t)s.age * 1000;
  return true;
}

namespace {
struct ResetKnob { bool read = false; std::string why; };
ResetKnob& resetKnob() {
  static ResetKnob k;
  if (k.read) return k;
  k.read = true;
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  const std::string path = d + ".reset";
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return k;
  char line[64] = {0};
  if (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    k.why = line;
  }
  fclose(f);
  remove(path.c_str());
  return k;
}
}  // namespace

uint32_t SimBoard::getResetReason() const { return resetKnob().why.empty() ? 0 : 1; }
const char* SimBoard::getResetReasonString(uint32_t reason) {
  return reason && !resetKnob().why.empty() ? resetKnob().why.c_str() : "Not available";
}

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

