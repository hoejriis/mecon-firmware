#include "ChannelOps.h"
#include <string.h>

namespace Iface {

namespace {

ChannelHost* hostOf(OpContext& c) { return static_cast<ChannelHost*>(c.user); }

int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

// The "slot" argument, required and within the table.
bool slotArg(OpContext& c, int* slot) {
  int64_t v;
  if (!jsonGetInt(*c.doc, argOf(c, "slot"), 0, (int64_t)hostOf(c)->capacity - 1, &v)) return false;
  *slot = (int)v;
  return true;
}

bool hexKey(const char* s, uint8_t out[CHANNEL_SECRET_BYTES]) {
  if (strlen(s) != 2 * CHANNEL_SECRET_BYTES) return false;
  for (unsigned i = 0; i < 2 * CHANNEL_SECRET_BYTES; i++) {
    const char ch = s[i];
    const int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (v < 0) return false;
    if (i & 1) out[i / 2] |= (uint8_t)v; else out[i / 2] = (uint8_t)(v << 4);
  }
  return true;
}

void writeChannel(JsonWriter& w, int slot, const ChannelInfo& ch) {
  static const char H[] = "0123456789abcdef";
  const char hash[3] = {H[ch.hash >> 4], H[ch.hash & 15], 0};
  w.key("slot"); w.unum((uint64_t)slot);
  w.key("name"); w.str(ch.name);
  w.key("hash"); w.str(hash);
  w.key("key_bits"); w.unum((uint64_t)ch.key_bytes * 8);
}

}  // namespace

Status opChannelList(OpContext& c) {
  ChannelHost* h = hostOf(c);
  int64_t cursor = 0;
  const int ct = argOf(c, "cursor");
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, (int64_t)h->capacity, &cursor)) return ST_INVALID;
  JsonWriter& w = *c.out;
  w.key("capacity"); w.unum((uint64_t)h->capacity);
  w.key("channels"); w.beginArray();
  int slot = (int)cursor;
  unsigned n = 0;
  ChannelInfo ch;
  for (; slot < h->capacity && n < CHANNEL_PAGE; slot++) {
    if (!h->get(h->user, slot, ch) || !ch.name[0]) continue;
    w.beginObject(); writeChannel(w, slot, ch); w.endObject();
    n++;
  }
  w.endArray();
  w.key("next"); if (slot < h->capacity) w.num(slot); else w.null();
  return ST_OK;
}

Status opChannelGet(OpContext& c) {
  ChannelHost* h = hostOf(c);
  int slot;
  if (!slotArg(c, &slot)) return ST_INVALID;
  ChannelInfo ch;
  if (!h->get(h->user, slot, ch) || !ch.name[0]) { c.error = "empty-slot"; return ST_FAILED; }
  writeChannel(*c.out, slot, ch);
  return ST_OK;
}

Status opChannelPut(OpContext& c) {
  ChannelHost* h = hostOf(c);
  int slot;
  char name[CHANNEL_NAME_MAX + 1], hex[2 * CHANNEL_SECRET_BYTES + 1];
  uint8_t key[CHANNEL_SECRET_BYTES];
  if (!slotArg(c, &slot)) return ST_INVALID;
  if (!jsonGetString(*c.doc, argOf(c, "name"), name, sizeof(name), CS_ANY, 1)) return ST_INVALID;
  if (!jsonGetString(*c.doc, argOf(c, "secret"), hex, sizeof(hex), CS_NAME, 1) || !hexKey(hex, key)) return ST_INVALID;
  uint8_t any = 0;
  for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= key[i];
  if (!any) return ST_INVALID;   // an all-zero key is an empty slot, not a channel
  const int other = h->find(h->user, key);
  if (other >= 0 && other != slot) { c.error = "duplicate-secret"; return ST_FAILED; }   // the native table matches the first
  ChannelInfo before;
  const bool replaced = h->get(h->user, slot, before) && before.name[0];
  if (!h->set(h->user, slot, name, key)) { c.error = "write-failed"; return ST_FAILED; }
  ChannelInfo ch;
  if (!h->get(h->user, slot, ch)) { c.error = "write-failed"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  writeChannel(w, slot, ch);
  w.key("replaced"); w.boolean(replaced);
  return ST_OK;
}

Status opChannelRemove(OpContext& c) {
  ChannelHost* h = hostOf(c);
  int slot;
  if (!slotArg(c, &slot)) return ST_INVALID;
  ChannelInfo ch;
  const bool was = h->get(h->user, slot, ch) && ch.name[0];
  static const uint8_t ZERO[CHANNEL_SECRET_BYTES] = {0};
  if (was && !h->set(h->user, slot, "", ZERO)) { c.error = "write-failed"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  w.key("slot"); w.unum((uint64_t)slot);
  w.key("removed"); w.boolean(was);
  return ST_OK;
}

}  // namespace Iface
