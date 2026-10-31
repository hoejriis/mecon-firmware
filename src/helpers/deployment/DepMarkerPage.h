#pragma once
// The provisioned marker kept in one flash page outside every file system (docs/contract/AUTHORITY.md section 4), for a
// platform with no key-value store (the nRF52). Header-only and platform-free, so the native tests drive it through a
// simulated page.
//
// The page is a log of 16-byte slots, written from the start: {magic, state, ~state, crc32 of the first 12 bytes}. The
// last valid slot is the state. Reading fails closed:
//   - an erased page (every byte 0xFF) is MK_ABSENT;
//   - valid slots followed only by erased ones give the last slot's state;
//   - anything else (a torn write, another firmware's data over the page after a downgrade) is MK_PRESENT.
// A write appends a slot. When the page is full, or holds anything invalid, it is erased first and the slot goes to the
// start. Every write is read back. A power cut during that erase leaves the page erased (absent). That is safe because
// every write is either a set made before the record it guards is committed, or a clear, which ends absent anyway.
// 256 slots a page: one erase per 256 changes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "DepMarker.h"

namespace Dep {

// One erase unit of flash. write() may only clear bits (flash programming); offsets and lengths are multiples of 4.
class FlashPage {
public:
  virtual ~FlashPage() {}
  virtual size_t size() const = 0;
  virtual bool read(uint32_t off, void* buf, size_t n) = 0;
  virtual bool write(uint32_t off, const void* buf, size_t n) = 0;
  virtual bool erase() = 0;
};

const uint32_t MARKER_MAGIC = 0x31524B4Du;    // "MKR1"
const uint32_t MARKER_SET = 0x564F5250u;      // "PROV"
const uint32_t MARKER_CLEAR = 0x52524C43u;    // "CLRR"
const size_t MARKER_SLOT = 16;

inline uint32_t markerCrc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

struct MarkerScan {
  MarkerState state;   // as read (fail closed)
  size_t next;         // offset of the first erased slot after the valid ones; size() when full
  bool clean;          // only valid slots, then only erased ones: a slot may be appended at next
};

inline bool markerSlotErased(const uint8_t s[MARKER_SLOT]) {
  for (size_t i = 0; i < MARKER_SLOT; i++)
    if (s[i] != 0xFF) return false;
  return true;
}

inline bool markerSlotValid(const uint8_t s[MARKER_SLOT], uint32_t* state) {
  uint32_t w[4];
  memcpy(w, s, sizeof(w));
  if (w[0] != MARKER_MAGIC || (w[1] != MARKER_SET && w[1] != MARKER_CLEAR) || w[2] != ~w[1]) return false;
  if (w[3] != markerCrc32(s, 12)) return false;
  *state = w[1];
  return true;
}

inline MarkerScan markerScan(FlashPage& p) {
  MarkerScan r = {MK_ABSENT, 0, true};
  uint8_t s[MARKER_SLOT];
  bool any = false, ended = false;
  for (size_t off = 0; off + MARKER_SLOT <= p.size(); off += MARKER_SLOT) {
    if (!p.read((uint32_t)off, s, MARKER_SLOT)) return {MK_PRESENT, p.size(), false};   // unreadable: fail closed
    if (markerSlotErased(s)) {
      if (!ended) { ended = true; r.next = off; }
      continue;
    }
    uint32_t st;
    if (ended || !markerSlotValid(s, &st)) return {MK_PRESENT, p.size(), false};   // data after the end, or not ours
    any = true;
    r.state = st == MARKER_SET ? MK_PRESENT : MK_ABSENT;
  }
  if (!ended) r.next = p.size();   // every slot used
  if (!any) r.state = MK_ABSENT;
  return r;
}

inline MarkerState markerPageRead(FlashPage& p) { return markerScan(p).state; }

// Appends the new state (erasing first when the page is full or not clean) and reads it back.
inline bool markerPageWrite(FlashPage& p, bool present) {
  MarkerScan sc = markerScan(p);
  if (sc.clean && sc.state == (present ? MK_PRESENT : MK_ABSENT) && (sc.next > 0 || !present)) return true;   // already so
  if (!sc.clean || sc.next + MARKER_SLOT > p.size()) {
    if (!p.erase()) return false;
    sc.next = 0;
  }
  uint32_t w[4] = {MARKER_MAGIC, present ? MARKER_SET : MARKER_CLEAR, 0, 0};
  w[2] = ~w[1];
  w[3] = markerCrc32(reinterpret_cast<const uint8_t*>(w), 12);
  if (!p.write((uint32_t)sc.next, w, sizeof(w))) return false;
  return markerPageRead(p) == (present ? MK_PRESENT : MK_ABSENT);
}

}  // namespace Dep
