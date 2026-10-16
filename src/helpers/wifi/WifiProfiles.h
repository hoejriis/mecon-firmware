#pragma once
// Stored Wi-Fi profiles and their CLI. Platform-free: no Arduino, no ESP32 headers, so it is unit-tested natively.
//
// Up to MECON_WIFI_MAX_PROFILES profiles (SSID, password, priority). Profile 0 is the one the stock
// `wifi.ssid` / `wifi.pwd` keys and commands refer to, so a stock prefs file and stock commands keep working.
// Passwords are write-only: nothing in this file ever copies one into a reply.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef MECON_WIFI_MAX_PROFILES
  #define MECON_WIFI_MAX_PROFILES 10
#endif
static_assert(MECON_WIFI_MAX_PROFILES >= 3, "every Wi-Fi board keeps at least 3 profiles");
static_assert(MECON_WIFI_MAX_PROFILES <= 10, "profile numbers are one digit in the CLI and the prefs keys");

class WifiCore;

struct WifiProfiles {
  static constexpr uint8_t COUNT = MECON_WIFI_MAX_PROFILES;
  static constexpr uint8_t DEFAULT_PRIORITY = 50;   // higher number = tried first; equal priority = lower profile number first
  static constexpr size_t SSID_BUF = 33;            // 32 characters + NUL (the 802.11 limit)
  static constexpr size_t PWD_BUF = 64;             // 63 characters + NUL (the WPA2 passphrase limit)

  // Stored values, public so the prefs serializer can bind to them. Profile 0 is the stock wifi.ssid / wifi.pwd.
  char ssid_[COUNT][SSID_BUF];
  char pwd_[COUNT][PWD_BUF];
  uint8_t prio_[COUNT];

  // Compile-time default network (the WIFI_SSID / WIFI_PWD build flags): profile 0 when it has no stored SSID.
  const char* build_ssid = "";
  const char* build_pwd = "";

  WifiProfiles() {
    memset(ssid_, 0, sizeof(ssid_));
    memset(pwd_, 0, sizeof(pwd_));
    for (uint8_t i = 0; i < COUNT; i++) prio_[i] = DEFAULT_PRIORITY;
  }

  // Effective values: profile 0 falls back to the build-time network when nothing is stored for it.
  const char* ssid(uint8_t i) const { return i == 0 && !ssid_[0][0] ? build_ssid : ssid_[i]; }
  const char* pwd(uint8_t i) const { return i == 0 && !ssid_[0][0] ? build_pwd : pwd_[i]; }
  bool configured(uint8_t i) const { return i < COUNT && ssid(i)[0] != 0; }
  bool hasPwd(uint8_t i) const { return pwd(i)[0] != 0; }
  uint8_t priority(uint8_t i) const { return prio_[i]; }
  uint8_t configuredCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < COUNT; i++) n += configured(i) ? 1 : 0;
    return n;
  }

  // Setters return false (and change nothing) when the value does not fit.
  bool setSsid(uint8_t i, const char* v) { return i < COUNT && copy(ssid_[i], SSID_BUF, v); }
  bool setPwd(uint8_t i, const char* v) { return i < COUNT && copy(pwd_[i], PWD_BUF, v); }
  void setPriority(uint8_t i, uint8_t p) { if (i < COUNT) prio_[i] = p; }
  void clear(uint8_t i) {
    if (i >= COUNT) return;
    ssid_[i][0] = 0;
    pwd_[i][0] = 0;
    prio_[i] = DEFAULT_PRIORITY;
  }

private:
  static bool copy(char* dst, size_t cap, const char* v) {
    size_t n = strlen(v);
    if (n >= cap) return false;
    memcpy(dst, v, n + 1);
    return true;
  }
};

// What a profile command changed, so the controller can react without a reboot.
struct WifiProfileChange {
  bool changed = false;
  uint8_t index = 0;
  bool credentials = false;   // SSID or password (the link may need to be redone), not only the priority
};

// Handles `set wifi.profile <n> ssid|pwd|priority <value>`, `set wifi.profile <n> clear`,
// `get wifi.profile <n>` and `get wifi.profiles [from]`. Returns false when `command` is none of these.
// `core` is optional; with it the listing carries each profile's last failure reason. `reply_cap` includes the NUL.
bool wifiProfileCommand(WifiProfiles& profiles, const WifiCore* core, const char* command, char* reply,
                        size_t reply_cap, WifiProfileChange& change);
