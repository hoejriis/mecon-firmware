#pragma once
// Stored MQTT broker profiles and their CLI. Platform-free: no Arduino, no ESP32 headers, so it is unit-tested natively.
//
// Up to MECON_MQTT_MAX_PROFILES profiles, each a priority, optional credentials and one of two kinds:
//   Static     a host name or IPv4 address and a port, TLS on or off, and (with TLS) an optional pinned certificate;
//   Discovery  an enrolled LAN broker: a site ID that must appear in the mDNS TXT record and a TLS trust anchor the
//              broker must satisfy. Always TLS, never plain.
// The trust anchor is either the SHA-256 fingerprint of the broker's own (leaf) certificate, or a CA certificate that
// the broker's certificate must chain to (stored as base64 of the DER, no PEM armour, in heap allocated only when set).
// Passwords are write-only: nothing in this file ever copies one into a reply.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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
  // A CA certificate is held as base64 text of its DER encoding: at most CA_B64_MAX characters (1,440 DER bytes; an
  // RSA-4096 root is about 1,400, an EC P-256 CA about 450). Prefs store it in CA_CHUNK-character pieces because one
  // prefs token holds 127 characters.
  static constexpr size_t CA_B64_MAX = 1920;
  static constexpr size_t CA_CHUNK = 96;
  static constexpr size_t CA_CHUNKS = CA_B64_MAX / CA_CHUNK;

  uint8_t kind;
  uint8_t prio;
  uint8_t tls;       // Static only; a Discovery profile is always TLS
  uint8_t manage;    // 1: this broker may manage the device over MQTT even in Hardened (docs/CLI.md); set locally only
  uint16_t port;     // 0 = the default for the profile's TLS setting (8883 / 1883)
  char host[HOST_BUF];
  char site[SITE_BUF];
  char anchor[ANCHOR_BUF];
  char user[USER_BUF];
  char pwd[PWD_BUF];
  char* ca;          // base64 DER of the CA certificate anchor, or nullptr; owned by MqttProfiles

  bool useTls() const { return kind == Discovery || tls != 0; }
  uint16_t effectivePort() const { return port ? port : (useTls() ? 8883 : 1883); }
  bool hasCa() const { return ca != nullptr && ca[0] != 0; }
  bool hasAnchor() const { return anchor[0] != 0 || hasCa(); }
  // 0 unset, 1 leaf fingerprint, 2 CA certificate (a profile holds at most one)
  uint8_t anchorKind() const { return hasCa() ? 2 : (anchor[0] ? 1 : 0); }
};

// The CA certificate text: base64 of one DER certificate. Returns the DER length written to `der` (at most `cap`),
// or -1 when the text is not base64, is not a single DER SEQUENCE whose declared length matches, or does not fit.
int mqttCaDecode(const char* b64, uint8_t* der, size_t cap);
// True when a host string is an IPv4 literal (digits and dots only): such a profile has no host name to check.
bool mqttHostIsLiteral(const char* host);

struct MqttProfiles {
  static constexpr uint8_t COUNT = MECON_MQTT_MAX_PROFILES;
  static constexpr uint8_t DEFAULT_PRIORITY = 50;   // higher number = tried first; equal priority = lower number first

  MqttProfile p[COUNT];

  MqttProfiles() {
    for (uint8_t i = 0; i < COUNT; i++) { p[i].ca = nullptr; clear(i); }
  }
  ~MqttProfiles();
  MqttProfiles(const MqttProfiles&) = delete;             // profiles own heap (CA certificates)
  MqttProfiles& operator=(const MqttProfiles&) = delete;

  // CA certificate staging: a CA does not fit one command line, so `ca begin`, `ca add` ... and `ca end` build it in a
  // buffer that exists only meanwhile and install it whole, or not at all. Nothing is saved before `ca end`.
  char* stage_ = nullptr;
  uint16_t stage_len_ = 0;
  uint8_t stage_idx_ = 0;
  void dropStage();
  bool setCa(uint8_t i, const char* b64);       // validates; false leaves the profile as it was
  void clearCa(uint8_t i);
  // Prefs loading: chunk `n` (CA_CHUNK characters but the last) of profile i's CA; false when out of order.
  bool loadCaChunk(uint8_t i, uint8_t n, const char* chunk);
  // Prefs saving: chunk `n` of profile i's CA into `out` (CA_CHUNK + 1 bytes); false when there is no such chunk.
  bool caChunk(uint8_t i, uint8_t n, char* out) const;

  // A profile is usable when its kind is set and everything that kind needs is present.
  bool configured(uint8_t i) const {
    if (i >= COUNT) return false;
    const MqttProfile& q = p[i];
    if (q.kind == MqttProfile::Static) return q.host[0] != 0;
    if (q.kind == MqttProfile::Discovery) return q.site[0] != 0 && (validAnchor(q.anchor) || q.hasCa());
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
    if (stage_ && stage_idx_ == i) dropStage();
    free(p[i].ca);
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

// Handles `set mqtt.profile <n> type|host|port|tls|anchor|site|user|pwd|priority|manage <value>`,
// `set mqtt.profile <n> clear`, `get mqtt.profile <n>` and `get mqtt.profiles [from]`.
// Returns false when `command` is none of these. `core` is optional; with it the listing carries each profile's last
// failure. `posture` is the current security posture. `reply_cap` includes the NUL.
bool mqttProfileCommand(MqttProfiles& profiles, const MqttCore* core, uint8_t posture, const char* command,
                        char* reply, size_t reply_cap, MqttProfileChange& change);
