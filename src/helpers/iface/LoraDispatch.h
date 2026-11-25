#pragma once
// Where a LoRa datagram goes, and what bulk frames cost (docs/contract/LORA_UPDATES.md sections 6 and 9). Pure logic, so the
// native unit tests run it: the radio, the mesh and the transfer are the caller's.
//  - the dispatch split: a point-to-point datagram whose payload starts with BULK_MARKER is a bulk frame for the update
//    transfer, never a request frame; a bulk frame on the management channel (a group datagram) is dropped and counted;
//  - the padding a datagram's encryption adds to a frame (whole 16-byte blocks of zeros), taken off before a bulk frame's length
//    is checked;
//  - the peer a transfer answers: where its last accepted frame came from;
//  - the gateway's budget: bulk may spend 80% of the duty window, the rest is the reserve control traffic always has.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "LoraBulk.h"
#include "LoraLink.h"

namespace Iface {

enum LoraRoute : uint8_t {
  ROUTE_REQUEST,           // a request frame (LoraLink reassembles it)
  ROUTE_BULK,              // a bulk frame, point to point: the transfer
  ROUTE_DROP_GROUP_BULK,   // a bulk frame on the management channel: dropped and counted
  ROUTE_OTHER,             // anything else: today's path
};

// `channel`: the frame came on the management channel (a group datagram), which carries requests and no image bytes.
inline LoraRoute loraRoute(const uint8_t* f, size_t n, bool channel) {
  if (!f || n == 0) return ROUTE_OTHER;
  if (f[0] == BULK_MARKER) return channel ? ROUTE_DROP_GROUP_BULK : ROUTE_BULK;
  if (f[0] == LORA_MARKER) return ROUTE_REQUEST;
  return ROUTE_OTHER;
}

// A datagram's encryption pads the plaintext (the 4-byte tag and the frame) to whole 16-byte blocks of zeros, so a frame is heard
// as long as its padded size less the tag. Returns the length a bulk frame of this transfer really has: `n` when it is already
// exact or is not a bulk frame, the frame's own length when `n` is exactly its padded size and the extra bytes are zero.
// Anything else stays as heard, and the length check drops it.
inline size_t bulkTrimPadding(uint32_t transfer_size, const uint8_t* f, size_t n) {
  if (n < BULK_HDR + 1 + BULK_TAG || f[0] != BULK_MARKER) return n;
  const uint32_t idx = (uint32_t)f[5] | (uint32_t)f[6] << 8 | (uint32_t)f[7] << 16 | (uint32_t)f[8] << 24;
  const size_t d = bulkFrameDataLen(transfer_size, idx);
  if (d == 0) return n;
  const size_t want = BULK_HDR + d + BULK_TAG;
  const size_t padded = (LORA_ANON_TAG + want + LORA_CIPHER_BLOCK - 1) / LORA_CIPHER_BLOCK * LORA_CIPHER_BLOCK - LORA_ANON_TAG;
  if (n != padded || n == want) return n;
  for (size_t i = want; i < n; i++) if (f[i]) return n;
  return want;
}

// A block answer heard as a reply datagram: the 22 bytes, whole, or null. The reply's padding (to 32 bytes) is zeros.
inline bool answerHeard(const uint8_t* d, size_t n) {
  if (!d || n < ANSWER_LEN || n >= ANSWER_LEN + LORA_CIPHER_BLOCK || d[0] != ANSWER_MARKER) return false;
  for (size_t i = ANSWER_LEN; i < n; i++) if (d[i]) return false;
  return true;
}

// Where the transfer's frames come from, and so where its answers go: the point-to-point path a LoRa management reply takes.
struct BulkPeer {
  uint8_t key[32];
  uint8_t secret[32];
  uint8_t path[64];
  uint8_t path_len;
  bool valid;
  void set(const uint8_t* k, const uint8_t* s, const uint8_t* p, uint8_t plen) {
    memcpy(key, k, 32);
    memcpy(secret, s, 32);
    path_len = plen;
    if (plen != LoraLink::PATH_UNKNOWN && plen <= sizeof(path)) memcpy(path, p, plen);
    else path_len = LoraLink::PATH_UNKNOWN;
    valid = true;
  }
  void clear() {
    volatile uint8_t* b = reinterpret_cast<volatile uint8_t*>(this);
    for (size_t i = 0; i < sizeof(*this); i++) b[i] = 0;
  }
};

// ---- the gateway's budget

const uint32_t BULK_CEILING_PCT = 80;     // bulk stops at this share of the duty window; the rest is the control reserve
const unsigned BULK_QUEUE_MAX = 16;       // frames waiting at the gateway: two calls of a full block
const uint32_t BULK_QUEUE_MS = 10u * 60 * 1000;   // frames that wait this long without one going out are dropped

inline uint32_t bulkCeilingMs(uint32_t limit_ms) { return (uint32_t)((uint64_t)limit_ms * BULK_CEILING_PCT / 100); }

// What bulk may still spend this hour before the reserve: the ceiling less the trailing airtime (all traffic, bulk or not) and
// less the airtime of the frames already queued. Zero when that is spent.
inline uint32_t bulkBudgetMs(uint32_t limit_ms, uint32_t used_ms, uint32_t queued_ms) {
  const uint64_t spent = (uint64_t)used_ms + queued_ms;
  const uint32_t ceiling = bulkCeilingMs(limit_ms);
  return spent >= ceiling ? 0 : (uint32_t)(ceiling - spent);
}

// The packet a frame of `n` bytes is on the air: the datagram's tag and padding, the sender's key, the MAC, the headers.
inline size_t bulkPacketBytes(size_t n) {
  return 2 + 1 + 32 + 2 + (LORA_ANON_TAG + n + LORA_CIPHER_BLOCK - 1) / LORA_CIPHER_BLOCK * LORA_CIPHER_BLOCK;
}

// Whether `frames` frames of `frame_ms` airtime each fit the budget.
inline bool bulkFits(uint32_t budget_ms, unsigned frames, uint32_t frame_ms) {
  return (uint64_t)frames * frame_ms <= budget_ms;
}

// What an rf.bulk argument frame must be: at most BULK_ROOM bytes, long enough for a header and a tag, marked as bulk.
inline bool bulkFrameShape(const uint8_t* f, size_t n) {
  return n >= BULK_HDR + 1 + BULK_TAG && n <= BULK_ROOM && f[0] == BULK_MARKER;
}

}  // namespace Iface
