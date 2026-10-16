#include "MqttProfiles.h"
#include "MqttCore.h"
#include <stdio.h>

static bool parseIndex(const char*& p, uint8_t& idx) {
  if (*p < '0' || *p > '9') return false;
  idx = (uint8_t)(*p - '0');
  p++;
  if (*p != 0 && *p != ' ') return false;   // one digit only
  return true;
}

static bool nameChars(const char* s) {
  for (; *s; s++) {
    char c = *s;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' || c == '_'))
      return false;
  }
  return true;
}

static bool setText(char* dst, size_t cap, const char* v) {
  size_t n = strlen(v);
  if (n >= cap) return false;
  memcpy(dst, v, n + 1);
  return true;
}

// Accepts 64 hex digits, upper or lower case, optionally separated by colons; stores lower case without colons.
static bool setAnchor(char* dst, const char* v) {
  char out[MqttProfile::ANCHOR_BUF];
  size_t n = 0;
  for (; *v; v++) {
    char c = *v;
    if (c == ':') continue;
    if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) || n >= 64) return false;
    out[n++] = c;
  }
  if (n != 64) return false;
  out[n] = 0;
  memcpy(dst, out, n + 1);
  return true;
}

static int formatProfile(const MqttProfiles& pr, const MqttCore* core, uint8_t i, char* out, size_t cap) {
  const MqttProfile& q = pr.p[i];
  const char* last = MqttCore::failName(core ? core->lastFail(i) : MqttFail::None);
  const char* user = q.user[0] ? "set" : "unset";
  const char* pwd = q.pwd[0] ? "set" : "unset";
  const char* anchor = q.hasAnchor() ? "set" : "unset";
  if (q.kind == MqttProfile::Discovery) {
    return snprintf(out, cap, "%u: discovery site=%s anchor=%s prio=%u user=%s pwd=%s last=%s", (unsigned)i, q.site,
                    anchor, (unsigned)q.prio, user, pwd, last);
  }
  return snprintf(out, cap, "%u: static %s:%u tls=%s anchor=%s prio=%u user=%s pwd=%s last=%s", (unsigned)i, q.host,
                  (unsigned)q.effectivePort(), q.tls ? "on" : "off", anchor, (unsigned)q.prio, user, pwd, last);
}

bool mqttProfileCommand(MqttProfiles& pr, const MqttCore* core, uint8_t posture, const char* command, char* reply,
                        size_t cap, MqttProfileChange& change) {
  change = MqttProfileChange();
  const char* p;

  if (memcmp(command, "set mqtt.profile ", 17) == 0) {
    p = command + 17;
    uint8_t idx;
    if (!parseIndex(p, idx) || idx >= MqttProfiles::COUNT) {
      snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(MqttProfiles::COUNT - 1));
      return true;
    }
    while (*p == ' ') p++;
    MqttProfile& q = pr.p[idx];
    auto done = [&](bool credentials, const char* msg) {
      change.changed = true; change.index = idx; change.credentials = credentials;
      snprintf(reply, cap, "> profile %u %s", (unsigned)idx, msg);
    };
    if (strcmp(p, "clear") == 0) {
      pr.clear(idx);
      done(true, "cleared");
    } else if (memcmp(p, "type ", 5) == 0) {
      const char* v = p + 5;
      if (strcmp(v, "static") == 0) { q.kind = MqttProfile::Static; done(true, "is a static profile"); }
      else if (strcmp(v, "discovery") == 0) { q.kind = MqttProfile::Discovery; done(true, "is a discovery profile"); }
      else snprintf(reply, cap, "ERR: type is static or discovery");
    } else if (memcmp(p, "host ", 5) == 0) {
      const char* v = p + 5;
      if (!*v || !nameChars(v) || strlen(v) >= MqttProfile::HOST_BUF) snprintf(reply, cap, "ERR: host is a name or IPv4 address, 1-63 characters");
      else { setText(q.host, sizeof(q.host), v); done(true, "host updated"); }
    } else if (memcmp(p, "port ", 5) == 0) {
      const char* v = p + 5;
      unsigned n = 0;
      bool ok = *v != 0;
      for (const char* c = v; *c; c++) {
        if (*c < '0' || *c > '9' || (n = n * 10 + (*c - '0')) > 65535) { ok = false; break; }
      }
      if (!ok) snprintf(reply, cap, "ERR: port is 0-65535 (0 = 8883 with TLS, 1883 without)");
      else { q.port = (uint16_t)n; done(true, "port updated"); }
    } else if (memcmp(p, "tls ", 4) == 0) {
      const char* v = p + 4;
      if (q.kind == MqttProfile::Discovery) snprintf(reply, cap, "ERR: a discovery profile is always TLS");
      else if (strcmp(v, "on") == 0) { q.tls = 1; done(true, "uses TLS"); }
      else if (strcmp(v, "off") == 0) {
        if (Posture::isHardened(posture))
          snprintf(reply, cap, "ERR: plain MQTT is refused in the Hardened posture");
        else { q.tls = 0; done(true, "is plain MQTT (unverified)"); }
      } else snprintf(reply, cap, "ERR: tls is on or off");
    } else if (memcmp(p, "anchor ", 7) == 0) {
      const char* v = p + 7;
      if (strcmp(v, "clear") == 0) { q.anchor[0] = 0; done(true, "anchor cleared"); }
      else if (!setAnchor(q.anchor, v)) snprintf(reply, cap, "ERR: anchor is the SHA-256 fingerprint of the broker certificate, 64 hex digits (or clear)");
      else done(true, "anchor updated");
    } else if (memcmp(p, "site ", 5) == 0) {
      const char* v = p + 5;
      if (!*v || !nameChars(v) || strlen(v) >= MqttProfile::SITE_BUF) snprintf(reply, cap, "ERR: site ID is 1-23 characters: letters, digits, . - _");
      else { setText(q.site, sizeof(q.site), v); done(true, "site ID updated"); }
    } else if (strcmp(p, "user") == 0 || memcmp(p, "user ", 5) == 0) {
      const char* v = p[4] ? p + 5 : "";
      if (strlen(v) >= MqttProfile::USER_BUF) snprintf(reply, cap, "ERR: user name is at most 31 characters");
      else { setText(q.user, sizeof(q.user), v); done(true, *v ? "user updated" : "user cleared"); }
    } else if (strcmp(p, "pwd") == 0 || memcmp(p, "pwd ", 4) == 0) {
      const char* v = p[3] ? p + 4 : "";
      if (strlen(v) >= MqttProfile::PWD_BUF) snprintf(reply, cap, "ERR: password is at most 63 characters");
      else { setText(q.pwd, sizeof(q.pwd), v); done(true, *v ? "pwd updated" : "pwd cleared"); }
    } else if (memcmp(p, "priority ", 9) == 0) {
      const char* v = p + 9;
      unsigned n = 0;
      bool ok = *v != 0;
      for (const char* c = v; *c; c++) {
        if (*c < '0' || *c > '9' || (n = n * 10 + (*c - '0')) > 255) { ok = false; break; }
      }
      if (!ok) snprintf(reply, cap, "ERR: priority is 0-255 (higher is tried first)");
      else {
        q.prio = (uint8_t)n;
        change.changed = true; change.index = idx; change.credentials = false;
        snprintf(reply, cap, "> profile %u priority is now %u", (unsigned)idx, n);
      }
    } else {
      snprintf(reply, cap, "ERR: set mqtt.profile <n> type|host|port|tls|anchor|site|user|pwd|priority <value> | clear");
    }
    return true;
  }

  if (memcmp(command, "get mqtt.profile ", 17) == 0) {
    p = command + 17;
    uint8_t idx;
    if (!parseIndex(p, idx) || idx >= MqttProfiles::COUNT) {
      snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(MqttProfiles::COUNT - 1));
      return true;
    }
    if (pr.p[idx].kind == MqttProfile::Unset) { snprintf(reply, cap, "> %u: (not set)", (unsigned)idx); return true; }
    int n = snprintf(reply, cap, "> ");
    formatProfile(pr, core, idx, reply + n, cap - n);
    return true;
  }

  if (strcmp(command, "get mqtt.profiles") == 0 || memcmp(command, "get mqtt.profiles ", 18) == 0) {
    uint8_t from = 0;
    if (command[17] == ' ') {
      p = command + 18;
      if (!parseIndex(p, from) || from >= MqttProfiles::COUNT) {
        snprintf(reply, cap, "ERR: profile number 0-%u", (unsigned)(MqttProfiles::COUNT - 1));
        return true;
      }
    }
    size_t len = snprintf(reply, cap, ">");
    bool any = false;
    for (uint8_t i = from; i < MqttProfiles::COUNT; i++) {
      if (pr.p[i].kind == MqttProfile::Unset) continue;
      char line[160];
      int ll = formatProfile(pr, core, i, line, sizeof(line));
      if (ll < 0 || len + 1 + (size_t)ll + 16 >= cap) {   // keep room for the continuation marker
        if (!any) { snprintf(reply + len, cap - len, " (reply too small)"); return true; }
        snprintf(reply + len, cap - len, "\n... next:%u", (unsigned)i);
        return true;
      }
      len += snprintf(reply + len, cap - len, "\n%s", line);
      any = true;
    }
    if (!any) snprintf(reply, cap, "> (no profiles)");
    return true;
  }
  return false;
}
