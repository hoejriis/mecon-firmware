#pragma once
// The device interface over LoRa, point to point (docs/contract/LORA_MANAGEMENT.md): messages cut into frames that fit one
// packet, a session per peer, and a deadline on every state. Pure logic: the mesh calls and the dispatch are the caller's, so
// this runs in the native unit tests. Nothing here decides what runs: a complete message goes to the same signed path as on
// every other link.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Wipe.h"

namespace Iface {

// A request frame, inside a native anonymous-request datagram after its 4-byte tag: marker, xid, index, count, bytes. The
// marker is a control character that no native anonymous request uses (a password starts at 0x20 or is empty, and the
// typed requests are 1 to 3), so neither can be read as the other.
const uint8_t LORA_MARKER = 0x1D;
const size_t LORA_HDR = 4;
const size_t LORA_ANON_TAG = 4;
const size_t LORA_REQ_PAYLOAD = 128;     // what an anonymous datagram takes (136), less the tag and the header
// A reply frame, in a native response datagram: the magic so a native response is never read as one, xid, index, count, bytes.
const uint8_t LORA_REPLY_MAGIC[4] = {0x1D, 'M', 'C', '1'};
const size_t LORA_REPLY_HDR = 7;
const size_t LORA_REPLY_PAYLOAD = 150;
const size_t LORA_MSG_MAX = 1024;
const size_t LORA_CIPHER_BLOCK = 16;
const unsigned LORA_MAX_FRAMES = 8;
const int LORA_PEERS = 2;
const uint32_t LORA_REASSEMBLY_MS = 60000;
const uint32_t LORA_REPLY_MS = 10UL * 60 * 1000;
const uint32_t LORA_SESSION_MS = 15UL * 60 * 1000;
const uint32_t LORA_FRAME_GAP_MS = 800;
const uint32_t LORA_UNVERIFIED_PER_MIN = 4;

class LoraLink {
public:
  enum : uint8_t { PATH_UNKNOWN = 0xFF };
  struct Peer {
    uint8_t key[32];
    uint8_t secret[32];
    uint8_t path[64];
    uint8_t path_len;     // PATH_UNKNOWN when the way back is not known
    bool used;
    uint32_t last_ms;
    // the message being reassembled
    uint8_t* rx;
    size_t rx_cap;
    uint8_t xid, count, have;
    size_t last_len;
    uint32_t first_ms;
    bool complete;
    // the reply being sent
    uint8_t* tx;
    size_t tx_len, tx_off;
    uint8_t tx_xid, tx_idx, tx_count;
    bool tx_flood_ok;
    uint32_t tx_deadline;
  };
  struct Counts {
    uint32_t frames, dropped_bad, dropped_budget, dropped_deadline, dropped_memory, evicted;
  };
  enum Feed { F_IGNORED, F_PARTIAL, F_COMPLETE, F_NOMEM };
  typedef bool (*SendFn)(void* user, const Peer& p, const uint8_t* frame, size_t n, bool flood_ok);

  LoraLink() { memset(_p, 0, sizeof(_p)); memset(&_c, 0, sizeof(_c)); }
  ~LoraLink() { for (Peer& p : _p) clear(p); }
  LoraLink(const LoraLink&) = delete;
  LoraLink& operator=(const LoraLink&) = delete;

  const Counts& counts() const { return _c; }

  // One frame from this peer, without any tag: marker, xid, index, count, bytes. payload is the size every frame but the last
  // must have. A frame of another message from the same peer discards the incomplete one: parts of two messages never combine.
  Feed feed(const uint8_t key[32], const uint8_t secret[32], const uint8_t* path, uint8_t path_len, const uint8_t* f, size_t n,
            size_t payload, uint32_t now) {
    if (n <= LORA_HDR || f[0] != LORA_MARKER) return F_IGNORED;
    const uint8_t xid = f[1], idx = f[2], cnt = f[3];
    size_t blen = n - LORA_HDR;
    // The datagram's encryption pads to whole blocks with zeros: a full frame reads longer than it is, and a last one has
    // zeros after its text, which holds no NUL.
    if (cnt != 0 && idx + 1 == cnt) { while (blen && f[LORA_HDR + blen - 1] == 0) blen--; }
    else if (blen >= payload && blen < payload + LORA_CIPHER_BLOCK) blen = payload;
    // before anything is reserved
    if (cnt == 0 || cnt > LORA_MAX_FRAMES || idx >= cnt || blen == 0 || blen > payload || (idx + 1 < cnt && blen != payload) ||
        (size_t)(cnt - 1) * payload + blen > LORA_MSG_MAX) {
      _c.dropped_bad++;
      return F_IGNORED;
    }
    _c.frames++;
    Peer* p = peerFor(key, now);
    memcpy(p->secret, secret, 32);
    p->path_len = path_len;
    if (path_len != PATH_UNKNOWN && path_len <= sizeof(p->path)) memcpy(p->path, path, path_len);
    p->last_ms = now;
    if (p->complete) return F_IGNORED;   // one message in hand: the retry comes after its answer
    if (p->rx && (p->xid != xid || p->count != cnt)) freeRx(*p);
    if (!p->rx) {
      p->rx_cap = cnt * payload + 1;
      p->rx = static_cast<uint8_t*>(malloc(p->rx_cap));
      if (!p->rx) { _c.dropped_memory++; return F_NOMEM; }
      p->xid = xid; p->count = cnt; p->have = 0; p->first_ms = now; p->last_len = 0;
    }
    const uint8_t bit = (uint8_t)(1u << idx);
    if (p->have & bit) return F_PARTIAL;   // heard twice: a flood or a retry costs nothing
    memcpy(p->rx + idx * payload, f + LORA_HDR, blen);
    p->have |= bit;
    if (idx + 1 == cnt) p->last_len = blen;
    if (p->have != fullMask(cnt)) return F_PARTIAL;
    p->last_len = (cnt - 1) * payload + p->last_len;
    p->rx[p->last_len] = 0;
    p->complete = true;
    return F_COMPLETE;
  }

  // The next message ready to run, or null. Once answered, release() frees it.
  Peer* nextComplete() {
    for (Peer& p : _p) if (p.used && p.complete && !p.tx) return &p;
    return nullptr;
  }
  static const char* message(const Peer& p) { return reinterpret_cast<const char*>(p.rx); }
  static size_t messageLen(const Peer& p) { return p.last_len; }
  void release(Peer& p) { freeRx(p); }

  // The answer to the message in hand. A verified one may flood if no way back is known. An unverified one goes only on a
  // known way back, within a budget shared by all peers: keys are free, so a per-peer budget would not hold.
  bool queueReply(Peer& p, const char* msg, size_t len, bool verified, uint32_t now) {
    if (len == 0 || len > LORA_MSG_MAX) { _c.dropped_bad++; return false; }
    if (!verified && (p.path_len == PATH_UNKNOWN || !takeBudget(now))) { _c.dropped_budget++; return false; }
    freeTx(p);
    p.tx = static_cast<uint8_t*>(malloc(len));
    if (!p.tx) { _c.dropped_memory++; return false; }
    memcpy(p.tx, msg, len);
    p.tx_len = len; p.tx_off = 0; p.tx_xid = p.xid; p.tx_idx = 0;
    p.tx_count = (uint8_t)((len + LORA_REPLY_PAYLOAD - 1) / LORA_REPLY_PAYLOAD);
    p.tx_flood_ok = verified;
    p.tx_deadline = now + LORA_REPLY_MS;
    _next_send = now;
    return true;
  }

  // Deadlines, and at most one frame of a reply on this pass. A frame the send cannot take now stays for the next pass.
  void tick(uint32_t now, SendFn send, void* user) {
    for (Peer& p : _p) {
      if (!p.used) continue;
      if (p.rx && !p.complete && (int32_t)(now - p.first_ms) >= (int32_t)LORA_REASSEMBLY_MS) partsMissing(p, now);
      if (p.tx && (int32_t)(now - p.tx_deadline) >= 0) { _c.dropped_deadline++; freeTx(p); }
      if (!p.tx && !p.rx && (int32_t)(now - p.last_ms) >= (int32_t)LORA_SESSION_MS) clear(p);
    }
    if ((int32_t)(now - _next_send) < 0) return;
    for (int k = 0; k < LORA_PEERS; k++) {   // every frame of a reply is one message: they never interleave on one peer
      Peer& p = _p[k];
      if (!p.used || !p.tx) continue;
      uint8_t f[LORA_REPLY_HDR + LORA_REPLY_PAYLOAD];
      const size_t take = p.tx_len - p.tx_off < LORA_REPLY_PAYLOAD ? p.tx_len - p.tx_off : LORA_REPLY_PAYLOAD;
      memcpy(f, LORA_REPLY_MAGIC, 4);
      f[4] = p.tx_xid; f[5] = p.tx_idx; f[6] = p.tx_count;
      memcpy(f + LORA_REPLY_HDR, p.tx + p.tx_off, take);
      if (!send(user, p, f, LORA_REPLY_HDR + take, p.tx_flood_ok)) return;
      p.tx_off += take;
      p.tx_idx++;
      if (p.tx_off >= p.tx_len) freeTx(p);
      _next_send = now + LORA_FRAME_GAP_MS;
      return;
    }
  }

  bool takeBudget(uint32_t now) {
    if (!_tok_init) { _tok_init = true; _tok = LORA_UNVERIFIED_PER_MIN * 15000; _tok_ms = now; }
    uint32_t dt = now - _tok_ms;
    _tok_ms = now;
    // thousandths of a token: dt ms at 4 a minute is dt/15 of them; capped at one minute's worth
    const uint32_t cap = LORA_UNVERIFIED_PER_MIN * 15000;
    const uint32_t add = dt > 60000 ? cap : dt * LORA_UNVERIFIED_PER_MIN / 4;
    _tok = _tok + add > cap ? cap : _tok + add;
    if (_tok < 15000) return false;
    _tok -= 15000;
    return true;
  }

private:
  Peer _p[LORA_PEERS];
  Counts _c;
  uint32_t _next_send = 0, _tok = 0, _tok_ms = 0;
  bool _tok_init = false;

  static uint8_t fullMask(uint8_t cnt) { return cnt >= 8 ? 0xFF : (uint8_t)((1u << cnt) - 1); }

  Peer* peerFor(const uint8_t key[32], uint32_t now) {
    Peer* oldest = nullptr;
    for (Peer& p : _p) {
      if (p.used && memcmp(p.key, key, 32) == 0) return &p;
      if (!p.used) { if (!oldest || oldest->used) oldest = &p; }
      else if (!oldest || (oldest->used && (int32_t)(p.last_ms - oldest->last_ms) < 0)) oldest = &p;
    }
    if (oldest->used) _c.evicted++;   // its results stay cached or owed: the manager asks with job.get
    clear(*oldest);
    oldest->used = true;
    memcpy(oldest->key, key, 32);
    oldest->path_len = PATH_UNKNOWN;
    oldest->last_ms = now;
    return oldest;
  }

  // A message incomplete at its deadline: the sender hears which frames arrived, so it re-sends only those.
  void partsMissing(Peer& p, uint32_t now) {
    char m[96];
    const int n = snprintf(m, sizeof(m), "{\"v\":1,\"id\":\"\",\"status\":\"busy\",\"error\":\"parts-missing\",\"xid\":%u,\"have\":%u}",
                           p.xid, p.have);
    const uint8_t xid = p.xid;
    freeRx(p);
    if (n > 0 && (size_t)n < sizeof(m)) {
      p.xid = xid;
      queueReply(p, m, (size_t)n, false, now);
    }
  }

  void freeRx(Peer& p) {
    if (p.rx) { wipe(p.rx, p.rx_cap); free(p.rx); }   // a config.apply carries passwords
    p.rx = nullptr; p.have = 0; p.count = 0; p.complete = false; p.last_len = 0;
  }
  void freeTx(Peer& p) {
    if (p.tx) { wipe(p.tx, p.tx_len); free(p.tx); }
    p.tx = nullptr; p.tx_len = 0; p.tx_off = 0;
  }
  void clear(Peer& p) {
    freeRx(p);
    freeTx(p);
    wipe(&p, sizeof(p));
    p.path_len = PATH_UNKNOWN;
  }
};

// What a request may run over this link: an operation stays off LoRa until it is named here (LORA_MANAGEMENT.md section 2).
// No secret or sensitive operation is ever named: key material never goes on the air, and there is no nonce handshake.
inline bool loraOpAllowed(const char* op) {
  static const char* const OPS[] = {"hello", "status.get", "config.schema", "config.get", "config.apply", "deployment.get",
                                    "job.get", "job.list", "job.ack", "job.cancel", "job.confirm", "posture.set"};
  for (const char* o : OPS) if (strcmp(o, op) == 0) return true;
  return false;
}

// Where the mesh hands a frame: set by the role's interface. secret is the pairwise secret the datagram was decrypted with.
struct LoraSink {
  void (*fn)(void* user, const uint8_t* key, const uint8_t* secret, const uint8_t* path, uint8_t path_len, const uint8_t* frame,
             size_t n) = nullptr;
  void* user = nullptr;
};
inline LoraSink& loraSink() {
  static LoraSink s;
  return s;
}

// The way back from a received datagram's route, as a direct path, or PATH_UNKNOWN: a flood's hops reversed, none for a neighbour.
inline uint8_t loraReplyPath(bool flood, uint8_t hash_size, uint8_t hops, const uint8_t* path, uint8_t* out, size_t cap) {
  if (hops == 0) return 0;
  if (!flood || (size_t)hops * hash_size > cap) return LoraLink::PATH_UNKNOWN;
  for (uint8_t i = 0; i < hops; i++) memcpy(out + (size_t)i * hash_size, path + (size_t)(hops - 1 - i) * hash_size, hash_size);
  return (uint8_t)(((hash_size - 1) << 6) | hops);
}

}  // namespace Iface
