#pragma once
// Stored MQTT broker profiles and their CLI. Platform-free: no Arduino, no ESP32 headers, so it is unit-tested natively.
//
// Up to MECON_MQTT_MAX_PROFILES profiles, each a priority, optional credentials and one of two kinds:
//   Static     a host name or IPv4 address and a port, TLS on or off, and (with TLS) an optional pinned certificate;
//   Discovery  an enrolled LAN broker: a site ID that must appear in the mDNS TXT record and the SHA-256 fingerprint
//              of the TLS certificate the broker must present. Always TLS, never plain.
// Passwords are write-only: nothing in this file ever copies one into a reply.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../Posture.h"

#ifndef MECON_MQTT_MAX_PROFILES
  #define MECON_MQTT_MAX_PROFILES 6
#endif
static_assert(MECON_MQTT_MAX_PROFILES >= 2 && MECON_MQTT_MAX_PROFILES <= 10, "profile numbers are one digit");

class MqttCore;

struct MqttProfile {
  enum Kind : uint8_t { Unset = 0, Static = 1, Discovery = 2 };
  static constexpr size_t HOST_BUF = 64;     // 63 characters
  static constexpr size_t SITE_BUF = 24;     // 23 characters
  static constexpr size_t ANCHOR_BUF = 65;   // SHA-256 as 64 lower-case hex digits
  static constexpr size_t USER_BUF = 32;
  static constexpr size_t PWD_BUF = 64;

  uint8_t kind;
  uint8_t prio;
  uint8_t tls;       // Static only; a Discovery profile is always TLS
  uint16_t port;     // 0 = the default for the profile's TLS setting (8883 / 1883)
  char host[HOST_BUF];
  char site[SITE_BUF];
  char anchor[ANCHOR_BUF];
  char user[USER_BUF];
  char pwd[PWD_BUF];

  bool useTls() const { return kind == Discovery || tls != 0; }
  uint16_t effectivePort() const { return port ? port : (useTls() ? 8883 : 1883); }
  bool hasAnchor() const { return anchor[0] != 0; }
};

struct MqttProfiles {
  static constexpr uint8_t COUNT = MECON_MQTT_MAX_PROFILES;
  static constexpr uint8_t DEFAULT_PRIORITY = 50;   // higher number = tried first; equal priority = lower number first

  MqttProfile p[COUNT];

  MqttProfiles() { for (uint8_t i = 0; i < COUNT; i++) clear(i); }

  // A profile is usable when its kind is set and everything that kind needs is present.
  bool configured(uint8_t i) const {
    if (i >= COUNT) return false;
    const MqttProfile& q = p[i];
    if (q.kind == MqttProfile::Static) return q.host[0] != 0;
    if (q.kind == MqttProfile::Discovery) return q.site[0] != 0 && validAnchor(q.anchor);
    return false;
  }
  uint8_t configuredCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < COUNT; i++) n += configured(i) ? 1 : 0;
    return n;
  }
  // The Hardened rule: a static profile without TLS is not used. A discovery profile is TLS-pinned already.
  static bool permitted(const MqttProfile& q, uint8_t posture) {
    return q.kind != MqttProfile::Static || q.tls != 0 || !Posture::isHardened(posture);
  }
  bool anyPermitted(uint8_t posture) const {
    for (uint8_t i = 0; i < COUNT; i++) if (configured(i) && permitted(p[i], posture)) return true;
    return false;
  }
  void clear(uint8_t i) {
    if (i >= COUNT) return;
    memset(&p[i], 0, sizeof(p[i]));
    p[i].prio = DEFAULT_PRIORITY;
    p[i].tls = 1;   // secure unless the owner says otherwise
  }
  static bool validAnchor(const char* a) {
    if (strlen(a) != 64) return false;
    for (int i = 0; i < 64; i++) {
      char c = a[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
  }
};

// What a profile command changed, so the controller can react without a reboot.
struct MqttProfileChange {
  bool changed = false;
  uint8_t index = 0;
  bool credentials = false;   // anything but the priority: the session may need to be redone
};

// Handles `set mqtt.profile <n> type|host|port|tls|anchor|site|user|pwd|priority <value>`,
// `set mqtt.profile <n> clear`, `get mqtt.profile <n>` and `get mqtt.profiles [from]`.
// Returns false when `command` is none of these. `core` is optional; with it the listing carries each profile's last
// failure. `posture` is the current security posture. `reply_cap` includes the NUL.
bool mqttProfileCommand(MqttProfiles& profiles, const MqttCore* core, uint8_t posture, const char* command,
                        char* reply, size_t reply_cap, MqttProfileChange& change);
