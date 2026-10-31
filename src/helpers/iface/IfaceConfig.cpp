#include "IfaceConfig.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Iface {

// The keys of the interface's settings profile config/1.0. Indexes are 0-based, as in the CLI.
const KeyDef KEYS[] = {
  {"name", K_STRING, 1, 31, nullptr, KF_WRITABLE, 0, "get name", F_WHOLE, "set name %s"},
  {"tx", K_INT, -9, 30, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get tx", F_WHOLE, "set tx %s"},
  {"radio", K_STRING, 7, 40, nullptr, KF_WRITABLE | KF_CONFIRM | KF_REBOOT, 0, "get radio", F_WHOLE, "set radio %s", "freq,bw,sf,cr"},
  {"connectivity.mode", K_ENUM, 0, 0, "ble|wifi", KF_WRITABLE | KF_CONFIRM | KF_RESTARTS, 0, "get connectivity.mode", F_WHOLE,
   "set connectivity.mode %s"},
  {"wifi.enabled", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_CONFIRM | KF_REBOOT, 0, "get wifi.enabled", F_WHOLE, "set wifi.enabled %s"},
  {"wifi.profile.{n}.ssid", K_STRING, 0, 32, nullptr, KF_WRITABLE | KF_CONFIRM, 9, "get wifi.profile %u", F_WIFI_SSID,
   "set wifi.profile %u ssid %s"},
  {"wifi.profile.{n}.pwd", K_STRING, 0, 63, nullptr, KF_WRITABLE | KF_CONFIRM | KF_WRITE_ONLY, 9, "get wifi.profile %u", F_WIFI_PWD,
   "set wifi.profile %u pwd %s"},
  {"wifi.profile.{n}.priority", K_INT, 0, 255, nullptr, KF_WRITABLE | KF_CONFIRM, 9, "get wifi.profile %u", F_WIFI_PRIO,
   "set wifi.profile %u priority %s"},
  {"mqtt.enabled", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get mqtt.enabled", F_WHOLE, "set mqtt.enabled %s"},
  {"mqtt.profile.{n}.type", K_ENUM, 0, 0, "static|discovery", KF_WRITABLE | KF_CONFIRM, 5, "get mqtt.profile %u", F_MQTT_TYPE,
   "set mqtt.profile %u type %s"},
  {"mqtt.profile.{n}.host", K_STRING, 1, 63, nullptr, KF_WRITABLE | KF_CONFIRM, 5, "get mqtt.profile %u", F_MQTT_HOST,
   "set mqtt.profile %u host %s"},
  {"mqtt.profile.{n}.port", K_INT, 0, 65535, nullptr, KF_WRITABLE | KF_CONFIRM, 5, "get mqtt.profile %u", F_MQTT_PORT,
   "set mqtt.profile %u port %s"},
  {"mqtt.profile.{n}.tls", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_CONFIRM | KF_ONOFF, 5, "get mqtt.profile %u", F_MQTT_TLS,
   "set mqtt.profile %u tls %s"},
  {"mqtt.profile.{n}.site", K_STRING, 1, 23, nullptr, KF_WRITABLE | KF_CONFIRM, 5, "get mqtt.profile %u", F_MQTT_SITE,
   "set mqtt.profile %u site %s"},
  {"mqtt.profile.{n}.user", K_STRING, 0, 63, nullptr, KF_WRITABLE | KF_CONFIRM | KF_WRITE_ONLY, 5, "get mqtt.profile %u", F_MQTT_USER,
   "set mqtt.profile %u user %s"},
  {"mqtt.profile.{n}.pwd", K_STRING, 0, 63, nullptr, KF_WRITABLE | KF_CONFIRM | KF_WRITE_ONLY, 5, "get mqtt.profile %u", F_MQTT_PWD,
   "set mqtt.profile %u pwd %s"},
  {"mqtt.profile.{n}.priority", K_INT, 0, 255, nullptr, KF_WRITABLE | KF_CONFIRM, 5, "get mqtt.profile %u", F_MQTT_PRIO,
   "set mqtt.profile %u priority %s"},
};
const size_t NKEYS = sizeof(KEYS) / sizeof(KEYS[0]);

// The Repeater and Room server share their settings CLI (CommonCLI, CommonRadioPrefs), so they share this table.
// advert.interval is kept in 2-minute steps: an odd value reads back one lower.
const KeyDef SERVER_KEYS[] = {
  {"name", K_STRING, 1, 31, nullptr, KF_WRITABLE, 0, "get name", F_WHOLE, "set name %s"},
  {"tx", K_INT, -9, 30, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get tx", F_WHOLE, "set tx %s"},
  {"radio", K_STRING, 7, 40, nullptr, KF_WRITABLE | KF_CONFIRM | KF_REBOOT, 0, "get radio", F_WHOLE, "set radio %s", "freq,bw,sf,cr"},
  {"lat", K_STRING, 1, 12, nullptr, KF_WRITABLE, 0, "get lat", F_WHOLE, "set lat %s", "latitude"},
  {"lon", K_STRING, 1, 12, nullptr, KF_WRITABLE, 0, "get lon", F_WHOLE, "set lon %s", "longitude"},
  {"repeat", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_ONOFF, 0, "get repeat", F_WHOLE, "set repeat %s"},
  {"advert.interval", K_INT, 0, 240, nullptr, KF_WRITABLE, 0, "get advert.interval", F_WHOLE,
   "set advert.interval %s"},
  {"flood.advert.interval", K_INT, 0, 168, nullptr, KF_WRITABLE, 0, "get flood.advert.interval", F_WHOLE,
   "set flood.advert.interval %s"},
  {"flood.max", K_INT, 0, 64, nullptr, KF_WRITABLE, 0, "get flood.max", F_WHOLE, "set flood.max %s"},
  {"loop.detect", K_ENUM, 0, 0, "off|minimal|moderate|strict", KF_WRITABLE, 0, "get loop.detect", F_WHOLE,
   "set loop.detect %s"},
  {"allow.read.only", K_BOOL, 0, 1, nullptr, KF_WRITABLE | KF_ONOFF, 0, "get allow.read.only", F_WHOLE,
   "set allow.read.only %s"},
  {"owner.info", K_STRING, 0, 119, nullptr, KF_WRITABLE, 0, "get owner.info", F_WHOLE, "set owner.info %s"},
};
const size_t N_SERVER_KEYS = sizeof(SERVER_KEYS) / sizeof(SERVER_KEYS[0]);

namespace {
// A plain decimal ("869.618", "125", "250.0") within [lo, hi]; *end gets the first character after it.
bool decimal(const char* s, const char** end, double lo, double hi) {
  const char* p = s;
  double v = 0, scale = 0;
  if (*p < '0' || *p > '9') return false;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9') return false;
    scale = 1;
    while (*p >= '0' && *p <= '9') { scale /= 10; v += (*p++ - '0') * scale; }
  }
  *end = p;
  return v >= lo && v <= hi;
}
}  // namespace

bool checkFormat(const char* format, const char* value) {
  const bool lat = strcmp(format, "latitude") == 0, lon = strcmp(format, "longitude") == 0;
  if (lat || lon) {
    const char* p = value;
    if (*p == '-') p++;
    const double lim = lat ? 90 : 180;
    return decimal(p, &p, 0, lim) && *p == 0;
  }
  if (strcmp(format, "freq,bw,sf,cr") != 0) return false;
  const char* p = value;
  const double lo[] = {100, 7, 5, 5}, hi[] = {1000, 500, 12, 8};
  for (int i = 0; i < 4; i++) {
    if (!decimal(p, &p, lo[i], hi[i])) return false;
    if (i < 3) { if (*p != ',') return false; p++; }
  }
  return *p == 0;
}

namespace {

const size_t LINE_MAX = 160;
const size_t VALUE_MAX = 64;

ConfigHost* hostOf(OpContext& c) { return static_cast<ConfigHost*>(c.user); }
const KeyDef* tableOf(const ConfigHost* h, size_t* n) {
  *n = h->keys ? h->nkeys : NKEYS;
  return h->keys ? h->keys : KEYS;
}

// "<reason>:<key>" for the result's error; the dispatcher copies it out before the next request.
char g_error[80];
const char* keyError(const char* reason, const char* key) {
  snprintf(g_error, sizeof(g_error), "%s:%s", reason, key);
  return g_error;
}

// "wifi.profile.3.ssid" against "wifi.profile.{n}.ssid": true with the index when the key fits the pattern.
bool matches(const char* pat, const char* key, size_t len, unsigned* index) {
  const char* hole = strstr(pat, "{n}");
  if (!hole) {
    *index = 0;
    return strlen(pat) == len && memcmp(pat, key, len) == 0;
  }
  const size_t pre = (size_t)(hole - pat), post = strlen(hole + 3);
  if (len < pre + post + 1 || memcmp(key, pat, pre) != 0 || memcmp(key + len - post, hole + 3, post) != 0) return false;
  const char* d = key + pre;
  const size_t dn = len - pre - post;
  if (dn > 3 || (dn > 1 && d[0] == '0')) return false;   // no leading zeros: one spelling per key
  unsigned v = 0;
  for (size_t i = 0; i < dn; i++) {
    if (d[i] < '0' || d[i] > '9') return false;
    v = v * 10 + (unsigned)(d[i] - '0');
  }
  *index = v;
  return true;
}

// The value after "<name>=" up to the next space, in reply; false when absent.
bool tokenAfter(const char* reply, const char* name, char* out, size_t cap) {
  char pat[16];
  snprintf(pat, sizeof(pat), " %s=", name);
  const char* p = strstr(reply, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t n = 0;
  while (p[n] && p[n] != ' ') n++;
  if (n >= cap) return false;
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

void copyOut(char* out, size_t cap, const char* s, size_t n) {
  if (n >= cap) n = cap - 1;
  memcpy(out, s, n);
  out[n] = 0;
}

void writeValue(JsonWriter& w, const KeyDef& k, const char* text, bool is_null) {
  if (is_null) { w.null(); return; }
  if (k.flags & KF_WRITE_ONLY) { w.str(text); return; }   // "set" / "unset"
  switch (k.type) {
    case K_INT: w.num(strtol(text, nullptr, 10)); break;
    case K_BOOL: w.boolean(strcmp(text, "1") == 0 || strcmp(text, "on") == 0); break;
    default: w.str(text); break;
  }
}

void writeDef(JsonWriter& w, const KeyDef& k) {
  static const char* const TYPES[] = {"string", "integer", "boolean", "enum"};
  w.beginObject();
  w.key("key"); w.str(k.key);
  w.key("type"); w.str(TYPES[k.type]);
  if (k.type == K_INT) { w.key("min"); w.num(k.min); w.key("max"); w.num(k.max); }
  if (k.type == K_STRING) { w.key("min_len"); w.num(k.min); w.key("max_len"); w.num(k.max); }
  if (k.format) { w.key("format"); w.str(k.format); }
  if (k.type == K_ENUM) {
    w.key("choices"); w.beginArray();
    const char* s = k.choices;
    while (*s) {
      const char* e = strchr(s, '|');
      size_t n = e ? (size_t)(e - s) : strlen(s);
      w.str(s, n);
      s += n + (e ? 1 : 0);
    }
    w.endArray();
  }
  if (k.idx_max) { w.key("index"); w.beginArray(); w.num(0); w.num(k.idx_max); w.endArray(); }
  w.key("writable"); w.boolean((k.flags & KF_WRITABLE) != 0);
  w.key("write_only"); w.boolean((k.flags & KF_WRITE_ONLY) != 0);
  w.key("reboot"); w.boolean((k.flags & KF_REBOOT) != 0);
  w.key("restarts"); w.boolean((k.flags & KF_RESTARTS) != 0);
  w.key("confirm"); w.boolean((k.flags & KF_CONFIRM) != 0);   // a caller other than the cable must ask to confirm
  w.endObject();
}

// The value of a write as the CLI line takes it, after the type and range checks. False with *why set when it fails.
bool cliValue(const JsonDoc& d, int t, const KeyDef& k, char* out, size_t cap, const char** why) {
  switch (k.type) {
    case K_INT: {
      int64_t v;
      if (!jsonGetInt(d, t, k.min, k.max, &v)) { *why = "out-of-range"; return false; }
      snprintf(out, cap, "%ld", (long)v);
      return true;
    }
    case K_BOOL: {
      bool b;
      if (!jsonGetBool(d, t, &b)) { *why = "not-a-boolean"; return false; }
      const bool onoff = (k.flags & KF_ONOFF) != 0;   // the CLI's word for this key
      snprintf(out, cap, "%s", onoff ? (b ? "on" : "off") : (b ? "1" : "0"));
      return true;
    }
    case K_ENUM: {
      if (!jsonGetString(d, t, out, cap, CS_NAME, 1)) { *why = "not-a-choice"; return false; }
      const size_t n = strlen(out);
      for (const char* s = k.choices; *s;) {
        const char* e = strchr(s, '|');
        const size_t cn = e ? (size_t)(e - s) : strlen(s);
        if (cn == n && memcmp(s, out, n) == 0) return true;
        s += cn + (e ? 1 : 0);
      }
      *why = "not-a-choice";
      return false;
    }
    default:
      if (!jsonGetString(d, t, out, cap, CS_ANY, (size_t)k.min) || strlen(out) > (size_t)k.max) {
        *why = "bad-length";
        return false;
      }
      if (strchr(out, '\n') || strchr(out, '\r')) { *why = "bad-length"; return false; }
      if (k.format && !checkFormat(k.format, out)) { *why = "bad-format"; return false; }
      return true;
  }
}

void lineFor(char* out, size_t cap, const char* fmt, unsigned idx, const char* value) {
  if (strstr(fmt, "%u")) {
    if (value) snprintf(out, cap, fmt, idx, value); else snprintf(out, cap, fmt, idx);
  } else {
    if (value) snprintf(out, cap, fmt, value); else snprintf(out, cap, "%s", fmt);
  }
}

bool cliOk(const char* reply) { return strncmp(reply, "OK", 2) == 0 || strncmp(reply, "> ", 2) == 0; }

// ---- commit-or-revert (OPERATIONS.md section 3.4)

// Every key of one profile is read from the same get line ("get wifi.profile %u").
bool sameProfile(const KeyDef& a, const KeyDef& b) { return a.idx_max && b.idx_max && strcmp(a.get_line, b.get_line) == 0; }

// A value read back from the CLI, in the form the key's set line takes.
bool setForm(const KeyDef& k, const char* text, char* out, size_t cap) {
  if (k.type == K_BOOL) {
    const bool b = strcmp(text, "1") == 0 || strcmp(text, "on") == 0;
    if (!b && strcmp(text, "0") != 0 && strcmp(text, "off") != 0) return false;
    snprintf(out, cap, "%s", (k.flags & KF_ONOFF) ? (b ? "on" : "off") : (b ? "1" : "0"));
    return true;
  }
  if (strlen(text) >= cap) return false;
  strcpy(out, text);
  return true;
}

// Whether a snapshot value is one the key's set line may take: the checks a request's value gets, short of the
// minimum length (a previous value may be empty, as an unset password is).
bool restorable(const KeyDef& k, const char* v) {
  if (!ConfigSnap::valueOk(v, strlen(v))) return false;
  switch (k.type) {
    case K_INT: {
      char* end = nullptr;
      const long n = strtol(v, &end, 10);
      return *v && end && !*end && n >= k.min && n <= k.max;
    }
    case K_BOOL:
      return (k.flags & KF_ONOFF) ? (strcmp(v, "on") == 0 || strcmp(v, "off") == 0) : (strcmp(v, "1") == 0 || strcmp(v, "0") == 0);
    case K_ENUM: {
      const size_t n = strlen(v);
      for (const char* s = k.choices; *s;) {
        const char* e = strchr(s, '|');
        const size_t cn = e ? (size_t)(e - s) : strlen(s);
        if (cn == n && memcmp(s, v, n) == 0) return true;
        s += cn + (e ? 1 : 0);
      }
      return false;
    }
    default:
      return strlen(v) <= (size_t)k.max && (!k.format || checkFormat(k.format, v));
  }
}

// The previous value of one key of a request, into the snapshot. A profile key captures its whole profile, once: every
// field it has (the write-only ones through the role), or the profile's removal when the slot was empty.
bool capture(const ConfigHost* h, const KeyDef* tk, size_t nk, const KeyDef& k, unsigned idx, ConfigSnap& s) {
  char line[LINE_MAX], reply[LINE_MAX], val[VALUE_MAX], form[VALUE_MAX];
  bool ok = false;
  lineFor(line, sizeof(line), k.get_line, idx, nullptr);
  if (!k.idx_max) {
    bool is_null = false;
    ok = h->run(h->user, line, reply, sizeof(reply)) && parseField(k.field, reply, val, sizeof(val), &is_null) &&
         (is_null || (setForm(k, val, form, sizeof(form)) && s.add(ConfigSnap::R_SET, (uint8_t)(&k - tk), 0, form)));
  } else {
    size_t first = 0;   // the profile's first key in the table stands for the profile
    while (first < nk && !sameProfile(tk[first], k)) first++;
    const uint8_t f = (uint8_t)first, ix = (uint8_t)idx;
    if (s.has(ConfigSnap::R_CLEAR, f, ix) || s.has(ConfigSnap::R_SET, f, ix)) return true;   // already captured
    if (!h->run(h->user, line, reply, sizeof(reply)) || strncmp(reply, "> ", 2) != 0) ok = false;
    else if (strstr(reply, ": (not set)")) ok = s.add(ConfigSnap::R_CLEAR, f, ix, nullptr);
    else {
      ok = true;
      for (size_t i = first; ok && i < nk; i++) {
        const KeyDef& kk = tk[i];
        if (!sameProfile(kk, k) || !(kk.flags & KF_WRITABLE)) continue;
        bool is_null = false;
        if (kk.flags & KF_WRITE_ONLY) {
          const ConfirmHost* ch = h->confirm;
          ok = ch->read_secret && ch->read_secret(h->user, kk, idx, val, sizeof(val));
        } else {
          ok = parseField(kk.field, reply, val, sizeof(val), &is_null);
        }
        if (ok && !is_null) ok = setForm(kk, val, form, sizeof(form)) && s.add(ConfigSnap::R_SET, (uint8_t)i, ix, form);
      }
    }
  }
  wipe(val, sizeof(val));
  wipe(form, sizeof(form));
  wipe(reply, sizeof(reply));
  return ok;
}

// The CLI line that puts one snapshot record back, rebuilt from the key table; false for a record that names no key a
// revert may set, or a value its line may not take.
bool restoreLine(const KeyDef* tk, size_t nk, const ConfigSnap::Rec& r, char* line, size_t cap) {
  if (r.key >= nk) return false;
  const KeyDef& k = tk[r.key];
  if (!(k.flags & KF_CONFIRM) || !(k.flags & KF_WRITABLE) || r.idx > k.idx_max) return false;
  if (r.kind == ConfigSnap::R_CLEAR) {
    if (!k.idx_max || strncmp(k.get_line, "get ", 4) != 0) return false;
    char fmt[48];
    snprintf(fmt, sizeof(fmt), "set %s clear", k.get_line + 4);   // "set wifi.profile %u clear"
    lineFor(line, cap, fmt, r.idx, nullptr);
    return true;
  }
  char v[ConfigSnap::VALUE_MAX + 1];
  memcpy(v, r.value, r.len);
  v[r.len] = 0;
  const bool ok = restorable(k, v);
  if (ok) lineFor(line, cap, k.set_line, r.idx, v);
  wipe(v, sizeof(v));
  return ok;
}

const char APPLY_FAILED_REVERTED[] = "{\"error\":\"apply-failed\",\"reverted\":true}";
const char APPLY_FAILED_KEPT[] = "{\"error\":\"apply-failed\",\"reverted\":false}";

}  // namespace

bool configRevert(const ConfigHost& h, const Job& j, bool* reboot) {
  *reboot = false;
  const ConfirmHost* ch = h.confirm;
  if (!ch || !ch->snap || !ch->run_as) return false;
  size_t nk;
  const KeyDef* tk = tableOf(&h, &nk);
  uint8_t* buf = static_cast<uint8_t*>(malloc(ConfigSnap::IMAGE_MAX));
  ConfigSnap* s = new (std::nothrow) ConfigSnap();
  bool ok = buf && s;
  if (ok) {
    const size_t n = ch->snap->load(buf, ConfigSnap::IMAGE_MAX);
    ok = n && s->parse(buf, n) && strcmp(s->job(), j.id) == 0;
  }
  ConfigSnap::Rec r;
  char line[LINE_MAX], reply[LINE_MAX];
  for (int i = 0; ok && s->rec(i, &r); i++) {
    // Every record is tried; one that fails leaves the job failed, never pending.
    const bool line_ok = restoreLine(tk, nk, r, line, sizeof(line));
    if (!line_ok || !ch->run_as(h.user, j, line, reply, sizeof(reply)) || !cliOk(reply)) ok = false;
    if (line_ok && (tk[r.key].flags & (KF_REBOOT | KF_RESTARTS))) *reboot = true;
    wipe(line, sizeof(line));
    wipe(reply, sizeof(reply));
  }
  if (buf) { wipe(buf, ConfigSnap::IMAGE_MAX); free(buf); }
  delete s;
  ch->snap->discard();
  return ok;
}

void configConfirmed(const ConfigHost& h) {
  if (h.confirm && h.confirm->snap) h.confirm->snap->discard();
}

void configSnapBoot(const ConfigHost& h) {
  const ConfirmHost* ch = h.confirm;
  if (!ch || !ch->snap) return;
  JobTable* t = ch->jobs ? ch->jobs->peek() : nullptr;   // at boot: no table, no job to keep
  const Job* open = t ? t->openJob() : nullptr;
  bool keep = false;
  if (open && open->state == JOB_AWAITING && strcmp(open->op, "config.apply") == 0) {
    uint8_t* buf = static_cast<uint8_t*>(malloc(ConfigSnap::IMAGE_MAX));
    ConfigSnap* s = new (std::nothrow) ConfigSnap();
    if (buf && s) {
      const size_t n = ch->snap->load(buf, ConfigSnap::IMAGE_MAX);
      keep = n && s->parse(buf, n) && strcmp(s->job(), open->id) == 0;
    }
    if (buf) { wipe(buf, ConfigSnap::IMAGE_MAX); free(buf); }
    delete s;
  }
  if (!keep) ch->snap->discard();   // an orphan of a cut apply, or of a job that ended: it holds old secrets
}

namespace {

}  // namespace

const KeyDef* findKey(const KeyDef* table, size_t n, const char* key, size_t len, unsigned* index) {
  for (size_t i = 0; i < n; i++)
    if (matches(table[i].key, key, len, index) && *index <= table[i].idx_max) return &table[i];
  return nullptr;
}

const KeyDef* findKey(const char* key, size_t len, unsigned* index) { return findKey(KEYS, NKEYS, key, len, index); }

bool parseField(uint8_t field, const char* reply, char* out, size_t cap, bool* is_null) {
  *is_null = false;
  if (strncmp(reply, "> ", 2) != 0) return false;
  const char* body = reply + 2;
  if (field == F_WHOLE) { copyOut(out, cap, body, strlen(body)); return true; }
  const char* colon = strstr(body, ": ");
  if (!colon) return false;
  const char* rest = colon + 2;
  if (strcmp(rest, "(not set)") == 0) {   // an empty profile: no SSID / host, nothing else known
    *is_null = !(field == F_WIFI_SSID || field == F_WIFI_PWD || field == F_MQTT_USER || field == F_MQTT_PWD);
    const char* v = field == F_WIFI_PWD || field == F_MQTT_USER || field == F_MQTT_PWD ? "unset" : "";
    copyOut(out, cap, v, strlen(v));
    return true;
  }
  switch (field) {
    case F_WIFI_SSID: {
      const char* p = strstr(rest, " prio=");
      if (!p) return false;
      copyOut(out, cap, rest, (size_t)(p - rest));
      return true;
    }
    case F_WIFI_PRIO: case F_MQTT_PRIO: return tokenAfter(rest - 1, "prio", out, cap);
    case F_WIFI_PWD: case F_MQTT_PWD: return tokenAfter(rest - 1, "pwd", out, cap);
    case F_MQTT_USER: return tokenAfter(rest - 1, "user", out, cap);
    case F_MQTT_TYPE: {
      const char* sp = strchr(rest, ' ');
      copyOut(out, cap, rest, sp ? (size_t)(sp - rest) : strlen(rest));
      return strcmp(out, "static") == 0 || strcmp(out, "discovery") == 0;
    }
    case F_MQTT_SITE:
      if (strncmp(rest, "discovery ", 10) != 0) { *is_null = true; out[0] = 0; return true; }
      return tokenAfter(rest - 1, "site", out, cap);
    case F_MQTT_TLS:
      if (strncmp(rest, "discovery ", 10) == 0) { copyOut(out, cap, "on", 2); return true; }   // always TLS
      return tokenAfter(rest - 1, "tls", out, cap);
    case F_MQTT_HOST: case F_MQTT_PORT: {
      if (strncmp(rest, "static ", 7) != 0) { *is_null = true; out[0] = 0; return true; }
      const char* hp = rest + 7;
      const char* sp = strchr(hp, ' ');
      const size_t n = sp ? (size_t)(sp - hp) : strlen(hp);
      const char* c = nullptr;
      for (size_t i = 0; i < n; i++) if (hp[i] == ':') c = hp + i;
      if (!c) return false;
      if (field == F_MQTT_HOST) copyOut(out, cap, hp, (size_t)(c - hp));
      else copyOut(out, cap, c + 1, (size_t)(hp + n - c - 1));
      return true;
    }
    default: return false;
  }
}

Status opConfigSchema(OpContext& c) {
  int64_t cursor = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  size_t nk;
  const KeyDef* tk = tableOf(hostOf(c), &nk);
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, (int64_t)nk, &cursor)) return ST_INVALID;
  JsonWriter& w = *c.out;
  w.key("profile"); w.str("config/1.0");
  w.key("keys"); w.beginArray();
  size_t i = (size_t)cursor;
  for (unsigned n = 0; i < nk && n < SCHEMA_PAGE; i++, n++) writeDef(w, tk[i]);
  w.endArray();
  w.key("next"); if (i < nk) w.num((int64_t)i); else w.null();
  return ST_OK;
}

Status opConfigGet(OpContext& c) {
  ConfigHost* h = hostOf(c);
  const JsonDoc& d = *c.doc;
  const int keys = c.req->args >= 0 ? jsonFind(d, c.req->args, "keys") : -1;
  if (!isArray(d, keys) || d.tok[keys].size < 1 || (unsigned)d.tok[keys].size > GET_MAX_KEYS) return ST_INVALID;
  static const char* const WHY[] = {nullptr, "unknown-key", "refused", "unreadable"};
  uint8_t why[GET_MAX_KEYS] = {0};
  bool any_error = false;
  JsonWriter& w = *c.out;
  char key[48], line[LINE_MAX], reply[LINE_MAX], val[VALUE_MAX];
  w.key("values"); w.beginObject();
  for (int i = 0; i < d.tok[keys].size; i++) {
    if (!jsonGetString(d, jsonElem(d, keys, i), key, sizeof(key), CS_NAME, 1)) return ST_INVALID;
    unsigned idx;
    bool is_null;
    size_t nk;
    const KeyDef* tk = tableOf(h, &nk);
    const KeyDef* k = findKey(tk, nk, key, strlen(key), &idx);
    if (k) lineFor(line, sizeof(line), k->get_line, idx, nullptr);
    if (!k) why[i] = 1;
    else if (!h->allow(h->user, line)) why[i] = 2;
    else if (!h->run(h->user, line, reply, sizeof(reply)) || !parseField(k->field, reply, val, sizeof(val), &is_null)) why[i] = 3;
    if (why[i]) { any_error = true; continue; }
    w.key(key);
    writeValue(w, *k, val, is_null);
  }
  w.endObject();
  if (any_error) {   // the keys that got no value, each with its reason
    w.key("errors"); w.beginObject();
    for (int i = 0; i < d.tok[keys].size; i++) {
      if (!why[i]) continue;
      jsonGetString(d, jsonElem(d, keys, i), key, sizeof(key), CS_NAME, 1);
      w.key(key); w.str(WHY[why[i]]);
    }
    w.endObject();
  }
  return ST_OK;
}

Status opConfigApply(OpContext& c) {
  ConfigHost* h = hostOf(c);
  const JsonDoc& d = *c.doc;
  const int set = c.req->args >= 0 ? jsonFind(d, c.req->args, "set") : -1;
  if (!isObject(d, set) || d.tok[set].size < 1 || (unsigned)d.tok[set].size > GET_MAX_KEYS) return ST_INVALID;
  char key[48], val[VALUE_MAX], line[LINE_MAX], reply[LINE_MAX];
  // "confirm":{"within_s":N} asks for commit-or-revert (OPERATIONS.md 3.4); N from 30 to 900, 300 when absent.
  const ConfirmHost* ch = h->confirm;
  const int conf = jsonFind(d, c.req->args, "confirm");
  int64_t within = 300;
  if (conf >= 0) {
    if (!isObject(d, conf)) return ST_INVALID;
    const int wt = jsonFind(d, conf, "within_s");
    if (wt >= 0 && !jsonGetInt(d, wt, 30, 900, &within)) return ST_INVALID;
  }
  bool needs = false;   // a key that can cut the caller's path
  uint8_t cls = 0;      // every CLI class among the lines, a bit each
  // Checked as a whole first: every key known and writable, every value of its type and range, every line allowed for
  // this caller. Nothing is written unless all of them pass.
  for (int i = 0, t = set + 1; i < d.tok[set].size; i++, t = jsonSkip(d, t + 1)) {
    if (!jsonGetString(d, t, key, sizeof(key), CS_NAME, 1)) return ST_INVALID;
    unsigned idx;
    size_t nk;
    const KeyDef* tk = tableOf(h, &nk);
    const KeyDef* k = findKey(tk, nk, key, strlen(key), &idx);
    const char* why = nullptr;
    if (!k) { c.error = keyError("unknown-key", key); return ST_INVALID; }
    if (!(k->flags & KF_WRITABLE)) { c.error = keyError("read-only", key); return ST_INVALID; }
    if (!cliValue(d, t + 1, *k, val, sizeof(val), &why)) { c.error = keyError(why, key); return ST_INVALID; }
    lineFor(line, sizeof(line), k->set_line, idx, val);
    const bool allowed = h->allow(h->user, line);
    if (ch && ch->line_class) cls |= (uint8_t)(1u << ch->line_class(h->user, line));
    if (k->flags & KF_CONFIRM) needs = true;
    memset(val, 0, sizeof(val));
    memset(line, 0, sizeof(line));
    if (!allowed) { c.error = keyError("refused", key); return ST_REFUSED; }
  }
  // Only the unsigned cable keeps its path whatever it changes; anyone else asks for confirmation, and a confirmation
  // that protects nothing is refused rather than ignored.
  const bool cable = ch && ch->cable && ch->cable(h->user);
  if (needs && conf < 0 && !cable) { c.error = "confirm-required"; return ST_INVALID; }
  if (conf >= 0 && !needs) { c.error = "confirm-not-needed"; return ST_INVALID; }
  Job* job = nullptr;
  JobTable* table = nullptr;
  if (conf >= 0) {
    if (!ch || !ch->jobs || !ch->snap || !ch->run_as || !ch->job_id || !ch->record_caller) {
      c.error = "confirm-unsupported";
      return ST_INVALID;
    }
    char jid[Job::ID_CAP + 1];
    if (!ch->job_id(h->user, *c.req, jid, sizeof(jid))) return ST_INVALID;
    table = ch->jobs->table();
    if (!table) { c.error = "no-memory"; return ST_BUSY; }
    Job* known = nullptr;
    const JobTable::Lookup l = table->find(jid, &known);
    if (l == JobTable::FOUND) { JobService::writeSummary(*c.out, *known); return ST_OK; }   // never a second run
    if (l == JobTable::EXPIRED) { c.error = "result-expired"; return ST_FAILED; }
    if (const Job* open = table->openJob()) {
      snprintf(g_error, sizeof(g_error), "job-open:%s", open->id);
      c.error = g_error;
      return ST_BUSY;
    }
    // The snapshot first, then the job, then the writes: a cut anywhere leaves either nothing applied or a job that
    // reverts, and an orphan snapshot is removed at the next boot.
    ConfigSnap* snap = new (std::nothrow) ConfigSnap();
    uint8_t* img = static_cast<uint8_t*>(malloc(ConfigSnap::IMAGE_MAX));
    bool ok = snap && img;
    if (ok) {
      snap->begin(jid);
      for (int i = 0, t = set + 1; ok && i < d.tok[set].size; i++, t = jsonSkip(d, t + 1)) {
        jsonGetString(d, t, key, sizeof(key), CS_NAME, 1);
        unsigned idx;
        size_t nk;
        const KeyDef* tk = tableOf(h, &nk);
        const KeyDef* k = findKey(tk, nk, key, strlen(key), &idx);
        ok = capture(h, tk, nk, *k, idx, *snap);
      }
    }
    const size_t n = ok ? snap->serialize(img, ConfigSnap::IMAGE_MAX) : 0;
    ok = n && ch->snap->save(img, n);
    if (img) { wipe(img, ConfigSnap::IMAGE_MAX); free(img); }
    delete snap;
    if (ok) {
      job = table->open(jid, "config.apply");
      ok = job != nullptr;
    }
    if (ok) {
      job->cls = cls;
      ch->record_caller(h->user, *job);
      ok = ch->jobs->persist();
      if (!ok) table->setState(job, JOB_FAILED);
    }
    if (!ok) { ch->snap->discard(); c.error = "storage"; return ST_BUSY; }
  }
  JsonWriter& w = *c.out;
  bool reboot = false, restarts = false, all_ok = true;   // what the writes did
  w.key("applied"); w.beginObject();
  for (int i = 0, t = set + 1; i < d.tok[set].size; i++, t = jsonSkip(d, t + 1)) {
    jsonGetString(d, t, key, sizeof(key), CS_NAME, 1);
    unsigned idx;
    size_t nk;
    const KeyDef* tk = tableOf(h, &nk);
    const KeyDef* k = findKey(tk, nk, key, strlen(key), &idx);
    const char* why = nullptr;
    cliValue(d, t + 1, *k, val, sizeof(val), &why);
    lineFor(line, sizeof(line), k->set_line, idx, val);
    const bool ok = h->run(h->user, line, reply, sizeof(reply)) && cliOk(reply);
    memset(val, 0, sizeof(val));   // a secret does not linger on the stack
    memset(line, 0, sizeof(line));
    w.key(key);
    if (ok) {
      w.str("ok");
      reboot |= (k->flags & KF_REBOOT) != 0;
      restarts |= (k->flags & KF_RESTARTS) != 0;
    } else {
      all_ok = false;
      w.str("failed");   // never the reply: it could quote the value back
    }
  }
  w.endObject();
  w.key("complete"); w.boolean(all_ok);   // false: a write the checks allowed still failed; the others stand
  w.key("reboot_needed"); w.boolean(reboot);
  w.key("restarting"); w.boolean(restarts);
  if (job) {
    w.key("job"); w.str(job->id);
    if (!all_ok) {   // a tentative change is all or nothing: what did apply goes back at once
      bool rb;
      const bool back = configRevert(*h, *job, &rb);
      const char* r = back ? APPLY_FAILED_REVERTED : APPLY_FAILED_KEPT;
      table->setState(job, JOB_FAILED, r, strlen(r));
      w.key("state"); w.str("failed");
      w.key("reverted"); w.boolean(back);
    } else {
      // A change that takes effect only after a reboot gets that reboot as its first boot.
      table->awaitConfirm(job, (uint32_t)within, !(reboot || restarts));
      w.key("state"); w.str("awaiting-confirm");
      w.key("confirm_left_s"); w.unum((uint32_t)within);
    }
    ch->jobs->persist();
  }
  return ST_OK;
}

}  // namespace Iface
