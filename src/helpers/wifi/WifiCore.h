#pragma once
// Decision logic of the Wi-Fi controller: which profile to join, when to fail over, when to go back to a better
// one, how long to back off. Platform-free (no Arduino, no ESP32 headers): the controller feeds it observations
// and time, it answers with one action at a time. Unit-tested natively (test/test_wifi_core).
//
// Rules:
//  * Selection: among the profiles seen in the last scan and not cooling down, the highest priority wins;
//    equal priorities go to the lower profile number.
//  * Failover: an auth failure, a not-found, a connect timeout or repeated quick drops put the profile into a
//    cooldown (auth failures escalate, so a wrong password is not retried every minute) and the next visible
//    profile is tried.
//  * Return: while on a profile that is not the best configured one, rescan periodically; switch up only to a
//    profile that has been seen in every scan for the whole hold-down time.
//  * Nothing reachable: back off (doubling, capped), then scan again.
#include <stdint.h>
#include "WifiProfiles.h"

enum class WifiState : uint8_t { Off, Idle, Scanning, Connecting, Connected, Backoff };
enum class WifiFail : uint8_t { None, NotFound, AuthFailed, Disconnects, Timeout, Other };
// Why the link went away, as far as the platform tells.
enum class WifiDrop : uint8_t { NotFound, AuthFailed, Other };

struct WifiTiming {
  uint32_t scan_timeout_ms = 20000;       // a scan that never reports back counts as one that found nothing
  uint32_t connect_timeout_ms = 20000;    // join + DHCP
  uint32_t rescan_ms = 60000;             // between upgrade scans while on a lower-priority profile
  uint32_t hold_down_ms = 120000;         // a better profile must be seen this long before switching up
  uint32_t backoff_min_ms = 5000;         // nothing reachable: wait this long, doubling ...
  uint32_t backoff_max_ms = 300000;       // ... up to this
  uint32_t fail_cooldown_ms = 15000;      // a profile that timed out is skipped this long
  uint32_t auth_cooldown_ms = 120000;     // wrong password: skipped this long, doubling per consecutive failure ...
  uint32_t auth_cooldown_max_ms = 3600000;  // ... up to this
  uint32_t stable_ms = 30000;             // a link that lasted this long was a good one
  uint8_t max_quick_drops = 3;            // this many drops before stable_ms put the profile into cooldown
  uint32_t drop_cooldown_ms = 120000;
};

struct WifiAction {
  enum Kind : uint8_t { None, Scan, Connect, Disconnect } kind = None;
  uint8_t profile = 0;   // for Connect
};

class WifiCore {
public:
  static constexpr uint8_t NONE = 0xFF;

  WifiCore() { }
  WifiCore(const WifiProfiles* profiles, const WifiTiming& timing = WifiTiming());
  void configure(const WifiProfiles* profiles, const WifiTiming& timing = WifiTiming());

  // ---- lifecycle
  void begin(uint32_t now);                 // start: scan, then join
  void stop();                              // Off; the caller drops the link
  // A profile was edited. `credentials`: SSID or password (not only the priority). Re-evaluates without a reboot.
  void profileChanged(uint32_t now, uint8_t index, bool credentials);

  // ---- the next thing the platform layer must do (call every loop; None when nothing)
  WifiAction poll(uint32_t now);

  // ---- observations from the platform layer
  void scanObserve(const char* ssid, int8_t rssi);   // one scan result (any number of them, between poll()=Scan and scanDone)
  void scanDone(uint32_t now);
  void onConnected(uint32_t now);
  void onDisconnected(uint32_t now, WifiDrop reason);

  // ---- status
  WifiState state() const { return _state; }
  uint8_t current() const { return _state == WifiState::Connecting || _state == WifiState::Connected ? _cur : NONE; }
  WifiFail lastFail(uint8_t i) const { return i < WifiProfiles::COUNT ? _p[i].fail : WifiFail::None; }
  bool visible(uint8_t i) const { return i < WifiProfiles::COUNT && _p[i].vis; }
  int8_t rssiOf(uint8_t i) const { return _p[i].rssi; }
  uint32_t connectedMs(uint32_t now) const { return _state == WifiState::Connected ? now - _connected_at : 0; }
  uint32_t connectCount() const { return _connect_count; }   // joins that succeeded since begin()
  static const char* stateName(WifiState s);
  static const char* failName(WifiFail f);

private:
  struct Per {
    bool vis;             // seen in the last scan
    bool seen_now;        // seen in the scan in progress
    int8_t rssi;
    uint32_t vis_since;   // when the current run of consecutive sightings began
    bool skip_hold;       // edited by the owner: the next sighting counts as having waited out the hold-down
    bool cooling;
    uint32_t cool_until;
    WifiFail fail;
    uint8_t auth_fails;   // consecutive, reset by a successful join or an edit
    uint8_t quick_drops;
  };

  const WifiProfiles* _profiles = nullptr;
  WifiTiming _t;
  WifiState _state = WifiState::Off;
  Per _p[WifiProfiles::COUNT];
  uint8_t _cur = NONE;
  uint32_t _state_since = 0;
  uint32_t _connected_at = 0;
  uint32_t _connect_count = 0;
  bool _scan_pending = false;       // a Scan action is owed
  bool _scan_running = false;       // a scan was started and has not reported
  bool _scan_upgrade = false;       // ... while connected, looking for a better profile
  uint32_t _scan_started = 0;
  uint32_t _last_upgrade_scan = 0;
  bool _upgrade_scan_due_now = false;
  WifiAction _pending;
  uint32_t _backoff_ms = 0;
  uint32_t _backoff_until = 0;

  static bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
  bool cooling(uint8_t i, uint32_t now);
  void startCooldown(uint8_t i, uint32_t now, uint32_t ms);
  uint8_t best(uint32_t now, int min_prio_exclusive, bool require_hold_down);
  bool betterExists(uint32_t now);
  void connectTo(uint32_t now, uint8_t i);
  void chooseOrBackoff(uint32_t now);
  void enterBackoff(uint32_t now);
  void failProfile(uint32_t now, uint8_t i, WifiFail why);
  void beginScan(uint32_t now) { _state = WifiState::Scanning; _state_since = now; _scan_pending = true; }
};
