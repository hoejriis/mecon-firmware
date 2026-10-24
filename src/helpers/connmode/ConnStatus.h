#pragma once

// What the connectivity status screen and the home-screen status line say, as text.
//
// Pure formatting over a plain struct, so the wording and the "never a secret" rule are checked by native unit
// tests; the platform layer (examples/companion_radio/ui-new/ConnStatusScreen.h) fills the struct from the live
// controllers. The struct has no field that could carry a password, a key or a PIN digit: the screen cannot draw
// what it is never given. (The BLE pairing PIN stays where the stock firmware shows it, on the home screen.)

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ConnMode.h"

namespace ConnMode {

// How the MQTT session is secured, as the screen words it.
enum Tls : uint8_t { TLS_NONE = 0, TLS_PLAIN = 1, TLS_VERIFIED = 2, TLS_UNVERIFIED = 3 };

struct ConnStatus {
  uint8_t mode = BLE;
  uint8_t posture = 0;
  // BLE
  bool ble_pin_default = false;   // the PIN is the published default (0 or 123456)
  bool ble_connected = false;     // a phone is connected over BLE
  // Wi-Fi
  bool wifi_has_profile = false;  // at least one profile (or the build-time network) is configured
  bool wifi_running = false;      // the Wi-Fi controller was started at boot
  bool wifi_connected = false;
  const char* wifi_state = "off"; // WifiCore::stateName() when not connected
  int8_t wifi_profile = -1;       // profile number while connecting or connected
  char ssid[33] = {0};            // network name (not a secret)
  int rssi = 0;
  char ip[20] = {0};
  // MQTT
  bool mqtt_has_profile = false;  // at least one broker profile is configured
  bool mqtt_running = false;
  bool mqtt_connected = false;
  const char* mqtt_state = "off"; // MqttCore::stateName() when not connected
  int8_t mqtt_profile = -1;
  uint8_t mqtt_tls = TLS_NONE;
  // Memory
  bool heap_low = false;          // the largest free heap block is below the floor (helpers/HeapFloor.h)
};

enum { LINES = 4, LINE_LEN = 40, HOME_COLUMNS = 21 };   // a screen line is 21 characters wide

// Fills up to LINES lines for the status screen; `warn[i]` marks a line the screen draws as a warning.
// Returns the number of lines.
inline int formatLines(const ConnStatus& s, char lines[LINES][LINE_LEN], bool warn[LINES]) {
  for (int i = 0; i < LINES; i++) { lines[i][0] = 0; warn[i] = false; }
  const bool wifi = sanitize(s.mode) == WIFI;
  snprintf(lines[0], LINE_LEN, "Mode: %s, %s", wifi ? "Wi-Fi" : "BLE", Posture::isHardened(s.posture) ? "Hardened" : "Normal");
  if (!wifi) {
    snprintf(lines[1], LINE_LEN, "Pin: %s", s.ble_pin_default ? "DEFAULT (unsafe)" : "per-device");
    warn[1] = s.ble_pin_default;
    snprintf(lines[2], LINE_LEN, "Phone: %s", s.ble_connected ? "connected" : "not connected");
    snprintf(lines[3], LINE_LEN, "Wi-Fi, MQTT: off");
    return LINES;
  }
  if (!s.wifi_has_profile) {
    snprintf(lines[1], LINE_LEN, "Wi-Fi: no profile");
  } else if (!s.wifi_running) {
    snprintf(lines[1], LINE_LEN, "Wi-Fi: off");   // Hardened without an MQTT profile Hardened permits
  } else if (s.wifi_connected) {
    snprintf(lines[1], LINE_LEN, "Wi-Fi p%d %s %ddBm", (int)s.wifi_profile, s.ssid, s.rssi);
  } else if (s.wifi_profile >= 0) {
    snprintf(lines[1], LINE_LEN, "Wi-Fi p%d %s", (int)s.wifi_profile, s.wifi_state);
  } else {
    snprintf(lines[1], LINE_LEN, "Wi-Fi: %s", s.wifi_state);
  }
  snprintf(lines[2], LINE_LEN, "IP: %s", s.wifi_connected && s.ip[0] ? s.ip : "-");
  if (!s.mqtt_has_profile) {
    snprintf(lines[3], LINE_LEN, "MQTT: no profile");
  } else if (!s.mqtt_running) {
    snprintf(lines[3], LINE_LEN, "MQTT: off");
  } else if (s.mqtt_connected) {
    const char* t = s.mqtt_tls == TLS_VERIFIED ? "TLS verified" : s.mqtt_tls == TLS_UNVERIFIED ? "TLS UNVERIFIED" : "plain";
    snprintf(lines[3], LINE_LEN, "MQTT%d %s", (int)s.mqtt_profile, t);   // short: a line is 21 characters wide
    warn[3] = s.mqtt_tls == TLS_UNVERIFIED;
  } else {
    snprintf(lines[3], LINE_LEN, "MQTT: %s", s.mqtt_state);
  }
  return LINES;
}

// The one status line on the home screen. While the heap is low the line carries the marker LOW MEM: after the normal
// text when both fit the 21-character line, else alone (the connectivity page still has the details).
inline void formatHomeStatus(const ConnStatus& s, char* out, size_t cap);
inline void formatHome(const ConnStatus& s, char* out, size_t cap) {
  formatHomeStatus(s, out, cap);
  if (!s.heap_low) return;
  static const char MARK[] = "LOW MEM";
  const size_t len = strlen(out);
  if (len + 1 + (sizeof(MARK) - 1) <= HOME_COLUMNS && cap > len + sizeof(MARK)) snprintf(out + len, cap - len, " %s", MARK);
  else snprintf(out, cap, "%s", MARK);
}

// What the home page draws under the status line: the BLE pairing PIN until a central has connected over BLE, then
// "< Connected >". Only the BLE link decides whether the PIN shows: a USB serial interface reports itself connected
// whenever it exists, so asking whether anything is connected hid the PIN for good on a build with USB and BLE.
enum HomeLink : uint8_t { LINK_NONE = 0, LINK_PIN = 1, LINK_CONNECTED = 2 };
inline HomeLink homeLink(uint32_t pin, bool ble_mode, bool ble_connected, bool any_connected) {
  if (pin != 0 && ble_mode && !ble_connected) return LINK_PIN;
  return any_connected ? LINK_CONNECTED : LINK_NONE;
}

inline void formatHomeStatus(const ConnStatus& s, char* out, size_t cap) {
  if (sanitize(s.mode) == BLE) {
    snprintf(out, cap, "%s", s.ble_pin_default ? "BLE (default PIN!)" : "BLE");
  } else if (!s.wifi_has_profile) {
    snprintf(out, cap, "Wi-Fi: no profile");
  } else if (s.wifi_connected && s.ip[0]) {
    snprintf(out, cap, "Wi-Fi %s", s.ip);
  } else {
    snprintf(out, cap, "Wi-Fi: %s", s.wifi_running ? s.wifi_state : "off");
  }
}

// The label the confirmation asks about.
inline const char* switchTargetLabel(uint8_t mode) { return sanitize(mode) == WIFI ? "BLE" : "Wi-Fi"; }

}  // namespace ConnMode
