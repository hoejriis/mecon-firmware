#pragma once
// Prioritised multi-profile Wi-Fi controller: the thin platform layer over WifiCore (the decision logic) and the
// Arduino `WiFi` API. The TCP companion server uses it today; a later MQTT or connectivity-mode module asks the
// same instance for the link instead of joining a network itself.
//
// Uses what the Arduino WiFi classes of ESP32 and RP2040 share (begin, disconnect, status, async scan, RSSI,
// localIP) plus, on ESP32, the disconnect event for the exact failure reason and its own scan: ESP-IDF's
// esp_wifi_scan_start with a short dwell, its own done event and its own bound (see docs/WIFI.md, "Scanning"), because
// Arduino's async scan was seen never to report on hardware. No upstream Wi-Fi code is duplicated.
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
  // The link as plain values, for telemetry (docs/contract/TELEMETRY.md, connectivity).
  struct Snapshot {
    bool started = false;
    WifiState state = WifiState::Off;
    uint8_t profile = WifiCore::NONE;   // NONE when no profile is joining or joined
    const char* ssid = nullptr;
    int rssi = 0;                       // valid while connected
    char ip[20] = {0};
    uint32_t up_s = 0;
    WifiFail last_fail = WifiFail::None;   // of the current profile, or of profile 0 when none is current
  };
  void snapshot(Snapshot* out);
  void formatIp(char* out, size_t cap);   // "" when not connected

private:
  friend struct WifiControllerEvents;
  WifiCore _core;
  WifiProfiles* _profiles = nullptr;
  bool _started = false;
  bool _scanning = false;                 // a scan is running in the radio
  uint32_t _scan_t0 = 0;                  // when it was started
  uint32_t _scan_ev_at = 0;               // when loop() first saw its done event (0 = not yet)
  volatile bool _scan_armed = false;      // a scan of ours is running: the done event is ours to take
  volatile bool _ev_scan_done = false;    // the done event arrived (set from the event loop task)
  volatile bool _ev_scan_ok = false;      // ... and the driver says the scan completed
  volatile bool _ev_got_ip = false;       // set from the Wi-Fi task, consumed in loop()
  volatile bool _ev_disc = false;
  volatile uint8_t _ev_reason = 0;
  void doConnect(uint8_t profile);
  void scanStart(uint32_t now);           // start a scan, or report it failed to the core
  void scanPoll(uint32_t now);            // finish a running scan: results, failure, or the bound
  void scanAbort();                       // stop a scan that is still running in the driver
};

// The one controller of the device (function-local static: safe to touch from any translation unit).
WifiController& wifiController();

#if WIFI_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define WIFICTL_LOG(F, ...) Serial.printf("WiFi: " F "\n", ##__VA_ARGS__)
#else
  #define WIFICTL_LOG(...) {}
#endif
