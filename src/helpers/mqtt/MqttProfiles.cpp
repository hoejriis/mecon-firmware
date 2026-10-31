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

// A WebSocket path: '/' and then the characters RFC 3986 allows in a path (no query, no fragment), 63 at most.
static bool pathOk(const char* s) {
  const size_t n = strlen(s);
  if (n == 0 || n >= MqttProfile::PATH_BUF || s[0] != '/') return false;
  for (size_t i = 0; i < n; i++) {
    const char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
    if (!strchr("/-._~!$&'()*+,;=:@%", c)) return false;
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

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

int mqttCaDecode(const char* b64, uint8_t* der, size_t cap) {
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0, pad = 0;
  for (const char* c = b64; *c; c++) {
    if (*c == '=') { pad++; continue; }
    int v = b64val(*c);
    if (v < 0 || pad) return -1;    // a stray character, or data after the padding
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= cap) return -1;
      der[n++] = (uint8_t)(acc >> bits);
      acc &= (1u << bits) - 1;
    }
  }
  if (pad > 2 || (bits != 0 && acc != 0)) return -1;
  // One DER SEQUENCE, and the length it declares is exactly what was decoded: a truncated paste is refused here.
  if (n < 64 || der[0] != 0x30) return -1;
  size_t hdr, len;
  if (der[1] < 0x80) { hdr = 2; len = der[1]; }
  else if (der[1] == 0x81) { hdr = 3; len = der[2]; }
  else if (der[1] == 0x82) { hdr = 4; len = ((size_t)der[2] << 8) | der[3]; }
  else return -1;
  if (hdr + len != n) return -1;
  return (int)n;
}

bool mqttHostIsLiteral(const char* host) {
  if (!*host) return false;
  for (; *host; host++) if (!((*host >= '0' && *host <= '9') || *host == '.')) return false;
  return true;
}

MqttProfiles::~MqttProfiles() {
  dropStage();
  for (uint8_t i = 0; i < COUNT; i++) free(p[i].ca);
}

void MqttProfiles::dropStage() {
  free(stage_);
  stage_ = nullptr;
  stage_len_ = 0;
}

bool MqttProfiles::setCa(uint8_t i, const char* b64) {
  if (i >= COUNT || strlen(b64) > MqttProfile::CA_B64_MAX) return false;
  uint8_t der[MqttProfile::CA_B64_MAX / 4 * 3 + 3];
  if (mqttCaDecode(b64, der, sizeof(der)) < 0) return false;
  char* copy = (char*)malloc(strlen(b64) + 1);
  if (!copy) return false;
  strcpy(copy, b64);
  free(p[i].ca);
  p[i].ca = copy;
  p[i].anchor[0] = 0;    // a profile has one anchor: the CA replaces a leaf fingerprint
  return true;
}

void MqttProfiles::clearCa(uint8_t i) {
  if (i >= COUNT) return;
  free(p[i].ca);
  p[i].ca = nullptr;
}

bool MqttProfiles::anchorUsable(const MqttProfile& q) {
  if (q.hasCa()) {
    uint8_t der[MqttProfile::CA_B64_MAX / 4 * 3 + 3];
    return mqttCaDecode(q.ca, der, sizeof(der)) > 0;
  }
  return q.anchor[0] == 0 || validAnchor(q.anchor);
}

// A damaged CA must not disappear: a profile with no anchor connects unverified. A marker that is not base64 stays in
// its place, so the profile still has a CA, the CA does not decode, and the profile fails closed.
static void poisonCa(MqttProfile& q) {
  free(q.ca);
  q.ca = (char*)malloc(2);
  if (q.ca) { q.ca[0] = '!'; q.ca[1] = 0; }
}

bool MqttProfiles::loadCaChunk(uint8_t i, uint8_t n, const char* chunk) {
  if (i >= COUNT || n >= MqttProfile::CA_CHUNKS) return false;
  if (n == 0) clearCa(i);   // a file loaded over a profile that already had a CA replaces it
  size_t have = p[i].ca ? strlen(p[i].ca) : 0;
  size_t cl = strlen(chunk);
  if (have != (size_t)n * MqttProfile::CA_CHUNK || cl == 0 || cl > MqttProfile::CA_CHUNK) {
    poisonCa(p[i]);    // a gap or an out-of-order chunk: the CA is damaged, and a damaged anchor is not "no anchor"
    return false;
  }
  char* grown = (char*)realloc(p[i].ca, have + cl + 1);
  if (!grown) { poisonCa(p[i]); return false; }
  memcpy(grown + have, chunk, cl + 1);
  p[i].ca = grown;
  return true;
}

bool MqttProfiles::caChunk(uint8_t i, uint8_t n, char* out) const {
  if (i >= COUNT || !p[i].ca) return false;
  size_t len = strlen(p[i].ca), off = (size_t)n * MqttProfile::CA_CHUNK;
  if (off >= len) return false;
  size_t cl = len - off < MqttProfile::CA_CHUNK ? len - off : MqttProfile::CA_CHUNK;
  memcpy(out, p[i].ca + off, cl);
  out[cl] = 0;
  return true;
}

static int formatProfile(const MqttProfiles& pr, const MqttCore* core, uint8_t i, char* out, size_t cap) {
  const MqttProfile& q = pr.p[i];
  const char* last = MqttCore::failName(core ? core->lastFail(i) : MqttFail::None);
  const char* user = q.user[0] ? "set" : "unset";
  const char* pwd = q.pwd[0] ? "set" : "unset";
  const char* anchor = q.hasCa() ? "ca" : (q.anchor[0] ? "fingerprint" : "unset");
  const char* manage = q.manage ? " manage=on" : "";   // shown only when on, so the listing of a profile that does not manage is as it was
  if (q.kind == MqttProfile::Discovery) {
    return snprintf(out, cap, "%u: discovery site=%s anchor=%s prio=%u user=%s pwd=%s%s last=%s", (unsigned)i, q.site,
                    anchor, (unsigned)q.prio, user, pwd, manage, last);
  }
  // The transport is shown only for a WebSocket profile, so a TCP profile's listing is as it was. The path is not in the
  // line (`get mqtt.profile <n> path` reads it): with a 63-character host and path the line would outgrow the 150-byte
  // reply and lose the fields after it.
  const char* ws = q.ws ? " transport=ws" : "";
  return snprintf(out, cap, "%u: static %s:%u tls=%s%s anchor=%s prio=%u user=%s pwd=%s%s last=%s", (unsigned)i, q.host,
                  (unsigned)q.effectivePort(), q.tls ? "on" : "off", ws, anchor, (unsigned)q.prio, user, pwd, manage, last);
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
      if (!ok) snprintf(reply, cap, "ERR: port is 0-65535 (0 = 8883 with TLS, 1883 without; 443 and 80 over WebSocket)");
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
    } else if (memcmp(p, "transport ", 10) == 0) {
      const char* v = p + 10;
      if (q.kind == MqttProfile::Discovery) snprintf(reply, cap, "ERR: a discovery profile is always MQTT over TLS");
      else if (strcmp(v, "tcp") == 0) { q.ws = 0; done(true, "speaks MQTT over the connection"); }
      else if (strcmp(v, "ws") == 0) { q.ws = 1; done(true, "speaks MQTT over WebSocket"); }
      else snprintf(reply, cap, "ERR: transport is tcp or ws");
    } else if (memcmp(p, "path ", 5) == 0) {
      const char* v = p + 5;
      if (!pathOk(v)) snprintf(reply, cap, "ERR: path starts with / and is at most 63 URL path characters: letters, digits and -._~!$&'()*+,;=:@%/");
      else { setText(q.path, sizeof(q.path), strcmp(v, "/mqtt") == 0 ? "" : v); done(true, "path updated"); }
    } else if (memcmp(p, "anchor ", 7) == 0) {
      const char* v = p + 7;
      if (strcmp(v, "clear") == 0) { q.anchor[0] = 0; pr.clearCa(idx); done(true, "anchor cleared"); }
      else if (!setAnchor(q.anchor, v)) snprintf(reply, cap, "ERR: anchor is the SHA-256 fingerprint of the broker certificate, 64 hex digits (or clear); a CA certificate is set with ca begin|add|end");
      else { pr.clearCa(idx); done(true, "anchor updated"); }
    } else if (memcmp(p, "ca ", 3) == 0) {
      const char* v = p + 3;
      if (strcmp(v, "begin") == 0) {
        pr.dropStage();
        pr.stage_ = (char*)malloc(MqttProfile::CA_B64_MAX + 1);
        if (!pr.stage_) { snprintf(reply, cap, "ERR: out of memory"); return true; }
        pr.stage_[0] = 0;
        pr.stage_len_ = 0;
        pr.stage_idx_ = idx;
        snprintf(reply, cap, "> profile %u ca: send ca add <base64>, then ca end", (unsigned)idx);
      } else if (memcmp(v, "add ", 4) == 0) {
        const char* chunk = v + 4;
        if (!pr.stage_ || pr.stage_idx_ != idx) { snprintf(reply, cap, "ERR: ca begin first"); return true; }
        if (memcmp(chunk, "-----", 5) == 0) {   // a PEM armour line pasted as it is
          snprintf(reply, cap, "> ca %u chars", (unsigned)pr.stage_len_);
          return true;
        }
        size_t n = strlen(chunk);
        bool ok = n > 0;
        for (size_t k = 0; k < n && ok; k++) ok = b64val(chunk[k]) >= 0 || chunk[k] == '=';
        if (!ok) { snprintf(reply, cap, "ERR: ca add takes base64 text only"); return true; }
        if (pr.stage_len_ + n > MqttProfile::CA_B64_MAX) {
          pr.dropStage();
          snprintf(reply, cap, "ERR: ca is longer than %u base64 characters; send ca begin to start again", (unsigned)MqttProfile::CA_B64_MAX);
          return true;
        }
        memcpy(pr.stage_ + pr.stage_len_, chunk, n + 1);
        pr.stage_len_ = (uint16_t)(pr.stage_len_ + n);
        snprintf(reply, cap, "> ca %u chars", (unsigned)pr.stage_len_);
      } else if (strcmp(v, "end") == 0) {
        if (!pr.stage_ || pr.stage_idx_ != idx) { snprintf(reply, cap, "ERR: ca begin first"); return true; }
        bool ok = pr.setCa(idx, pr.stage_);
        pr.dropStage();
        if (ok) done(true, "anchor is a CA certificate");
        else snprintf(reply, cap, "ERR: ca is not one complete base64 DER certificate; the profile is unchanged");
      } else if (strcmp(v, "clear") == 0) {
        pr.clearCa(idx);
        if (pr.stage_ && pr.stage_idx_ == idx) pr.dropStage();
        done(true, "CA certificate cleared");
      } else snprintf(reply, cap, "ERR: ca is begin, add <base64>, end or clear");
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
    } else if (memcmp(p, "manage ", 7) == 0) {
      const char* v = p + 7;
      if (strcmp(v, "on") == 0) { q.manage = 1; done(false, "may be managed over MQTT"); }
      else if (strcmp(v, "off") == 0) { q.manage = 0; done(false, "is read-only over MQTT in Hardened"); }
      else snprintf(reply, cap, "ERR: manage is on or off");
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
      snprintf(reply, cap, "ERR: set mqtt.profile <n> type|host|port|tls|transport|path|anchor|ca|site|user|pwd|priority|manage <value> | clear");
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
    if (strcmp(p, " path") == 0) { snprintf(reply, cap, "> %s", pr.p[idx].wsPath()); return true; }
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
