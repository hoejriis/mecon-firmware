// Stub of the ESP32 BLE serial interface (selected by SIM_BLE_STUB in examples/companion_radio/main.cpp) for the simulated board: no radio, no GATT, no pairing. It exists so the
// standalone Companion's mode logic and screen run in the simulator: it logs when the stack would be brought up
// ("SIMBLE begin ..."), classifies its transport like the real one (trusted only with a PIN that is not the
// published default) and reports a connected phone when --ble-env names a file that says "connected".
// It carries no frames. Whatever depends on a real BLE link is not verified by the simulator.
#pragma once
#include <helpers/BaseSerialInterface.h>
#include <stdio.h>
#include <string>
#include "sim_config.h"
#include "sim_clock.h"

class SerialBLEInterface : public BaseSerialInterface {
  bool _enabled = false;
  uint32_t _pin = 0;
public:
  bool isTrustedTransport() const override { return _pin != 0 && _pin != 123456; }   // as the real class
  void begin(const char*, char*, uint32_t pin) {
    _pin = pin;
    fprintf(stderr, "SIMBLE begin pin=%s\n", (pin == 0 || pin == 123456) ? "default" : "per-device");
    fflush(stderr);
  }
  void enable() override { _enabled = true; }
  void disable() override { _enabled = false; }
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override {
    if (!_enabled || g_sim.ble_env.empty()) return false;
    static unsigned long next = 0;   // the file is read at most every 100 ms
    static bool last = false;
    unsigned long now = millis();
    if (now >= next) {
      next = now + 100;
      FILE* f = fopen(g_sim.ble_env.c_str(), "r");
      char b[16] = {0};
      size_t n = f ? fread(b, 1, sizeof(b) - 1, f) : 0;
      if (f) fclose(f);
      last = n >= 9 && !strncmp(b, "connected", 9);
    }
    return last;
  }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t[], size_t) override { return 0; }
  size_t checkRecvFrame(uint8_t[]) override { return 0; }
};
