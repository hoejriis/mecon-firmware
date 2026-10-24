#pragma once
// The last 16 request ids of one transport with their stored result bytes, so a duplicate id (a reconnect, a
// retried write) gets the stored result and the operation does not run twice. DEVICE_INTERFACE.md section 3.
//
// Memory per transport: 16 * (IFACE_CACHE_ENTRY_CAP + 36) = 16 * 292 = 4,672 bytes with the default 256 byte
// cap (id 33 + length 2 + flags 1, rounded). 256 holds an ok/failed result for a handful of keys; bigger results
// are the paged operations' and are rarely a retried write. Override with -D IFACE_CACHE_ENTRY_CAP=n.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef IFACE_CACHE_ENTRY_CAP
#define IFACE_CACHE_ENTRY_CAP 256
#endif

namespace Iface {

class ResultCache {
public:
  static constexpr int SLOTS = 16;
  static constexpr size_t ENTRY_CAP = IFACE_CACHE_ENTRY_CAP;
  enum Lookup { MISS, HIT, OVERSIZE };

  ResultCache() : _next(0) { memset(_e, 0, sizeof(_e)); }

  // HIT: *data / *len point at the stored result. OVERSIZE: the id ran but its result was too big to keep,
  // so it must not run again and the caller answers busy.
  Lookup find(const char* id, const char** data, size_t* len) const {
    for (int i = 0; i < SLOTS; i++) {
      if (!_e[i].used || strcmp(_e[i].id, id) != 0) continue;
      if (_e[i].oversize) return OVERSIZE;
      *data = _e[i].data; *len = _e[i].len;
      return HIT;
    }
    return MISS;
  }

  // Oldest entry is replaced first. An id already present is overwritten in place.
  void store(const char* id, const char* data, size_t len) {
    if (strlen(id) > 32) return;
    int slot = -1;
    for (int i = 0; i < SLOTS && slot < 0; i++)
      if (_e[i].used && strcmp(_e[i].id, id) == 0) slot = i;
    if (slot < 0) { slot = _next; _next = (_next + 1) % SLOTS; }
    Entry& e = _e[slot];
    e.used = true;
    strcpy(e.id, id);
    e.oversize = len > ENTRY_CAP;
    e.len = e.oversize ? 0 : (uint16_t)len;
    if (!e.oversize) memcpy(e.data, data, len);
  }

  void clear() { memset(_e, 0, sizeof(_e)); _next = 0; }

private:
  struct Entry {
    char id[33];
    bool used, oversize;
    uint16_t len;
    char data[IFACE_CACHE_ENTRY_CAP];
  };
  Entry _e[SLOTS];
  int _next;
};

}  // namespace Iface
