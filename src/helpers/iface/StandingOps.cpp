#include "StandingOps.h"
#include <stdio.h>
#include <string.h>

namespace Iface {

namespace {

const char* const NAMES[] = {"health", "radio", "location", "counters"};

bool fieldsOf(const JsonDoc& d, int arr, uint8_t* out) {
  if (arr < 0 || d.tok[arr].type != JSMN_ARRAY || d.tok[arr].size < 1 || d.tok[arr].size > 4) return false;
  uint8_t m = 0;
  for (int i = 0, t = arr + 1; i < d.tok[arr].size; i++, t = jsonSkip(d, t)) {
    char name[12];   // "counters" is the longest
    if (!jsonGetString(d, t, name, sizeof(name), CS_NAME, 1)) return false;
    uint8_t bit = 0;
    for (int k = 0; k < 4; k++) if (strcmp(name, NAMES[k]) == 0) bit = (uint8_t)(1u << k);
    if (!bit) return false;
    m |= bit;
  }
  *out = m;
  return true;
}

bool hexKey(const char* hex, uint8_t out[32]) {
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

// Standard base64 with padding, as the rf stream's reports carry it.
void b64(char* out, const uint8_t* in, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = ((uint32_t)in[i] << 16) | ((i + 1 < n ? (uint32_t)in[i + 1] : 0) << 8) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = A[v >> 18];
    out[o++] = A[(v >> 12) & 63];
    out[o++] = i + 1 < n ? A[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < n ? A[v & 63] : '=';
  }
  out[o] = 0;
}

uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const uint8_t KEEP_BIT = 0x80;   // in the stored fields byte: the instruction keeps its reports

void writeFields(JsonWriter& w, uint8_t m) {
  w.beginArray();
  for (int k = 0; k < 4; k++) if (m & (1u << k)) w.str(NAMES[k]);
  w.endArray();
}

}  // namespace

// Which fields and where: "fields", and "to" (a node key) or "slot" (a channel). A private field needs the right to read it, and
// never goes on a channel, where every member can read it. A field this board cannot produce is an answer, not a zero.
bool StandingService::parseTarget(OpContext& c, uint8_t* fields, uint8_t to[32], bool* has_to, int* slot) {
  const JsonDoc& d = *c.doc;
  if (c.req->args < 0 || !fieldsOf(d, jsonFind(d, c.req->args, "fields"), fields)) return false;
  *has_to = false;
  *slot = -1;
  char hex[65];
  const int t = jsonFind(d, c.req->args, "to");
  if (t >= 0) {
    if (!jsonGetString(d, t, hex, sizeof(hex), CS_NAME, 64) || !hexKey(hex, to)) return false;
    *has_to = true;
  }
  const int sl = jsonFind(d, c.req->args, "slot");
  if (sl >= 0) {
    int64_t v;
    if (!jsonGetInt(d, sl, 0, 255, &v)) return false;
    *slot = (int)v;
  }
  return *has_to || *slot >= 0;
}

Status StandingService::opNow(OpContext& c, uint32_t now) {
  uint8_t fields, to[32];
  bool has_to;
  int slot;
  if (!parseTarget(c, &fields, to, &has_to, &slot)) return ST_INVALID;
  if ((fields & _h.produced(_h.user)) != fields) { c.error = "unsupported-field"; return ST_FAILED; }
  if ((fields & RF_PRIVATE) && (slot >= 0 || !_h.may_read_private(_h.user))) { c.error = "private"; return ST_REFUSED; }
  if (_now_sent && (uint32_t)(now - _now_ms) < NOW_GAP_MS) { c.error = "rate-limited"; return ST_BUSY; }
  uint32_t seq = 0;
  if (!_h.send(_h.user, fields, has_to ? to : nullptr, slot, &seq, nullptr, nullptr)) { c.error = "not-sent"; return ST_BUSY; }
  _now_sent = true;
  _now_ms = now;
  JsonWriter& w = *c.out;
  w.key("seq"); w.unum(seq);
  w.key("fields"); writeFields(w, fields);
  return ST_OK;
}

Status StandingService::opPut(OpContext& c, uint32_t now) {
  const JsonDoc& d = *c.doc;
  Standing n;
  bool has_to;
  int slot;
  if (c.req->args < 0 || !jsonGetString(d, jsonFind(d, c.req->args, "id"), n.id, sizeof(n.id), CS_ID, 1)) return ST_INVALID;
  if (!parseTarget(c, &n.fields, n.to, &has_to, &slot)) return ST_INVALID;
  n.has_to = has_to;
  n.slot = (int16_t)slot;
  int64_t every;
  if (!jsonGetInt(d, jsonFind(d, c.req->args, "every_s"), MIN_EVERY_S, MAX_EVERY_S, &every)) return ST_INVALID;
  n.every_s = (uint32_t)every;
  const int rl = jsonFind(d, c.req->args, "runs");
  if (rl >= 0) {
    int64_t v;
    if (!jsonGetInt(d, rl, 1, 0xFFFF, &v)) return ST_INVALID;
    n.runs_left = (uint16_t)v;
  }
  const int dur = jsonFind(d, c.req->args, "durable");
  if (dur >= 0) {
    bool v;
    if (!jsonGetBool(d, dur, &v)) return ST_INVALID;
    n.durable = v;
  }
  const int kp = jsonFind(d, c.req->args, "keep");
  if (kp >= 0 && !jsonGetBool(d, kp, &n.keep)) return ST_INVALID;
  if ((n.fields & _h.produced(_h.user)) != n.fields) { c.error = "unsupported-field"; return ST_FAILED; }
  if ((n.fields & RF_PRIVATE) && (slot >= 0 || !_h.may_read_private(_h.user))) { c.error = "private"; return ST_REFUSED; }
  if (n.keep && !_kept.available()) { c.error = "unsupported"; return ST_FAILED; }   // a board whose flash cannot hold the ring
  snprintf(n.issuer, sizeof(n.issuer), "%s", _h.issuer(_h.user));
  Standing* place = nullptr;
  for (Standing& s : _s) if (s.used && strcmp(s.id, n.id) == 0) place = &s;
  if (!place) for (Standing& s : _s) if (!s.used) { place = &s; break; }
  if (!place) { c.error = "full"; return ST_BUSY; }
  const Standing old = *place;
  n.used = true;
  *place = n;
  schedule(*place, now, false);
  place->fresh = true;
  if (!persist()) { *place = old; c.error = "storage"; return ST_BUSY; }
  JsonWriter& w = *c.out;
  w.key("id"); w.str(n.id);
  w.key("every_s"); w.unum(n.every_s);
  return ST_OK;
}

Status StandingService::opList(OpContext& c) {
  JsonWriter& w = *c.out;
  w.key("capacity"); w.num(CAPACITY);
  w.key("instructions"); w.beginArray();
  for (const Standing& s : _s) {
    if (!s.used) continue;
    w.beginObject();
    w.key("id"); w.str(s.id);
    w.key("fields"); writeFields(w, s.fields);
    w.key("every_s"); w.unum(s.every_s);
    if (s.runs_left) { w.key("runs_left"); w.unum(s.runs_left); }
    w.key("durable"); w.boolean(s.durable);
    if (s.keep) { w.key("keep"); w.boolean(true); }
    if (s.slot >= 0) { w.key("slot"); w.num(s.slot); }
    w.key("runs"); w.unum(s.runs);
    w.key("failed"); w.unum(s.failed);
    w.key("skipped"); w.unum(s.skipped);
    w.endObject();
  }
  w.endArray();
  return ST_OK;
}

Status StandingService::opRemove(OpContext& c) {
  const JsonDoc& d = *c.doc;
  char id[17];
  if (c.req->args < 0 || !jsonGetString(d, jsonFind(d, c.req->args, "id"), id, sizeof(id), CS_ID, 1)) return ST_INVALID;
  for (Standing& s : _s) {
    if (s.used && strcmp(s.id, id) == 0) {
      const Standing old = s;
      s = Standing();
      if (!persist()) { s = old; c.error = "storage"; return ST_BUSY; }
      JsonWriter& w = *c.out;
      w.key("id"); w.str(id);
      w.key("removed"); w.boolean(true);
      return ST_OK;
    }
  }
  c.error = "unknown-id";
  return ST_FAILED;
}

namespace {

struct ListState {
  JsonWriter* w;
  uint32_t limit, listed, withheld;
  size_t budget;
  bool priv, more;
};

bool isPrivate(const KeptEntry& e) { return e.len > 2 && (e.bytes[2] & RF_PRIVATE) != 0; }

bool listVisit(void* user, const KeptEntry& e) {
  ListState& st = *static_cast<ListState*>(user);
  if (isPrivate(e) && !st.priv) { st.withheld++; return true; }   // never cut out of a report: the signature covers it
  if (st.more) return true;   // the page is full; the rest is only counted
  const size_t est = 90 + 4 * ((e.len + 2) / 3);
  if (st.listed >= st.limit || (st.listed > 0 && st.w->size() + est > st.budget)) { st.more = true; return true; }
  char text[4 * ((REPORT_MAX + 2) / 3) + 4];
  b64(text, e.bytes, e.len);
  st.w->beginObject();
  st.w->key("cursor"); st.w->unum(e.cursor);
  st.w->key("boot"); st.w->unum(rd32(e.bytes + 3));
  st.w->key("seq"); st.w->unum(rd32(e.bytes + 7));
  st.w->key("report"); st.w->str(text);
  st.w->endObject();
  st.listed++;
  return true;
}

struct AckState {
  uint32_t upto, owner;
  bool priv, withheld, foreign;
};

bool ackVisit(void* user, const KeptEntry& e) {
  AckState& st = *static_cast<AckState*>(user);
  if (e.cursor > st.upto) return false;
  if (isPrivate(e) && !st.priv) st.withheld = true;   // it was not listed to this caller, so this caller cannot release it
  if (e.owner != st.owner) st.foreign = true;
  return true;
}

}  // namespace

Status StandingService::opReportList(OpContext& c) {
  if (!_kept.available()) { c.error = "unsupported"; return ST_FAILED; }
  const JsonDoc& d = *c.doc;
  int64_t after = 0, limit = 8;
  const int af = c.req->args >= 0 ? jsonFind(d, c.req->args, "after") : -1;
  const int li = c.req->args >= 0 ? jsonFind(d, c.req->args, "limit") : -1;
  if (af >= 0 && !jsonGetInt(d, af, 0, 0xFFFFFFFFLL, &after)) return ST_INVALID;
  if (li >= 0 && !jsonGetInt(d, li, 1, 8, &limit)) return ST_INVALID;
  JsonWriter& w = *c.out;
  ListState st = {&w, (uint32_t)limit, 0, 0, _h.page_budget ? _h.page_budget(_h.user) : 1100, _h.may_read_private(_h.user), false};
  w.key("reports"); w.beginArray();
  _kept.each((uint32_t)after, listVisit, &st);
  w.endArray();
  w.key("more"); w.boolean(st.more);
  w.key("dropped"); w.unum(_kept.dropped());
  w.key("withheld"); w.unum(st.withheld);
  return ST_OK;
}

// Releases what the caller could list, and only what is the caller's: the acknowledgement belongs to the manager that set the
// keeping instruction (the member that signed it, or an unsigned caller where nothing is enrolled).
Status StandingService::opReportAck(OpContext& c) {
  if (!_kept.available()) { c.error = "unsupported"; return ST_FAILED; }
  const JsonDoc& d = *c.doc;
  int64_t upto = 0;
  if (c.req->args < 0 || !jsonGetInt(d, jsonFind(d, c.req->args, "upto"), 1, 0xFFFFFFFFLL, &upto)) return ST_INVALID;
  if ((uint32_t)upto > _kept.last()) { c.error = "unknown-cursor"; return ST_FAILED; }
  AckState st = {(uint32_t)upto, KeptReports::ownerOf(_h.issuer(_h.user)), _h.may_read_private(_h.user), false, false};
  _kept.each(0, ackVisit, &st);
  if (st.withheld) { c.error = "location-withheld"; return ST_REFUSED; }
  if (st.foreign) { c.error = "not-owner"; return ST_REFUSED; }
  uint32_t released = 0;
  if (!_kept.release((uint32_t)upto, &released)) { c.error = "storage"; return ST_BUSY; }
  JsonWriter& w = *c.out;
  w.key("acked"); w.unum(_kept.mark());
  w.key("released"); w.unum(released);
  w.key("remaining"); w.unum(_kept.held());
  return ST_OK;
}

// A jitter of up to a tenth of the period, at most 5 minutes, so two devices do not stay in step. After a boot the first run is
// anywhere within that jitter of a period's end: a missed tick is gone, never made up.
void StandingService::schedule(Standing& s, uint32_t now, bool first) {
  uint32_t j = s.every_s / 10;
  if (j > 300) j = 300;
  const uint32_t jitter = j ? _h.random(_h.user) % (j * 1000) : 0;
  s.from_ms = now;
  s.wait_ms = first ? jitter : s.every_s * 1000UL + jitter;   // at most 30 days and 5 minutes: well inside 32 bits of ms
}

void StandingService::tick(uint32_t now) {
  if (!_begun) return;
  _kept.tick(now);
  bool changed = false;
  for (Standing& s : _s) {
    if (!s.used) continue;
    // A loop takes its time once and handles requests later in the pass, so a put can be stamped after the time this
    // tick is given; the difference would wrap and the run fall due at once. Its period starts here instead.
    if (s.fresh) { s.fresh = false; s.from_ms = now; continue; }
    if ((uint32_t)(now - s.from_ms) < s.wait_ms) continue;
    if (s.issuer[0] && !_h.issuer_valid(_h.user, s.issuer)) {   // revoked, or the Deployment went
      s = Standing();
      changed = true;
      continue;
    }
    if (!_h.airtime_ok(_h.user)) {   // not a try: it waits a little, as a boot starts with the hour's window full, then it is passed over
      if (s.held < HOLDS) {
        s.held++;
        s.from_ms = now;
        s.wait_ms = HOLD_MS;
      } else {
        s.held = 0;
        s.skipped++;
        schedule(s, now, false);
      }
      continue;
    }
    s.held = 0;
    uint32_t seq;
    uint8_t built[REPORT_MAX];
    size_t built_len = 0;
    const bool sent = _h.send(_h.user, s.fields, s.has_to ? s.to : nullptr, s.slot, &seq, s.keep ? built : nullptr, &built_len);
    if (sent) s.runs++;
    else s.failed++;
    // Kept whether or not it went: a datagram has no acknowledgement, so the device cannot know it was heard. One append.
    if (s.keep && built_len) _kept.add(built, built_len, KeptReports::ownerOf(s.issuer), now);
    schedule(s, now, false);
    if (s.runs_left) {
      if (--s.runs_left == 0) s = Standing();
      changed = true;   // the count is kept, so a reboot does not give a bounded instruction its runs back
    }
    break;   // one on a pass: a report is airtime, and the next can wait
  }
  if (changed) persist();
}

void StandingService::clear() {
  for (Standing& s : _s) s = Standing();
  _kept.clear();
  if (_begun) persist();
}

// {1, count}, then fixed records. Temporary instructions are not written: they end with the boot, as a subscription does.
bool StandingService::persist() {
  if (!_h.store) return false;
  uint8_t buf[2 + CAPACITY * RECORD];
  size_t n = 2;
  uint8_t count = 0;
  for (const Standing& s : _s) {
    if (!s.used || !s.durable) continue;
    uint8_t* p = buf + n;
    memcpy(p, s.id, 17); p += 17;
    memcpy(p, s.issuer, 17); p += 17;
    *p++ = (uint8_t)(s.fields | (s.keep ? KEEP_BIT : 0));
    p = put32(p, s.every_s);
    p = put16(p, s.runs_left);
    *p++ = s.has_to ? 1 : 0;
    memcpy(p, s.to, 32); p += 32;
    *p++ = (uint8_t)(s.slot + 1);
    n += RECORD;
    count++;
  }
  buf[0] = 1;
  buf[1] = count;
  if (!count) { _h.store->discard(); return true; }
  return _h.store->save(buf, n);
}

void StandingService::load(uint32_t now) {
  if (!_h.store) return;
  uint8_t buf[2 + CAPACITY * RECORD];
  const size_t n = _h.store->load(buf, sizeof(buf));
  if (n < 2 || buf[0] != 1 || buf[1] > CAPACITY || n != 2 + (size_t)buf[1] * RECORD) return;
  for (int i = 0; i < buf[1]; i++) {
    const uint8_t* p = buf + 2 + i * RECORD;
    Standing& s = _s[i];
    s.used = true;
    memcpy(s.id, p, 17); s.id[16] = 0; p += 17;
    memcpy(s.issuer, p, 17); s.issuer[16] = 0; p += 17;
    s.keep = (*p & KEEP_BIT) != 0 && _kept.available();
    s.fields = *p++ & RF_ALL;
    s.every_s = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); p += 4;
    s.runs_left = (uint16_t)(p[0] | (p[1] << 8)); p += 2;
    s.has_to = *p++ != 0;
    memcpy(s.to, p, 32); p += 32;
    s.slot = (int16_t)*p - 1;
    if (s.every_s < MIN_EVERY_S) { s = Standing(); continue; }
    schedule(s, now, true);
  }
}

void StandingService::begin(const ReportHost& h, uint32_t now) {
  _h = h;
  _begun = true;
  #if defined(NRF52_PLATFORM)
  const bool nrf52 = true;   // its file system wears fast: eight reports to a write (KeptReports.h)
#else
  const bool nrf52 = false;
#endif
  _kept.begin(h.kept, h.kept_batch ? h.kept_batch : keptBatchFor(nrf52));
  load(now);
}

}  // namespace Iface
