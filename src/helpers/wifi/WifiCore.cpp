#include "WifiCore.h"
#include <string.h>

WifiCore::WifiCore(const WifiProfiles* profiles, const WifiTiming& timing) : _profiles(profiles), _t(timing) {
  memset(_p, 0, sizeof(_p));
}

void WifiCore::configure(const WifiProfiles* profiles, const WifiTiming& timing) {
  _profiles = profiles;
  _t = timing;
  memset(_p, 0, sizeof(_p));
}

const char* WifiCore::stateName(WifiState s) {
  switch (s) {
    case WifiState::Off: return "off";
    case WifiState::Idle: return "no-profile";
    case WifiState::Scanning: return "scanning";
    case WifiState::Connecting: return "connecting";
    case WifiState::Connected: return "connected";
    case WifiState::Backoff: return "backoff";
  }
  return "?";
}

const char* WifiCore::failName(WifiFail f) {
  switch (f) {
    case WifiFail::None: return "none";
    case WifiFail::NotFound: return "not-found";
    case WifiFail::AuthFailed: return "auth";
    case WifiFail::Disconnects: return "drops";
    case WifiFail::Timeout: return "timeout";
    case WifiFail::Other: return "other";
  }
  return "?";
}

void WifiCore::begin(uint32_t now) {
  memset(_p, 0, sizeof(_p));
  _cur = NONE;
  _scan_running = _scan_upgrade = _scan_pending = false;
  _pending = WifiAction();
  _backoff_ms = 0;
  _connect_count = 0;
  if (_profiles->configuredCount() == 0) {
    _state = WifiState::Idle;
    _state_since = now;
    return;
  }
  beginScan(now);
}

void WifiCore::stop() {
  _state = WifiState::Off;
  _cur = NONE;
  _scan_running = _scan_upgrade = _scan_pending = false;
  _pending = WifiAction();
}

void WifiCore::profileChanged(uint32_t now, uint8_t index, bool credentials) {
  if (_state == WifiState::Off || index >= WifiProfiles::COUNT) return;
  // An edit is the owner fixing something: forget what was held against the profile.
  Per& p = _p[index];
  p.cooling = false;
  p.auth_fails = 0;
  p.quick_drops = 0;
  p.fail = WifiFail::None;
  p.skip_hold = true;   // the owner just fixed it: no reason to make them wait for the hold-down as well
  if (credentials) { p.vis = false; p.vis_since = 0; }
  bool on_it = (_state == WifiState::Connecting || _state == WifiState::Connected) && _cur == index;
  if (_profiles->configuredCount() == 0) {
    if (on_it || _state == WifiState::Connected) _pending.kind = WifiAction::Disconnect;
    _state = WifiState::Idle;
    _cur = NONE;
    _scan_pending = _scan_running = _scan_upgrade = false;
    return;
  }
  if (on_it && credentials) {
    // The link in use was edited: drop it and start over.
    _pending.kind = WifiAction::Disconnect;
    _cur = NONE;
    _backoff_ms = 0;
    beginScan(now);
  } else if (_state == WifiState::Connected) {
    _upgrade_scan_due_now = true;   // look for a better profile at once instead of at the next period
  } else if (_state == WifiState::Idle || _state == WifiState::Backoff) {
    _backoff_ms = 0;
    beginScan(now);
  }
}

bool WifiCore::cooling(uint8_t i, uint32_t now) {
  Per& p = _p[i];
  if (p.cooling && reached(now, p.cool_until)) p.cooling = false;
  return p.cooling;
}

void WifiCore::startCooldown(uint8_t i, uint32_t now, uint32_t ms) {
  _p[i].cooling = true;
  _p[i].cool_until = now + ms;
}

// Highest-priority profile that is configured, seen in the last scan and not cooling down, with a priority above
// `min_prio_exclusive` (-1 = any). With `require_hold_down` it must also have been seen for the hold-down time.
uint8_t WifiCore::best(uint32_t now, int min_prio_exclusive, bool require_hold_down) {
  uint8_t pick = NONE;
  for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) {
    if (!_profiles->configured(i) || !_p[i].vis || cooling(i, now)) continue;
    if ((int)_profiles->priority(i) <= min_prio_exclusive) continue;
    if (require_hold_down && (uint32_t)(now - _p[i].vis_since) < _t.hold_down_ms) continue;
    if (pick == NONE || _profiles->priority(i) > _profiles->priority(pick)) pick = i;   // strict: ties keep the lower number
  }
  return pick;
}

// Is there any configured profile above the current one that could still be worth a scan?
bool WifiCore::betterExists(uint32_t now) {
  for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) {
    if (i != _cur && _profiles->configured(i) && _profiles->priority(i) > _profiles->priority(_cur) && !cooling(i, now))
      return true;
  }
  return false;
}

void WifiCore::connectTo(uint32_t now, uint8_t i) {
  _cur = i;
  _state = WifiState::Connecting;
  _state_since = now;
  _pending.kind = WifiAction::Connect;
  _pending.profile = i;
}

void WifiCore::enterBackoff(uint32_t now) {
  _backoff_ms = _backoff_ms == 0 ? _t.backoff_min_ms : (_backoff_ms * 2 > _t.backoff_max_ms ? _t.backoff_max_ms : _backoff_ms * 2);
  _state = WifiState::Backoff;
  _state_since = now;
  _backoff_until = now + _backoff_ms;
  _cur = NONE;
  _pending.kind = WifiAction::Disconnect;   // stop any join still in progress
}

void WifiCore::chooseOrBackoff(uint32_t now) {
  uint8_t pick = best(now, -1, false);
  if (pick == NONE) enterBackoff(now);
  else connectTo(now, pick);
}

void WifiCore::failProfile(uint32_t now, uint8_t i, WifiFail why) {
  Per& p = _p[i];
  p.fail = why;
  switch (why) {
    case WifiFail::AuthFailed: {
      uint32_t ms = _t.auth_cooldown_ms;
      for (uint8_t k = 0; k < p.auth_fails && ms < _t.auth_cooldown_max_ms; k++) ms *= 2;
      if (ms > _t.auth_cooldown_max_ms) ms = _t.auth_cooldown_max_ms;
      if (p.auth_fails < 255) p.auth_fails++;
      startCooldown(i, now, ms);
      break;
    }
    case WifiFail::NotFound:
      p.vis = false;   // it is not there; the next scan decides whether it is back
      break;
    case WifiFail::Disconnects:
      startCooldown(i, now, _t.drop_cooldown_ms);
      p.quick_drops = 0;
      break;
    default:
      startCooldown(i, now, _t.fail_cooldown_ms);
      break;
  }
}

WifiAction WifiCore::poll(uint32_t now) {
  if (_pending.kind != WifiAction::None) {
    WifiAction a = _pending;
    _pending = WifiAction();
    return a;
  }
  WifiAction a;
  switch (_state) {
    case WifiState::Off:
    case WifiState::Idle:
      break;
    case WifiState::Scanning:
      if (_scan_pending) {
        _scan_pending = false;
        _scan_running = true;
        _scan_started = now;
        for (auto& p : _p) p.seen_now = false;
        a.kind = WifiAction::Scan;
      } else if (_scan_running && (uint32_t)(now - _scan_started) >= _t.scan_timeout_ms) {
        scanDone(now);   // a scan that never answers found nothing new
        return poll(now);
      }
      break;
    case WifiState::Backoff:
      if (reached(now, _backoff_until)) beginScan(now);
      break;
    case WifiState::Connecting:
      if ((uint32_t)(now - _state_since) >= _t.connect_timeout_ms) {
        failProfile(now, _cur, WifiFail::Timeout);
        a.kind = WifiAction::Disconnect;
        chooseOrBackoff(now);
        return a;
      }
      break;
    case WifiState::Connected:
      if (_scan_running) {
        if ((uint32_t)(now - _scan_started) >= _t.scan_timeout_ms) scanDone(now);
      } else if ((_upgrade_scan_due_now || (uint32_t)(now - _last_upgrade_scan) >= _t.rescan_ms) && betterExists(now)) {
        _upgrade_scan_due_now = false;
        _last_upgrade_scan = now;
        _scan_running = true;
        _scan_upgrade = true;
        _scan_started = now;
        for (auto& p : _p) p.seen_now = false;
        a.kind = WifiAction::Scan;
      } else if (_upgrade_scan_due_now || (uint32_t)(now - _last_upgrade_scan) >= _t.rescan_ms) {
        _upgrade_scan_due_now = false;
        _last_upgrade_scan = now;   // nothing better to look for; check again next period
      }
      break;
  }
  return a;
}

void WifiCore::scanObserve(const char* ssid, int8_t rssi) {
  if (!_scan_running) return;
  for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) {
    if (_profiles->configured(i) && strcmp(_profiles->ssid(i), ssid) == 0) {
      if (!_p[i].seen_now || rssi > _p[i].rssi) _p[i].rssi = rssi;   // several access points: the strongest
      _p[i].seen_now = true;
    }
  }
}

void WifiCore::scanDone(uint32_t now) {
  if (!_scan_running) return;
  _scan_running = false;
  for (auto& p : _p) {
    if (p.seen_now) {
      if (!p.vis) p.vis_since = now;   // a new run of sightings begins
      if (p.skip_hold) { p.vis_since = now - _t.hold_down_ms; p.skip_hold = false; }
      p.vis = true;
    } else {
      p.vis = false;                   // a miss resets the hold-down clock
    }
  }
  if (_scan_upgrade) {
    _scan_upgrade = false;
    if (_state != WifiState::Connected) return;
    uint8_t pick = best(now, _profiles->priority(_cur), true);
    if (pick != NONE) connectTo(now, pick);   // the adapter drops the old link as part of joining
    return;
  }
  if (_state == WifiState::Scanning) chooseOrBackoff(now);
}

void WifiCore::onConnected(uint32_t now) {
  if (_state != WifiState::Connecting) return;
  _state = WifiState::Connected;
  _state_since = _connected_at = now;
  _last_upgrade_scan = now;
  _upgrade_scan_due_now = false;
  _backoff_ms = 0;
  _connect_count++;
  _p[_cur].auth_fails = 0;
}

void WifiCore::onDisconnected(uint32_t now, WifiDrop reason) {
  if (_state == WifiState::Connecting) {
    failProfile(now, _cur, reason == WifiDrop::AuthFailed ? WifiFail::AuthFailed
                           : reason == WifiDrop::NotFound ? WifiFail::NotFound : WifiFail::Other);
    chooseOrBackoff(now);
  } else if (_state == WifiState::Connected) {
    uint8_t was = _cur;
    bool quick = (uint32_t)(now - _connected_at) < _t.stable_ms;
    if (!quick) _p[was].quick_drops = 0;
    else if (++_p[was].quick_drops >= _t.max_quick_drops) failProfile(now, was, WifiFail::Disconnects);
    _scan_running = _scan_upgrade = false;
    _cur = NONE;
    beginScan(now);   // what is visible has changed: look again before choosing
  }
}
