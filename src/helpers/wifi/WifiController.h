#pragma once
// Prioritised multi-profile Wi-Fi controller: the thin platform layer over WifiCore (the decision logic) and the
// Arduino `WiFi` API. The TCP companion server uses it today; a later MQTT or connectivity-mode module asks the
// same instance for the link instead of joining a network itself.
//
// Uses only what the Arduino WiFi classes of ESP32 and RP2040 share (begin, disconnect, status, async scan, RSSI,
// localIP) plus, on ESP32, the disconnect event for the exact failure reason. No upstream Wi-Fi code is duplicated.
#include <stddef.h>
#include <stdint.h>
#include "WifiCore.h"
#include "WifiProfiles.h"

class WifiController {
public:
  // Take the profile store (it must outlive the controller) and start: scan, then join the best visible profile.
  void begin(WifiProfiles* profiles, const WifiTiming& timing = WifiTiming());
  void loop();                                              // call from the main loop
  void profileChanged(uint8_t index, bool credentials);     // a profile was edited; applied at once, no reboot
  bool started() const { return _started; }
  bool connected() const { return _started && _core.state() == WifiState::Connected; }
  const WifiCore* core() const { return &_core; }
  const WifiProfiles* profiles() const { return _profiles; }

  // One status line: state, profile, SSID, RSSI, IP, time on the network. Never contains a password.
  void formatStatus(char* out, size_t cap);
  void formatIp(char* out, size_t cap);   // "" when not connected

private:
  friend struct WifiControllerEvents;
  WifiCore _core;
  WifiProfiles* _profiles = nullptr;
  bool _started = false;
  bool _scanning = false;                 // an async scan is running in the radio
  volatile bool _ev_got_ip = false;       // set from the Wi-Fi task, consumed in loop()
  volatile bool _ev_disc = false;
  volatile uint8_t _ev_reason = 0;
  void doConnect(uint8_t profile);
};

// The one controller of the device (function-local static: safe to touch from any translation unit).
WifiController& wifiController();

#if WIFI_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define WIFICTL_LOG(F, ...) Serial.printf("WiFi: " F "\n", ##__VA_ARGS__)
#else
  #define WIFICTL_LOG(...) {}
#endif
