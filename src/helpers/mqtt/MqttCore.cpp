#include "MqttCore.h"
#include <string.h>

void MqttCore::configure(const MqttProfiles* profiles, const MqttTiming& timing) {
  _profiles = profiles;
  _t = timing;
  memset(_p, 0, sizeof(_p));
  memset(_c, 0, sizeof(_c));
  _state = MqttState::Off;
  _cur = _cur_cand = _force = _force_cand = NONE;
  _browsing = false;
  _pending_disconnect = false;
}

const char* MqttCore::stateName(MqttState s) {
  switch (s) {
    case MqttState::Off: return "off";
    case MqttState::NoLink: return "no-link";
    case MqttState::Idle: return "idle";
    case MqttState::Connecting: return "connecting";
    case MqttState::Connected: return "connected";
    case MqttState::Backoff: return "backoff";
  }
  return "?";
}

const char* MqttCore::failName(MqttFail f) {
  switch (f) {
    case MqttFail::None: return "none";
    case MqttFail::Dns: return "dns";
    case MqttFail::Tcp: return "tcp";
    case MqttFail::Tls: return "tls";
    case MqttFail::Pin: return "pin";
    case MqttFail::Auth: return "auth";
    case MqttFail::Timeout: return "timeout";
    case MqttFail::Dropped: return "dropped";
    case MqttFail::Lost: return "lost";
    case MqttFail::Posture: return "posture";
    case MqttFail::Plain: return "plain";
    case MqttFail::Ws: return "ws";
  }
  return "?";
}

MqttFail MqttCore::lastFail(uint8_t i) const {
  if (i >= MqttProfiles::COUNT || !_profiles) return MqttFail::None;
  if (_profiles->configured(i) && !MqttProfiles::permitted(_profiles->p[i], _posture)) return MqttFail::Posture;
  return _p[i].fail;
}

bool MqttCore::candidate(uint8_t cand, MqttCandidate& out) const {
  if (cand >= MAX_CANDS || !_c[cand].used) return false;
  out.addr = _c[cand].addr;
  out.port = _c[cand].port;
  return true;
}

// ------------------------------------------------------------------ lifecycle

void MqttCore::begin(uint32_t now) {
  if (_state != MqttState::Off) return;
  _state = _link ? MqttState::Idle : MqttState::NoLink;
  _cur = _cur_cand = _force = _force_cand = NONE;
  _next_browse = now;
  _browsing = false;
}

void MqttCore::stop() {
  _state = MqttState::Off;
  _cur = _cur_cand = _force = _force_cand = NONE;
  _browsing = false;
  _pending_disconnect = false;
  memset(_c, 0, sizeof(_c));
}

void MqttCore::setLink(uint32_t now, bool up) {
  if (up == _link) return;
  _link = up;
  if (_state == MqttState::Off) return;
  if (!up) {
    // The session went with the link: no penalty for the profile, but the platform layer must tidy up.
    if (_state == MqttState::Connected || _state == MqttState::Connecting) _pending_disconnect = true;
    _state = MqttState::NoLink;
    _cur = _cur_cand = _force = _force_cand = NONE;
    _browsing = false;
    memset(_c, 0, sizeof(_c));
  } else {
    _state = MqttState::Idle;
    _next_browse = now;
  }
}

void MqttCore::profileChanged(uint32_t now, uint8_t index, bool credentials) {
  if (index >= MqttProfiles::COUNT) return;
  Per& p = _p[index];
  p.fail = MqttFail::None;
  p.cooling = false;
  p.fails = 0;
  p.skip_hold = true;
  dropCands(index);
  _next_browse = now;
  if (_state == MqttState::Off) return;
  if ((_state == MqttState::Connected || _state == MqttState::Connecting) && _cur == index && credentials) {
    _pending_disconnect = true;
    _state = _link ? MqttState::Idle : MqttState::NoLink;
    _cur = _cur_cand = NONE;
  }
}

// ------------------------------------------------------------------ helpers

bool MqttCore::usable(uint8_t i) const {
  return _profiles->configured(i) && MqttProfiles::permitted(_profiles->p[i], _posture);
}

bool MqttCore::better(uint8_t a, uint8_t b) const {
  if (b == NONE) return true;
  uint8_t pa = _profiles->p[a].prio, pb = _profiles->p[b].prio;
  return pa > pb || (pa == pb && a < b);
}

bool MqttCore::profileCooling(uint8_t i, uint32_t now) {
  Per& p = _p[i];
  if (p.cooling && reached(now, p.cool_until)) p.cooling = false;
  return p.cooling;
}

bool MqttCore::candCooling(Cand& c, uint32_t now) {
  if (c.cooling && reached(now, c.cool_until)) c.cooling = false;
  return c.cooling;
}

uint32_t MqttCore::escalate(uint32_t base, uint32_t max, uint8_t fails) const {
  uint32_t ms = base;
  for (uint8_t k = 1; k < fails && ms < max; k++) ms *= 2;
  return ms > max ? max : ms;
}

int MqttCore::findCand(uint8_t prof, uint32_t addr, uint16_t port) const {
  for (uint8_t k = 0; k < MAX_CANDS; k++)
    if (_c[k].used && _c[k].prof == prof && _c[k].addr == addr && _c[k].port == port) return k;
  return -1;
}

int MqttCore::freeCandSlot() const {
  for (uint8_t k = 0; k < MAX_CANDS; k++) if (!_c[k].used) return k;
  // Full: make room by dropping a candidate that is not (yet) visible, never the one in use.
  for (uint8_t k = 0; k < MAX_CANDS; k++) if (!_c[k].vis && k != _cur_cand) return k;
  return -1;
}

void MqttCore::dropCands(uint8_t prof) {
  for (uint8_t k = 0; k < MAX_CANDS; k++) if (_c[k].used && _c[k].prof == prof) _c[k].used = false;
}

// What the profile offers right now: NONE, STATIC_OK, or the index of the candidate to use.
// With `upgrade` (a session is up) the hold-down applies unless the owner just edited the profile.
uint8_t MqttCore::availability(uint8_t i, uint32_t now, bool upgrade) {
  if (!usable(i)) return NONE;
  if (profileCooling(i, now)) return NONE;
  const bool hold = upgrade && !_p[i].skip_hold;
  if (hold && (uint32_t)(now - _connected_at) < _t.hold_down_ms) return NONE;
  if (_profiles->p[i].kind == MqttProfile::Static) return STATIC_OK;
  for (uint8_t k = 0; k < MAX_CANDS; k++) {
    Cand& c = _c[k];
    if (!c.used || c.prof != i || !c.vis || candCooling(c, now)) continue;
    if (hold && (uint32_t)(now - c.vis_since) < _t.hold_down_ms) continue;
    return k;
  }
  return NONE;
}

// The best available profile (strictly better than the current one when upgrading).
uint8_t MqttCore::pick(uint32_t now, bool upgrade, uint8_t* cand_out) {
  uint8_t best = NONE, best_cand = NONE;
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    if (upgrade && !better(i, _cur)) continue;
    uint8_t a = availability(i, now, upgrade);
    if (a == NONE) continue;
    if (best == NONE || better(i, best)) { best = i; best_cand = a == STATIC_OK ? NONE : a; }
  }
  *cand_out = best_cand;
  return best;
}

// Browse only while a discovery profile better than what the device has could be the better choice.
bool MqttCore::browseWanted(uint32_t now) {
  uint8_t ref = NONE;
  if (_state == MqttState::Connected || _state == MqttState::Connecting) ref = _cur;
  else {
    // Nothing up yet: the reference is the best static profile that could be used.
    for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
      if (_profiles->p[i].kind != MqttProfile::Static || !usable(i) || profileCooling(i, now)) continue;
      if (ref == NONE || better(i, ref)) ref = i;
    }
  }
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    if (_profiles->p[i].kind != MqttProfile::Discovery || !usable(i)) continue;
    if (better(i, ref)) return true;
  }
  return false;
}

void MqttCore::startBrowse(uint32_t now) {
  _browsing = true;
  _browse_started = now;
  for (uint8_t k = 0; k < MAX_CANDS; k++) _c[k].seen_now = false;
}

// ------------------------------------------------------------------ the next action

MqttAction MqttCore::poll(uint32_t now) {
  MqttAction a;
  if (_state == MqttState::Off) return a;
  if (_pending_disconnect) {
    _pending_disconnect = false;
    a.kind = MqttAction::Disconnect;
    return a;
  }
  if (_state == MqttState::NoLink) return a;

  if (_browsing && (uint32_t)(now - _browse_started) >= _t.browse_timeout_ms) {
    _browsing = false;                          // abandoned: what was known stays as it was
    _next_browse = now + _t.rescan_ms;
  }

  if (_state == MqttState::Connected) {
    if (!usable(_cur)) {
      // The posture (or an edit) no longer allows the profile in use.
      _p[_cur].fail = MqttFail::Posture;
      _state = MqttState::Idle;
      _cur = _cur_cand = NONE;
      a.kind = MqttAction::Disconnect;
      return a;
    }
    if (!_browsing && reached(now, _next_browse) && browseWanted(now)) {
      startBrowse(now);
      a.kind = MqttAction::Browse;
      return a;
    }
    uint8_t cand;
    uint8_t up = pick(now, true, &cand);
    if (up != NONE) {
      // Switching up: end this session; the next poll connects to exactly this profile.
      _force = up;
      _force_cand = cand;
      _state = MqttState::Idle;
      _cur = _cur_cand = NONE;
      a.kind = MqttAction::Disconnect;
      return a;
    }
    return a;
  }

  if (_state == MqttState::Connecting) return a;

  // Idle or Backoff
  uint8_t i = NONE, cand = NONE;
  if (_force != NONE) {
    uint8_t av = availability(_force, now, false);
    if (av != NONE) { i = _force; cand = av == STATIC_OK ? NONE : av; }
    _force = _force_cand = NONE;
  }
  if (i == NONE) {
    // Look for a discovery profile first when one could beat the static choice.
    if (browseWanted(now)) {
      if (_browsing) return a;
      if (reached(now, _next_browse)) {
        startBrowse(now);
        a.kind = MqttAction::Browse;
        return a;
      }
    }
    i = pick(now, false, &cand);
  }
  if (i == NONE) {
    bool any = false;
    for (uint8_t k = 0; k < MqttProfiles::COUNT; k++) any = any || usable(k);
    _state = any ? MqttState::Backoff : MqttState::Idle;
    return a;
  }
  _state = MqttState::Connecting;
  _cur = i;
  _cur_cand = cand;
  _p[i].skip_hold = false;
  a.kind = MqttAction::Connect;
  a.profile = i;
  a.cand = cand;
  return a;
}

// ------------------------------------------------------------------ browse results

void MqttCore::browseObserve(uint32_t addr, uint16_t port, bool secure, const char* site) {
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    const MqttProfile& q = _profiles->p[i];
    if (q.kind != MqttProfile::Discovery || !_profiles->configured(i)) continue;
    if (strcmp(q.site, site) != 0) continue;
    if (!secure) {
      // A broker that offers plain MQTT is never used for a discovery profile.
      if (_cur != i) _p[i].fail = MqttFail::Plain;
      continue;
    }
    int k = findCand(i, addr, port);
    if (k < 0) {
      k = freeCandSlot();
      if (k < 0) continue;
      memset(&_c[k], 0, sizeof(_c[k]));
      _c[k].used = true;
      _c[k].prof = i;
      _c[k].addr = addr;
      _c[k].port = port;
    }
    _c[k].seen_now = true;
  }
}

void MqttCore::browseDone(uint32_t now) {
  if (!_browsing) return;
  _browsing = false;
  _next_browse = now + _t.rescan_ms;
  for (uint8_t k = 0; k < MAX_CANDS; k++) {
    Cand& c = _c[k];
    if (!c.used) continue;
    if (c.seen_now) {
      if (!c.vis) { c.vis = true; c.vis_since = now; }
    } else {
      const bool in_use = _state == MqttState::Connected && _cur_cand == k;
      c.used = false;   // gone from the network: forget it, and its cooldown with it
      if (in_use) {
        // The responder this session rides on has left: fail over.
        _p[_cur].fail = MqttFail::Lost;
        _pending_disconnect = true;
        _state = MqttState::Idle;
        _cur = _cur_cand = NONE;
      }
    }
  }
}

// ------------------------------------------------------------------ session results

void MqttCore::onConnected(uint32_t now, bool verified) {
  if (_state != MqttState::Connecting || _cur == NONE) return;
  _state = MqttState::Connected;
  _connected_at = now;
  _verified = verified;
  Per& p = _p[_cur];
  p.fail = MqttFail::None;
  p.cooling = false;
  p.fails = 0;
  if (_cur_cand != NONE && _c[_cur_cand].used) { _c[_cur_cand].fails = 0; _c[_cur_cand].cooling = false; }
  _connect_count++;
}

// A failure of the profile, or of the candidate it was reached through.
void MqttCore::failCurrent(uint32_t now, MqttFail why, bool session) {
  Per& p = _p[_cur];
  p.fail = why;
  const bool cand = _cur_cand != NONE && _c[_cur_cand].used;
  if (why == MqttFail::Pin) {
    // The certificate is not the anchor: that one broker is wrong, not the whole profile.
    if (cand) {
      Cand& c = _c[_cur_cand];
      if (c.fails < 255) c.fails++;
      c.cooling = true;
      c.cool_until = now + escalate(_t.pin_cooldown_ms, _t.pin_cooldown_max_ms, c.fails);
    } else {
      if (p.fails < 255) p.fails++;
      p.cooling = true;
      p.cool_until = now + escalate(_t.pin_cooldown_ms, _t.pin_cooldown_max_ms, p.fails);
    }
    return;
  }
  if (why == MqttFail::Auth) {
    if (p.fails < 255) p.fails++;
    p.cooling = true;
    p.cool_until = now + escalate(_t.auth_cooldown_ms, _t.auth_cooldown_max_ms, p.fails);
    return;
  }
  if (session && (uint32_t)(now - _connected_at) >= _t.stable_ms) {
    // A good session that dropped is retried soon; one that died young counts as a failure.
    p.fails = 0;
    p.cooling = true;
    p.cool_until = now + _t.drop_retry_ms;
    return;
  }
  if (cand) {
    Cand& c = _c[_cur_cand];
    if (c.fails < 255) c.fails++;
    c.cooling = true;
    c.cool_until = now + escalate(_t.fail_cooldown_ms, _t.fail_cooldown_max_ms, c.fails);
  } else {
    if (p.fails < 255) p.fails++;
    p.cooling = true;
    p.cool_until = now + escalate(_t.fail_cooldown_ms, _t.fail_cooldown_max_ms, p.fails);
  }
}

void MqttCore::onConnectFailed(uint32_t now, MqttFail why) {
  if (_state != MqttState::Connecting || _cur == NONE) return;
  failCurrent(now, why, false);
  _state = MqttState::Idle;
  _cur = _cur_cand = NONE;
}

void MqttCore::onConnectCancelled() {
  if (_state != MqttState::Connecting || _cur == NONE) return;
  _state = MqttState::Idle;
  _cur = _cur_cand = NONE;
}

void MqttCore::onSessionLost(uint32_t now, MqttFail why) {
  if (_state != MqttState::Connected || _cur == NONE) return;
  failCurrent(now, why, true);
  _state = MqttState::Idle;
  _cur = _cur_cand = NONE;
}
