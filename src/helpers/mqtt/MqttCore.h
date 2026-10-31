#pragma once
// Decision logic of the MQTT controller: which broker profile to use, when to fail over, when to go back to a better
// one, when to browse mDNS. Platform-free (no Arduino, no ESP32 headers): the controller feeds it observations and
// time, it answers with one action at a time. Unit-tested natively (test/test_mqtt_core).
//
// Rules:
//  * One session at a time. Among the usable profiles that are available, the highest priority wins; equal
//    priorities go to the lower profile number. A static profile is available unless it is cooling down; a discovery
//    profile is available while a responder carrying its site ID is visible and not cooling down.
//  * Failover: a failed connect (DNS, TCP, TLS, pinned certificate, credentials, timeout), a session that drops, a
//    responder that vanishes from a browse, or a posture that forbids the profile ends the session and the next
//    available profile is tried. A failed profile cools down (escalating, so a wrong password or a rogue broker is not
//    retried every minute).
//  * Return: a better profile replaces the current session only when the session is at least the hold-down old and
//    the better profile is not cooling down; a discovery responder must also have been seen in every browse for the
//    whole hold-down. At most one interruption per hold-down time.
//  * Discovery: a responder is browsed only while the device is not on the best available profile. A responder that
//    advertises plain MQTT is never a candidate for a discovery profile. A candidate that fails its certificate check
//    cools down on its own, so a rogue broker does not shut out the real one that carries the same site ID.
#include <stdint.h>
#include "MqttProfiles.h"

enum class MqttState : uint8_t { Off, NoLink, Idle, Connecting, Connected, Backoff };
enum class MqttFail : uint8_t { None, Dns, Tcp, Tls, Pin, Auth, Timeout, Dropped, Lost, Posture, Plain, Ws };

struct MqttTiming {
  uint32_t rescan_ms = 60000;            // between browses while on a lower-priority profile (or with nothing up)
  uint32_t browse_timeout_ms = 10000;    // a browse that never reports back is abandoned
  uint32_t hold_down_ms = 60000;         // a better profile must be available this long, and the session this old
  uint32_t stable_ms = 30000;            // a session that lasted this long was a good one
  uint32_t drop_retry_ms = 5000;         // a good session dropped: the profile is retried after this
  uint32_t fail_cooldown_ms = 15000;     // a failed connect (or a session that died young): skipped this long, doubling ...
  uint32_t fail_cooldown_max_ms = 300000;  // ... up to this
  uint32_t auth_cooldown_ms = 120000;    // refused credentials: doubling per consecutive failure ...
  uint32_t auth_cooldown_max_ms = 3600000;
  uint32_t pin_cooldown_ms = 300000;     // a certificate that does not match the anchor: doubling ...
  uint32_t pin_cooldown_max_ms = 3600000;
};

struct MqttAction {
  enum Kind : uint8_t { None, Browse, Connect, Disconnect } kind = None;
  uint8_t profile = 0;   // for Connect
  uint8_t cand = 0xFF;   // for Connect to a discovery profile: the candidate (address and port via candidate())
};

struct MqttCandidate {
  uint32_t addr;   // IPv4, host order
  uint16_t port;
};

class MqttCore {
public:
  static constexpr uint8_t NONE = 0xFF;
  static constexpr uint8_t MAX_CANDS = 6;

  MqttCore() { }
  void configure(const MqttProfiles* profiles, const MqttTiming& timing = MqttTiming());

  // ---- lifecycle
  void begin(uint32_t now);                 // start (the link state is set separately)
  void stop();                              // Off; the caller drops the session
  void setPosture(uint8_t posture) { _posture = posture; }
  void setLink(uint32_t now, bool up);      // the Wi-Fi link
  // A profile was edited. `credentials`: anything but the priority. Re-evaluates without a reboot.
  void profileChanged(uint32_t now, uint8_t index, bool credentials);

  // ---- the next thing the platform layer must do (call every loop; None when nothing)
  MqttAction poll(uint32_t now);

  // ---- observations from the platform layer
  // One mDNS responder of a browse: its IPv4 address, port, whether it is the secure service type, and the TXT site ID.
  void browseObserve(uint32_t addr, uint16_t port, bool secure, const char* site);
  void browseDone(uint32_t now);
  void onConnected(uint32_t now, bool verified);   // after a Connect action
  void onConnectFailed(uint32_t now, MqttFail why);
  // The attempt in flight was abandoned by the platform layer (the posture now forbids the profile): back to Idle with
  // no penalty for the profile, so the next poll picks again. Only meaningful while Connecting.
  void onConnectCancelled();
  void onSessionLost(uint32_t now, MqttFail why);

  // ---- status
  MqttState state() const { return _state; }
  uint8_t current() const { return _state == MqttState::Connecting || _state == MqttState::Connected ? _cur : NONE; }
  bool browsing() const { return _browsing; }
  bool verified() const { return _verified; }
  MqttFail lastFail(uint8_t i) const;
  uint32_t connectedMs(uint32_t now) const { return _state == MqttState::Connected ? now - _connected_at : 0; }
  uint32_t connectCount() const { return _connect_count; }
  bool candidate(uint8_t cand, MqttCandidate& out) const;
  static const char* stateName(MqttState s);
  static const char* failName(MqttFail f);

private:
  struct Per {
    MqttFail fail;
    bool cooling;
    uint32_t cool_until;
    uint8_t fails;       // consecutive, reset by a good session or an edit
    bool skip_hold;      // edited by the owner: the next selection ignores the hold-down
  };
  struct Cand {
    bool used;
    uint8_t prof;
    uint32_t addr;
    uint16_t port;
    bool vis;            // seen in the last browse
    bool seen_now;       // seen in the browse in progress
    uint32_t vis_since;  // when the current run of consecutive sightings began
    bool cooling;
    uint32_t cool_until;
    uint8_t fails;
  };
  static constexpr uint8_t STATIC_OK = 0xFE;

  const MqttProfiles* _profiles = nullptr;
  MqttTiming _t;
  MqttState _state = MqttState::Off;
  uint8_t _posture = 0;
  bool _link = false;
  Per _p[MqttProfiles::COUNT];
  Cand _c[MAX_CANDS];
  uint8_t _cur = NONE;
  uint8_t _cur_cand = NONE;
  uint8_t _force = NONE;        // a switch-up chose this profile; the next Connect goes there
  uint8_t _force_cand = NONE;
  uint32_t _connected_at = 0;
  uint32_t _connect_count = 0;
  bool _verified = false;
  bool _pending_disconnect = false;
  bool _browsing = false;
  uint32_t _browse_started = 0;
  uint32_t _next_browse = 0;

  static bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
  bool usable(uint8_t i) const;
  bool better(uint8_t a, uint8_t b) const;
  bool profileCooling(uint8_t i, uint32_t now);
  bool candCooling(Cand& c, uint32_t now);
  uint8_t availability(uint8_t i, uint32_t now, bool upgrade);   // NONE, STATIC_OK or a candidate index
  uint8_t pick(uint32_t now, bool upgrade, uint8_t* cand_out);
  bool browseWanted(uint32_t now);
  void startBrowse(uint32_t now);
  void failCurrent(uint32_t now, MqttFail why, bool session);
  uint32_t escalate(uint32_t base, uint32_t max, uint8_t fails) const;
  void dropCands(uint8_t prof);
  int findCand(uint8_t prof, uint32_t addr, uint16_t port) const;
  int freeCandSlot() const;
};
