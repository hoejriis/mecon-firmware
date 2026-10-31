#pragma once
// The security floor kept in one flash page outside every file system (docs/contract/UPDATES.md section 4), for the
// nRF52, which has no key-value store: the page the bootloader keeps across a serial DFU, below the provisioned marker's.
// Header-only and platform-free, so the native tests drive it through a simulated page.
//
// The page is a log of 16-byte slots, written from the start: {magic, floor, ~floor, crc32 of the first 12 bytes}. The
// last valid slot is the floor. Reading fails closed:
//   - an erased page (every byte 0xFF) is a floor of 0: nothing stored yet;
//   - valid slots followed only by erased ones give the last slot's floor;
//   - anything else (a torn write, another firmware's data over the page) cannot be read, and no install passes.
// A raise appends a slot only when it raises; when the page is full it is erased and the slot goes to the start. A power
// cut during that erase leaves the page erased, a floor of 0, until the next boot raises it to the running image's
// compiled version again: the same exposure the ESP32 has between a factory reset's NVS erase and its re-raise.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "helpers/deployment/DepMarkerPage.h"

namespace Upd {

const uint32_t FLOOR_MAGIC = 0x31524C46u;   // "FLR1"
const size_t FLOOR_SLOT = 16;

inline bool floorSlotValid(const uint8_t s[FLOOR_SLOT], uint32_t* floor) {
  uint32_t w[4];
  memcpy(w, s, sizeof(w));
  if (w[0] != FLOOR_MAGIC || w[2] != ~w[1] || w[3] != Dep::markerCrc32(s, 12)) return false;
  *floor = w[1];
  return true;
}

struct FloorScan {
  bool readable;   // false: content this cannot read (fail closed)
  uint32_t floor;  // the last valid slot's, 0 when the page is erased
  size_t next;     // the first erased slot after the valid ones; size() when full
};

inline FloorScan floorPageScan(Dep::FlashPage& p) {
  FloorScan r = {true, 0, 0};
  uint8_t s[FLOOR_SLOT];
  size_t off = 0;
  for (; off + FLOOR_SLOT <= p.size(); off += FLOOR_SLOT) {   // the valid slots
    if (!p.read((uint32_t)off, s, sizeof(s))) { r.readable = false; return r; }
    uint32_t f;
    if (!floorSlotValid(s, &f)) break;
    r.floor = f;
  }
  r.next = off;
  for (; off + FLOOR_SLOT <= p.size(); off += FLOOR_SLOT) {   // then only erased ones
    if (!p.read((uint32_t)off, s, sizeof(s)) || !Dep::markerSlotErased(s)) { r.readable = false; return r; }
  }
  return r;
}

// false: the page cannot be read. An erased page is a floor of 0.
inline bool floorPageRead(Dep::FlashPage& p, uint32_t* out) {
  const FloorScan s = floorPageScan(p);
  *out = s.readable ? s.floor : 0;
  return s.readable;
}

// Stores max(stored, v); true once the page reads back at least v. A page that cannot be read is left as it is.
inline bool floorPageRaise(Dep::FlashPage& p, uint32_t v) {
  FloorScan s = floorPageScan(p);
  if (!s.readable) return false;
  if (s.floor >= v) return true;
  if (s.next + FLOOR_SLOT > p.size()) {
    if (!p.erase()) return false;
    s.next = 0;
  }
  uint32_t w[4] = {FLOOR_MAGIC, v, ~v, 0};
  w[3] = Dep::markerCrc32(reinterpret_cast<const uint8_t*>(w), 12);
  if (!p.write((uint32_t)s.next, w, sizeof(w))) return false;
  uint32_t now;
  return floorPageRead(p, &now) && now >= v;
}

}  // namespace Upd
