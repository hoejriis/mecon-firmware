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
  w.key("source"); w.str(e.from_manager ? "manager" : "radio");
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

uint32_t Inbox::identity(const uint8_t* from, int channel, uint32_t ts, const char* text, size_t len) {
  uint32_t h = 2166136261u;   // FNV-1a: it tells one message from the next, and proves nothing
  const auto add = [&h](const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  };
  if (from) add(from, 32);
  const uint8_t ch = (uint8_t)channel;
  add(&ch, 1);
  add(reinterpret_cast<const uint8_t*>(&ts), sizeof(ts));
  add(reinterpret_cast<const uint8_t*>(text), len);
  return h | 2;   // never 0 or 1, the empty slots
}

bool Inbox::wasHeard(uint32_t id) const {
  for (uint32_t r : _recent)
    if ((r & ~1u) == (id & ~1u)) return true;
  return false;
}

bool Inbox::wasDelivered(uint32_t id) const {
  for (uint32_t r : _recent)
    if ((r & ~1u) == (id & ~1u) && (r & 1)) return true;
  return false;
}

void Inbox::remember(uint32_t id, bool delivered) {
  _recent[_recent_at] = (id & ~1u) | (delivered ? 1u : 0u);
  _recent_at = (uint8_t)((_recent_at + 1) % RECENT);
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

Status opMessageDeliver(OpContext& c) {
  InboxHost* h = static_cast<InboxHost*>(c.user);
  const int fr = argOf(c, "from"), ch = argOf(c, "channel"), tx = argOf(c, "text"), ts = argOf(c, "timestamp");
  if ((fr >= 0) == (ch >= 0) || tx < 0 || ts < 0) return ST_INVALID;   // one sender or slot, a text, and its own time
  uint8_t from[32];
  int64_t slot = -1, stamp = 0;
  if (fr >= 0) {
    char hex[65];
    if (!jsonGetString(*c.doc, fr, hex, sizeof(hex), CS_NAME, 64) || strlen(hex) != 64) return ST_INVALID;
    for (unsigned i = 0; i < 32; i++) {
      const auto nib = [](char x) { return x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : -1; };
      const int a = nib(hex[2 * i]), b = nib(hex[2 * i + 1]);
      if (a < 0 || b < 0) return ST_INVALID;
      from[i] = (uint8_t)(a << 4 | b);
    }
  }
  if (ch >= 0 && (!h->has_channels || !jsonGetInt(*c.doc, ch, 0, 126, &slot))) return ST_INVALID;
  if (!jsonGetInt(*c.doc, ts, 1, 0xFFFFFFFFLL, &stamp)) return ST_INVALID;
  char text[257];
  if (!jsonGetString(*c.doc, tx, text, sizeof(text), CS_ANY, 1)) { c.error = "text-too-long"; return ST_FAILED; }
  const DeliverResult r = h->deliver(h->user, fr >= 0 ? from : nullptr, (int)slot, (uint32_t)stamp, text, strlen(text));
  switch (r) {
    case DELIVER_NOT_FOUND: c.error = fr >= 0 ? "no-contact" : "no-channel"; return ST_FAILED;
    case DELIVER_TOO_LONG: c.error = "text-too-long"; return ST_FAILED;
    case DELIVER_QUEUE_FULL: c.error = "queue-full"; return ST_BUSY;
    case DELIVER_OK:
    case DELIVER_DUPLICATE: break;
  }
  JsonWriter& w = *c.out;
  w.key("state"); w.str(r == DELIVER_OK ? "delivered" : "duplicate");   // a duplicate has reached the app once already
  w.key("epoch"); w.unum(h->epoch);
  return ST_OK;
}

}  // namespace Iface
