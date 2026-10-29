#pragma once
// The last 8 ids of operations that change state, with their stored result bytes, so a duplicate id (a reconnect,
// a retried write, the same request arriving over another transport) gets the stored result and the operation does
// not run twice. One cache for the device, shared by every transport. Reads are not kept: running one again is
// harmless. DEVICE_INTERFACE.md section 3.
//
// Each entry also keeps the operation that produced it, and a duplicate id under another operation is a conflict, never a
// hit: otherwise a caller allowed an open read could fetch a stored write result by naming its id under that read.
// Memory: 8 * (IFACE_CACHE_ENTRY_CAP + 69) = 8 * 453 = 3,624 bytes with the default 384 byte cap (id 33 + op 33 + length
// 2 + flags 1). 384 holds the result of a config.apply of about eight keys with a 32-character id; 128 did not
// hold even a one-key apply (found on v3test), so its retry got result-expired. Override with -D IFACE_CACHE_ENTRY_CAP=n.
// Beside them, an 8-byte hash of each of the last 64 ids stored (512 bytes): a duplicate whose result is gone is
// recognised (EXPIRED) and must not run again. A hash collision can only make a fresh request look expired, never
// run a request twice.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef IFACE_CACHE_ENTRY_CAP
#define IFACE_CACHE_ENTRY_CAP 384
#endif

namespace Iface {

class ResultCache {
public:
  static constexpr int SLOTS = 8;
  static constexpr size_t ENTRY_CAP = IFACE_CACHE_ENTRY_CAP;
  static constexpr int HASHES = 64;
  enum Lookup { MISS, HIT, EXPIRED, CONFLICT };
  static constexpr size_t OP_CAP = 32;

  ResultCache() { clear(); }

  // HIT: *data / *len point at the stored result. EXPIRED: the id ran but its result is no longer kept (evicted, or
  // too big to keep), so it must not run again. CONFLICT: the id is held by another operation's result, which this
  // request must neither get nor overwrite.
  Lookup find(const char* id, const char* op, const char** data, size_t* len) const {
    for (int i = 0; i < SLOTS; i++) {
      if (!_e[i].used || strcmp(_e[i].id, id) != 0) continue;
      if (strcmp(_e[i].op, op) != 0) return CONFLICT;
      if (_e[i].oversize) return EXPIRED;
      *data = _e[i].data; *len = _e[i].len;
      return HIT;
    }
    const uint64_t h = hash(id);
    for (int i = 0; i < _nh; i++)
      if (_h[i] == h) return EXPIRED;
    return MISS;
  }

  // Oldest entry is replaced first. An id already present is overwritten in place.
  void store(const char* id, const char* op, const char* data, size_t len) {
    if (strlen(id) > 32 || strlen(op) > OP_CAP) return;
    int slot = -1;
    for (int i = 0; i < SLOTS && slot < 0; i++)
      if (_e[i].used && strcmp(_e[i].id, id) == 0) slot = i;
    if (slot < 0) {
      slot = _next; _next = (_next + 1) % SLOTS;
      _h[_hnext] = hash(id); _hnext = (_hnext + 1) % HASHES; if (_nh < HASHES) _nh++;
    }
    Entry& e = _e[slot];
    e.used = true;
    strcpy(e.id, id);
    strcpy(e.op, op);
    e.oversize = len > ENTRY_CAP;
    e.len = e.oversize ? 0 : (uint16_t)len;
    if (!e.oversize) memcpy(e.data, data, len);
  }

  void clear() { memset(_e, 0, sizeof(_e)); memset(_h, 0, sizeof(_h)); _next = 0; _hnext = 0; _nh = 0; }

private:
  struct Entry {
    char id[33];
    char op[OP_CAP + 1];
    bool used, oversize;
    uint16_t len;
    char data[IFACE_CACHE_ENTRY_CAP];
  };
  static uint64_t hash(const char* s) {   // FNV-1a
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
    return h;
  }
  Entry _e[SLOTS];
  int _next;
  uint64_t _h[HASHES];
  int _hnext, _nh;
};

}  // namespace Iface
