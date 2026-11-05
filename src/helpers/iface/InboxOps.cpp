#include "InboxOps.h"
#include <new>
#include <string.h>

// For size, as in MessageOps.cpp.
#pragma GCC optimize("Os")

namespace Iface {

namespace {

// A page stops before this, whatever the limit: past it a result could outgrow what one reply holds.
constexpr size_t PAGE_MAX = 1100;
constexpr size_t ENTRY_FIELDS_MAX = 270;   // an entry's JSON without its text

void hexOf(const uint8_t* b, size_t n, char* out) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

const char* typeName(uint8_t t) {
  switch (t) {
    case 0: return "plain";
    case 1: return "cli-data";
    case 2: return "signed";
    case 3: return "cli-command";
  }
  return "other";
}

// At most what JsonWriter writes for this text: an escape takes 2 or 6 bytes.
size_t textJsonMax(const InboxEntry& e) {
  size_t n = 2;
  for (size_t i = 0; i < e.len; i++) {
    const uint8_t b = (uint8_t)e.text[i];
    n += b == '"' || b == '\\' ? 2 : (b < 0x20 || b >= 0x80 ? 6 : 1);
  }
  return n;
}

void writeEntry(JsonWriter& w, const InboxEntry& e, uint32_t now_ms) {
  char hex[65];
  w.beginObject();
  w.key("seq"); w.unum(e.seq);
  if (e.channel >= 0) {
    w.key("kind"); w.str("channel");
    w.key("channel"); w.num(e.channel);
  } else {
    w.key("kind"); w.str("direct");
    w.key("from"); hexOf(e.from, sizeof(e.from), hex); w.str(hex);
  }
  w.key("type"); w.str(typeName(e.type));
  if (e.type == 2) { w.key("signer"); hexOf(e.signer, sizeof(e.signer), hex); w.str(hex); }
  w.key("text"); w.str(e.text, e.len);
  if (e.cut) { w.key("cut"); w.boolean(true); }
  w.key("ts"); w.unum(e.ts);
  if (e.rx_ts) { w.key("rx_ts"); w.unum(e.rx_ts); }
  w.key("age_s"); w.unum((now_ms - e.rx_ms) / 1000);
  w.key("hash"); hexOf(e.hash, sizeof(e.hash), hex); w.str(hex);
  w.key("snr_x4"); w.num(e.snr_x4);
  w.key("route"); w.str(e.path_len == 0xFF ? "direct" : "flood");
  if (e.path_len != 0xFF) { w.key("path_len"); w.num(e.path_len); }
  w.endObject();
}

}  // namespace

Inbox::~Inbox() { delete[] _e; }

bool Inbox::alloc() {
  if (!_e) _e = new (std::nothrow) InboxEntry[SLOTS]();
  return _e != nullptr;
}

InboxEntry* Inbox::add() {
  _received++;
  if (!_e) { _lost++; return nullptr; }
  InboxEntry* slot = nullptr;
  for (int i = 0; i < SLOTS && !slot; i++)
    if (!_e[i].seq) slot = &_e[i];
  if (!slot) {
    for (int i = 0; i < SLOTS; i++)
      if (_e[i].channel >= 0 && (!slot || _e[i].seq < slot->seq)) slot = &_e[i];
    if (!slot)
      for (int i = 0; i < SLOTS; i++)
        if (!slot || _e[i].seq < slot->seq) slot = &_e[i];
    _lost++;
  }
  memset(slot, 0, sizeof(*slot));
  slot->seq = _received;
  return slot;
}

void Inbox::ackUpTo(uint32_t seq) {
  if (seq > _acked) _acked = seq;
  if (!_e) return;
  for (int i = 0; i < SLOTS; i++)
    if (_e[i].seq && _e[i].seq <= seq) memset(&_e[i], 0, sizeof(_e[i]));
}

const InboxEntry* Inbox::nextAfter(uint32_t after) const {
  const InboxEntry* best = nullptr;
  if (!_e) return nullptr;
  for (int i = 0; i < SLOTS; i++)
    if (_e[i].seq > after && (!best || _e[i].seq < best->seq)) best = &_e[i];
  return best;
}

int Inbox::unread() const {
  int n = 0;
  if (_e)
    for (int i = 0; i < SLOTS; i++) if (_e[i].seq) n++;
  return n;
}

bool writeInboxEventData(char* out, size_t cap, const Inbox& b) {
  JsonWriter w(out, cap);
  w.beginObject();
  w.key("received"); w.unum(b.received());
  w.key("unread"); w.num(b.unread());
  w.key("lost"); w.unum(b.lost());
  w.endObject();
  return w.ok();
}

Status opInboxList(OpContext& c) {
  InboxHost* h = static_cast<InboxHost*>(c.user);
  const int af = argOf(c, "after"), li = argOf(c, "limit");
  int64_t after = h->inbox->acked(), limit = Inbox::SLOTS;
  if (af >= 0 && !jsonGetInt(*c.doc, af, 0, 0xFFFFFFFFLL, &after)) return ST_INVALID;
  if (li >= 0 && !jsonGetInt(*c.doc, li, 1, Inbox::SLOTS, &limit)) return ST_INVALID;
  if (!h->inbox->alloc()) { c.error = "no-memory"; return ST_BUSY; }
  JsonWriter& w = *c.out;
  w.key("epoch"); w.unum(h->epoch);
  w.key("received"); w.unum(h->inbox->received());
  w.key("acked"); w.unum(h->inbox->acked());
  w.key("lost"); w.unum(h->inbox->lost());
  w.key("unread"); w.num(h->inbox->unread());
  w.key("messages"); w.beginArray();
  uint32_t at = (uint32_t)after;
  const InboxEntry* e = h->inbox->nextAfter(at);
  for (int n = 0; e && n < limit; n++) {
    if (n > 0 && w.size() + ENTRY_FIELDS_MAX + textJsonMax(*e) > PAGE_MAX) break;
    writeEntry(w, *e, h->now_ms);
    at = e->seq;
    e = h->inbox->nextAfter(at);
  }
  w.endArray();
  w.key("more"); w.boolean(e != nullptr);
  return ST_OK;
}

Status opInboxAck(OpContext& c) {
  InboxHost* h = static_cast<InboxHost*>(c.user);
  int64_t epoch = 0, upto = 0;
  if (!jsonGetInt(*c.doc, argOf(c, "epoch"), 1, 0xFFFFFFFFLL, &epoch) ||
      !jsonGetInt(*c.doc, argOf(c, "upto"), 1, 0xFFFFFFFFLL, &upto)) return ST_INVALID;
  // A seq counts within one boot: from before a reboot it would release messages its sender never saw.
  if ((uint32_t)epoch != h->epoch) { c.error = "wrong-epoch"; return ST_FAILED; }
  if ((uint32_t)upto > h->inbox->received()) { c.error = "not-received"; return ST_FAILED; }
  h->inbox->ackUpTo((uint32_t)upto);
  JsonWriter& w = *c.out;
  w.key("epoch"); w.unum(h->epoch);
  w.key("acked"); w.unum(h->inbox->acked());
  w.key("unread"); w.num(h->inbox->unread());
  w.key("lost"); w.unum(h->inbox->lost());
  return ST_OK;
}

}  // namespace Iface
