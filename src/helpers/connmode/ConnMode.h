#pragma once

// Connectivity mode of the standalone Companion: BLE or Wi-Fi, never both.
//
// The mode is one byte kept with the prefs (atomic save) and applied at boot. A change saves the new mode and
// restarts the device into it, so the BLE and the Wi-Fi stacks are never both initialised and heap use stays
// predictable. USB management works in both modes.
//
//   who may change the mode            USB / UART console, BLE paired with a non-default PIN, the device button
//   who may not                        any remote caller (TCP client, remote CLI over the air): refused and audited
//   Hardened                           the button still switches (physical possession); the posture rules decide
//                                      what the new mode may start (BLE with a default PIN, TCP server)
//
// Pure logic: no file system, no clock, no radio, so it runs in the native unit tests.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../Posture.h"

namespace ConnMode {

enum Mode : uint8_t { BLE = 0, WIFI = 1 };

// A damaged byte starts BLE (the default), never both stacks.
inline uint8_t sanitize(uint8_t m) { return m == WIFI ? WIFI : BLE; }
inline const char* name(uint8_t m) { return sanitize(m) == WIFI ? "wifi" : "ble"; }
inline uint8_t other(uint8_t m) { return sanitize(m) == WIFI ? BLE : WIFI; }

// ---- start-up decisions: exactly one of the two radios stacks is brought up
inline bool bleMayStart(uint8_t mode) { return sanitize(mode) == BLE; }
inline bool wifiMayStart(uint8_t mode) { return sanitize(mode) == WIFI; }

// A BLE PIN is the published default when it is 0 ("build default") or 123456; every other PIN is the device's own.
inline bool pinIsDefault(uint32_t pin) { return pin == 0 || pin == 123456; }

// ---- who may change the mode: the transports that authenticate the peer by possession
inline bool callerMaySwitch(uint8_t caller) {
  return Cli::decide(Cli::contextForCaller(caller, Posture::NORMAL), Cli::C_POSTURE).allow;   // cli/CliPolicy.h
}

// Switch to `want` on behalf of `caller`. Returns true when the stored mode changed (the prefs must be saved and the
// device restarted). A refusal is audited; so is a change.
inline bool requestSwitch(uint8_t& mode, uint8_t want, uint8_t caller, uint32_t now) {
  if (!callerMaySwitch(caller)) {
    Posture::recordRefusal(now, caller, Posture::W_CONNMODE);
    return false;
  }
  want = sanitize(want);
  if (sanitize(mode) == want) return false;
  mode = want;
  Posture::audit().add(now, Posture::EV_MODE_SET, caller, want);
  return true;
}

// "get connectivity.mode" and "set connectivity.mode ble|wifi". Returns false when `cmd` is neither.
// `changed` is set when the stored mode changed: the caller saves the prefs and restarts after the reply went out.
inline bool handleCommand(const char* cmd, uint8_t caller, uint8_t& mode, uint32_t now, char* reply, size_t max,
                          bool& changed) {
  if (strcmp(cmd, "get connectivity.mode") == 0) {
    snprintf(reply, max, "> %s", name(mode));
    return true;
  }
  static const char SET[] = "set connectivity.mode";
  if (memcmp(cmd, SET, sizeof(SET) - 1) != 0 || (cmd[sizeof(SET) - 1] != 0 && cmd[sizeof(SET) - 1] != ' ')) return false;
  if (!callerMaySwitch(caller)) {
    Posture::recordRefusal(now, caller, Posture::W_CONNMODE);
    snprintf(reply, max, "ERR: not permitted");
    return true;
  }
  const char* arg = cmd + sizeof(SET) - 1;
  while (*arg == ' ') arg++;
  const bool to_ble = strcmp(arg, "ble") == 0;
  const bool to_wifi = strcmp(arg, "wifi") == 0;
  if (!to_ble && !to_wifi) {
    snprintf(reply, max, "ERR: set connectivity.mode ble | wifi");
    return true;
  }
  const uint8_t want = to_wifi ? WIFI : BLE;
  if (requestSwitch(mode, want, caller, now)) {
    changed = true;
    snprintf(reply, max, "OK - connectivity.mode %s, restarting", name(want));
  } else {
    snprintf(reply, max, "OK - already %s", name(want));
  }
  return true;
}

}  // namespace ConnMode
