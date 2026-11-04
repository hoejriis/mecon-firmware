#include "MessageOps.h"
#include <string.h>

namespace Iface {

namespace {

MessageHost* hostOf(OpContext& c) { return static_cast<MessageHost*>(c.user); }

int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

void hexOf(const uint8_t* b, size_t n, char* out) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

bool hexNibble(char ch, uint8_t* v) {
  if (ch >= '0' && ch <= '9') *v = (uint8_t)(ch - '0');
  else if (ch >= 'a' && ch <= 'f') *v = (uint8_t)(ch - 'a' + 10);
  else if (ch >= 'A' && ch <= 'F') *v = (uint8_t)(ch - 'A' + 10);
  else return false;
  return true;
}

// The recipient: its whole public key, as contact.get takes it (a prefix would name different contacts over time).
bool keyArg(const JsonDoc& d, int t, uint8_t key[32]) {
  char hex[65];
  if (!jsonGetString(d, t, hex, sizeof(hex), CS_NAME, 64) || strlen(hex) != 64) return false;
  for (unsigned i = 0; i < 32; i++) {
    uint8_t hi, lo;
    if (!hexNibble(hex[2 * i], &hi) || !hexNibble(hex[2 * i + 1], &lo)) return false;
    key[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

void writeEntry(JsonWriter& w, const MessageEntry& e, uint32_t epoch) {
  char hex[17];
  w.key("message"); w.str(e.id);
  w.key("state"); w.str(messageStateName(e.state));
  w.key("hash"); hexOf(e.hash, sizeof(e.hash), hex); w.str(hex);
  if (e.channel >= 0) {
    w.key("channel"); w.num(e.channel);
  } else {
    w.key("to"); hexOf(e.to, sizeof(e.to), hex); w.str(hex);
    w.key("ack"); hexOf(reinterpret_cast<const uint8_t*>(&e.ack), sizeof(e.ack), hex); w.str(hex);
    w.key("est_ms"); w.unum(e.est_ms);
  }
  w.key("route"); w.str(e.flood ? "flood" : "direct");
  w.key("ts"); w.unum(e.ts);
  if (e.state == MSG_DUTY_HELD) { w.key("wait_ms"); w.unum(e.wait_ms); }
  w.key("epoch"); w.unum(epoch);
}

}  // namespace

const char* messageStateName(uint8_t s) {
  switch (s) {
    case MSG_QUEUED: return "queued";
    case MSG_DUTY_HELD: return "duty-held";
    case MSG_TRANSMITTED: return "transmitted";
    case MSG_ACKED: return "acked";
    case MSG_TIMED_OUT: return "timed-out";
    case MSG_FAILED: return "failed";
  }
  return "unknown";
}

MessageEntry* MessageTable::find(const char* id) {
  for (MessageEntry& e : _e)
    if (e.state && strcmp(e.id, id) == 0) return &e;
  return nullptr;
}

MessageEntry* MessageTable::claim(const char* id) {
  if (strlen(id) > MessageEntry::ID_CAP) return nullptr;
  MessageEntry* slot = nullptr;
  for (MessageEntry& e : _e)
    if (!e.state) { slot = &e; break; }
  if (!slot)
    for (MessageEntry& e : _e)
      if (messageFinal(e) && e.collected && (!slot || e.seq < slot->seq)) slot = &e;
  if (!slot)
    for (MessageEntry& e : _e)
      if (messageFinal(e) && (!slot || e.seq < slot->seq)) slot = &e;
  if (!slot) return nullptr;
  memset(slot, 0, sizeof(*slot));
  strcpy(slot->id, id);
  slot->seq = ++_seq;
  return slot;
}

void MessageTable::onTx(const uint8_t hash[8], uint32_t now) {
  for (MessageEntry& e : _e) {
    if ((e.state != MSG_QUEUED && e.state != MSG_DUTY_HELD) || memcmp(e.hash, hash, sizeof(e.hash)) != 0) continue;
    e.state = MSG_TRANSMITTED;
    e.since_ms = now;   // the estimate counts from here, not from a request that may have waited an hour for airtime
    e.tell = true;
  }
}

void MessageTable::onTxFail(const uint8_t hash[8]) {
  for (MessageEntry& e : _e) {
    if ((e.state != MSG_QUEUED && e.state != MSG_DUTY_HELD) || memcmp(e.hash, hash, sizeof(e.hash)) != 0) continue;
    e.state = MSG_FAILED;
    e.tell = true;
  }
}

void MessageTable::onHeld(const uint8_t hash[8], uint32_t wait_ms) {
  for (MessageEntry& e : _e) {
    if ((e.state != MSG_QUEUED && e.state != MSG_DUTY_HELD) || memcmp(e.hash, hash, sizeof(e.hash)) != 0) continue;
    if (e.state != MSG_DUTY_HELD) e.tell = true;
    e.state = MSG_DUTY_HELD;
    e.wait_ms = wait_ms;
  }
}

bool MessageTable::onAck(uint32_t ack, uint8_t to[6]) {
  bool hit = false;
  if (!ack) return false;
  for (MessageEntry& e : _e) {
    if (e.channel >= 0 || e.ack != ack) continue;
    // An ACK can only follow a transmission, and the native ACK can arrive more than once: only the first changes this.
    if (e.state == MSG_TRANSMITTED || e.state == MSG_TIMED_OUT) { e.state = MSG_ACKED; e.tell = true; }
    if (e.state == MSG_ACKED) {
      memcpy(to, e.to, sizeof(e.to));
      hit = true;
    }
  }
  return hit;
}

void MessageTable::tick(uint32_t now) {
  for (MessageEntry& e : _e) {
    if (e.state == MSG_TRANSMITTED && e.channel < 0 && now - e.since_ms >= e.est_ms) {
      e.state = MSG_TIMED_OUT;
      e.tell = true;
    } else if ((e.state == MSG_QUEUED || e.state == MSG_DUTY_HELD) && now - e.since_ms >= QUEUE_LIMIT_MS) {
      e.state = MSG_UNKNOWN;   // the queue lost it without saying so: never read as failed, or as sent
      e.tell = true;
    }
  }
}

MessageEntry* MessageTable::nextToTell() {
  for (MessageEntry& e : _e)
    if (e.state && e.tell) { e.tell = false; return &e; }
  return nullptr;
}

int MessageTable::open() const {
  int n = 0;
  for (const MessageEntry& e : _e) if (e.state && !messageFinal(e)) n++;
  return n;
}

int MessageTable::owed() const {
  int n = 0;
  for (const MessageEntry& e : _e) if (e.state && messageFinal(e) && !e.collected) n++;
  return n;
}

bool writeMessageEventData(char* out, size_t cap, const MessageEntry& e) {
  char hex[17];
  JsonWriter w(out, cap);
  w.beginObject();
  w.key("message"); w.str(e.id);
  w.key("hash"); hexOf(e.hash, sizeof(e.hash), hex); w.str(hex);
  w.key("state"); w.str(messageStateName(e.state));
  w.endObject();
  return w.ok();
}

Status opMessageSend(OpContext& c) {
  MessageHost* h = hostOf(c);
  const int to = argOf(c, "to"), ch = argOf(c, "channel"), tx = argOf(c, "text"), ts = argOf(c, "timestamp"),
            at = argOf(c, "attempt");
  if ((to >= 0) == (ch >= 0) || tx < 0) return ST_INVALID;   // one recipient, and something to say
  uint8_t key[32];
  int64_t slot = -1;
  if (to >= 0 && !keyArg(*c.doc, to, key)) return ST_INVALID;
  if (ch >= 0 && (!h->has_channels || !jsonGetInt(*c.doc, ch, 0, 126, &slot))) return ST_INVALID;
  if (at >= 0 && ch >= 0) return ST_INVALID;                 // the retry count of a channel message has no native field
  int64_t stamp = 0, attempt = 0;
  if (ts >= 0 && !jsonGetInt(*c.doc, ts, 1, 0xFFFFFFFFLL, &stamp)) return ST_INVALID;
  if (at >= 0 && !jsonGetInt(*c.doc, at, 0, 3, &attempt)) return ST_INVALID;
  char text[257];   // larger than any native limit, so a text too long is told from a malformed one below
  if (!jsonGetString(*c.doc, tx, text, sizeof(text), CS_ANY, 1)) {
    c.error = "text-too-long";
    return ST_FAILED;
  }
  const size_t len = strlen(text);
  if (len > h->text_max) { c.error = "text-too-long"; return ST_FAILED; }
  if (!h->table) { c.error = "no-memory"; return ST_BUSY; }
  char id[MessageEntry::ID_CAP + 1];
  if (!h->identity(h->user, *c.req, id, sizeof(id))) return ST_INVALID;
  JsonWriter& w = *c.out;
  const MessageEntry* known = h->table->find(id);
  if (known) {   // a retry whose reply was lost, the result cache having moved on: never a second send
    writeEntry(w, *known, h->epoch);
    return ST_OK;
  }
  // Before the send, so that a send never goes out with nowhere to record it.
  MessageEntry* e = h->table->claim(id);
  if (!e) { c.error = "outcomes-full"; return ST_BUSY; }
  e->channel = (int8_t)slot;
  e->ts = ts >= 0 ? (uint32_t)stamp : h->clock(h->user);
  MessageSent sent = {};
  const MessageSend r = to >= 0 ? h->send_dm(h->user, key, e->ts, (uint8_t)attempt, text, sent)
                                : h->send_channel(h->user, (int)slot, e->ts, text, len, sent);
  if (r != MS_OK) {
    memset(e, 0, sizeof(*e));   // nothing was sent: the request's own answer is the whole record
    if (r == MS_QUEUE_FULL) { c.error = "queue-full"; return ST_BUSY; }
    c.error = r == MS_TOO_LONG ? "text-too-long" : (to >= 0 ? "no-contact" : "no-channel");
    return ST_FAILED;
  }
  memcpy(e->hash, sent.hash, sizeof(e->hash));
  e->ack = sent.ack;
  e->est_ms = sent.est_ms;
  e->flood = sent.flood;
  if (to >= 0) memcpy(e->to, key, sizeof(e->to));
  e->since_ms = h->now_ms;
  e->state = MSG_QUEUED;
  e->tell = true;
  writeEntry(w, *e, h->epoch);
  return ST_OK;
}

Status opMessageGet(OpContext& c) {
  MessageHost* h = hostOf(c);
  char id[MessageEntry::ID_CAP + 1];
  if (!jsonGetString(*c.doc, argOf(c, "message"), id, sizeof(id), CS_JOB, 1)) return ST_INVALID;
  const MessageEntry* e = h->table ? h->table->find(id) : nullptr;
  JsonWriter& w = *c.out;
  if (e) {
    writeEntry(w, *e, h->epoch);
    return ST_OK;
  }
  // Not here: evicted, lost to a reboot or never sent. Only the caller can tell, by the epoch it was sent under.
  w.key("message"); w.str(id);
  w.key("state"); w.str(messageStateName(MSG_UNKNOWN));
  w.key("epoch"); w.unum(h->epoch);
  return ST_OK;
}

Status opMessageList(OpContext& c) {
  MessageHost* h = hostOf(c);
  JsonWriter& w = *c.out;
  w.key("open"); w.num(h->table ? h->table->open() : 0);
  w.key("owed"); w.num(h->table ? h->table->owed() : 0);
  w.key("epoch"); w.unum(h->epoch);
  w.key("messages"); w.beginArray();
  if (h->table) {
    char hex[17];
    for (int i = 0; i < MessageTable::SLOTS; i++) {
      const MessageEntry& e = h->table->at(i);
      if (!e.state) continue;
      w.beginObject();
      w.key("message"); w.str(e.id);
      w.key("hash"); hexOf(e.hash, sizeof(e.hash), hex); w.str(hex);
      w.key("state"); w.str(messageStateName(e.state));
      w.endObject();
    }
  }
  w.endArray();
  return ST_OK;
}

Status opMessageAck(OpContext& c) {
  MessageHost* h = hostOf(c);
  char id[MessageEntry::ID_CAP + 1];
  if (!jsonGetString(*c.doc, argOf(c, "message"), id, sizeof(id), CS_JOB, 1)) return ST_INVALID;
  MessageEntry* e = h->table ? h->table->find(id) : nullptr;
  if (e && !messageFinal(*e)) { c.error = "not-final"; return ST_FAILED; }
  if (e) e->collected = true;
  c.out->key("message"); c.out->str(id);
  c.out->key("known"); c.out->boolean(e != nullptr);   // absent is ok: a retry of an ack whose reply was lost
  return ST_OK;
}

}  // namespace Iface
