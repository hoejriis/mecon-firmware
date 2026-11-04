#include "ContactOps.h"
#include <string.h>
#include "Streams.h"

namespace Iface {

namespace {

ContactHost* hostOf(OpContext& c) { return static_cast<ContactHost*>(c.user); }

int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

bool hexNibble(char ch, uint8_t* v) {
  if (ch >= '0' && ch <= '9') *v = (uint8_t)(ch - '0');
  else if (ch >= 'a' && ch <= 'f') *v = (uint8_t)(ch - 'a' + 10);
  else if (ch >= 'A' && ch <= 'F') *v = (uint8_t)(ch - 'A' + 10);
  else return false;
  return true;
}

// The "key" argument: the contact's whole public key as 64 hex digits (a prefix would name different contacts over time).
bool keyArg(OpContext& c, uint8_t key[32]) {
  char hex[65];
  if (!jsonGetString(*c.doc, argOf(c, "key"), hex, sizeof(hex), CS_NAME, 64) || strlen(hex) != 64) return false;
  for (unsigned i = 0; i < 32; i++) {
    uint8_t hi, lo;
    if (!hexNibble(hex[2 * i], &hi) || !hexNibble(hex[2 * i + 1], &lo)) return false;
    key[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

void writeContact(JsonWriter& w, const ContactRec& r) {
  ContactEntry e{};
  memcpy(e.key, r.key, 32);
  e.name = r.name;
  e.type = r.type;
  e.fav = r.fav;
  e.last_advert = r.last_advert;
  e.hops = r.hops;
  e.has_pos = r.has_pos;
  e.lat_e6 = r.lat_e6;
  e.lon_e6 = r.lon_e6;
  writeContactFields(w, e);
}

}  // namespace

Status opContactList(OpContext& c) {
  ContactHost* h = hostOf(c);
  int64_t cursor = 0;
  const int ct = argOf(c, "cursor");
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, (int64_t)h->capacity, &cursor)) return ST_INVALID;
  JsonWriter& w = *c.out;
  w.key("capacity"); w.unum((uint64_t)h->capacity);
  w.key("count"); w.unum((uint64_t)h->count);
  w.key("contacts"); w.beginArray();
  // `at` is where the walk resumes: the cursor of the contact held in `r` that did not fit. The record number a contact
  // keeps while it is stored makes the walk safe against adds and removals between pages.
  int cur = (int)cursor, at = cur;
  unsigned n = 0;
  ContactRec r;
  bool more = h->next(h->user, cur, r);
  while (more && n < CONTACT_PAGE && w.size() + CONTACT_ENTRY_WORST <= CONTACT_LIST_BUDGET) {
    w.beginObject(); writeContact(w, r); w.endObject();
    n++;
    at = cur;
    more = h->next(h->user, cur, r);
  }
  w.endArray();
  w.key("next"); if (more) w.num(at); else w.null();
  return ST_OK;
}

Status opContactGet(OpContext& c) {
  ContactHost* h = hostOf(c);
  uint8_t key[32];
  if (!keyArg(c, key)) return ST_INVALID;
  ContactRec r;
  if (!h->get(h->user, key, r)) { c.error = "not-found"; return ST_FAILED; }
  writeContact(*c.out, r);
  return ST_OK;
}

Status opContactPut(OpContext& c) {
  ContactHost* h = hostOf(c);
  ContactRec r, was;
  memset(&r, 0, sizeof(r));
  if (!keyArg(c, r.key)) return ST_INVALID;
  const bool existed = h->get(h->user, r.key, was);
  if (existed) r = was;   // an update changes only what the request names
  const int nm = argOf(c, "name"), ty = argOf(c, "type"), la = argOf(c, "lat_e6"), lo = argOf(c, "lon_e6");
  if (nm >= 0 && !jsonGetString(*c.doc, nm, r.name, sizeof(r.name), CS_ANY, 1)) return ST_INVALID;
  if (ty >= 0) {
    // Type 0 is the anonymous slot's (PR 187): it has no spelling here, so it cannot be asked for.
    static const char* const TYPES[] = {"chat", "repeater", "room", "sensor"};
    char t[10];
    if (!jsonGetString(*c.doc, ty, t, sizeof(t), CS_NAME, 1)) return ST_INVALID;
    unsigned i = 0;
    while (i < 4 && strcmp(t, TYPES[i]) != 0) i++;
    if (i == 4) return ST_INVALID;
    r.type = (uint8_t)(i + 1);
  }
  if ((la >= 0) != (lo >= 0)) return ST_INVALID;   // a position is both coordinates or neither
  if (la >= 0) {
    int64_t lat, lon;
    if (!jsonGetInt(*c.doc, la, -90000000, 90000000, &lat) || !jsonGetInt(*c.doc, lo, -180000000, 180000000, &lon))
      return ST_INVALID;
    r.has_pos = true;
    r.lat_e6 = (int32_t)lat;
    r.lon_e6 = (int32_t)lon;
  }
  if (!existed && (!r.name[0] || !r.type)) return ST_INVALID;   // a new contact needs its name and type
  const bool changed = !existed || strcmp(r.name, was.name) != 0 || r.type != was.type || r.has_pos != was.has_pos ||
                       r.lat_e6 != was.lat_e6 || r.lon_e6 != was.lon_e6;
  if (changed) {   // a repeat of a put that already took effect writes nothing and leaves lastmod alone
    const ContactWrite st = h->put(h->user, r);
    if (st == CW_FULL) { c.error = "table-full"; return ST_FAILED; }
    if (st != CW_OK) { c.error = "write-failed"; return ST_FAILED; }
  }
  ContactRec now;
  if (!h->get(h->user, r.key, now)) { c.error = "write-failed"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  writeContact(w, now);
  w.key("created"); w.boolean(!existed);
  w.key("changed"); w.boolean(changed);
  return ST_OK;
}

Status opContactRemove(OpContext& c) {
  ContactHost* h = hostOf(c);
  uint8_t key[32];
  if (!keyArg(c, key)) return ST_INVALID;
  const bool removed = h->remove(h->user, key) > 0;   // absent is ok: a retry of a remove that took effect says so
  JsonWriter& w = *c.out;
  static const char H[] = "0123456789abcdef";
  char hex[65];
  for (int i = 0; i < 32; i++) { hex[2 * i] = H[key[i] >> 4]; hex[2 * i + 1] = H[key[i] & 15]; }
  hex[64] = 0;
  w.key("key"); w.str(hex);
  w.key("removed"); w.boolean(removed);
  return ST_OK;
}

Status opContactProtect(OpContext& c) {
  ContactHost* h = hostOf(c);
  uint8_t key[32];
  bool on;
  if (!keyArg(c, key)) return ST_INVALID;
  if (!jsonGetBool(*c.doc, argOf(c, "protected"), &on)) return ST_INVALID;
  const int st = h->protect(h->user, key, on);
  if (st < 0) { c.error = "not-found"; return ST_FAILED; }
  ContactRec r;
  if (!h->get(h->user, key, r)) { c.error = "not-found"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  writeContact(w, r);
  w.key("changed"); w.boolean(st > 0);
  return ST_OK;
}

}  // namespace Iface
