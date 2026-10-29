#pragma once
// The commit-or-revert snapshot of a config.apply (docs/contract/OPERATIONS.md section 3.4): the previous value of every
// setting the request changes, kept in its own file while the job awaits confirmation, and replayed to revert.
//
// It holds records, never CLI lines: (kind, key, profile index, value). A revert rebuilds each line from the role's key
// table, so a value set through another route (a raw CLI line over BLE or MQTT) can never become a second command. A
// value with a byte below 0x20 or 0x7F is refused when the snapshot is built. The file is bound to its job by id and
// checked with a CRC; anything else reads as no snapshot.
//
// The values include write-only ones (passwords). The file is device-internal like the prefs that hold the same values,
// is never reported, and is removed when its job ends. The flash file systems are copy-on-write, so removal frees the
// blocks without overwriting them: the bytes may stay in flash until reused, as they do for the prefs.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Jobs.h"
#include "Wipe.h"

namespace Iface {

// The snapshot's file, replaced atomically by the role's file system.
class SnapStore {
public:
  virtual ~SnapStore() {}
  virtual size_t load(uint8_t* buf, size_t cap) = 0;   // 0: no file, or unreadable
  virtual bool save(const uint8_t* buf, size_t len) = 0;
  virtual void discard() = 0;                          // the file and any half-written copy of it
};

class ConfigSnap {
public:
  static constexpr size_t CAP = 2048;              // records; a request that needs more is refused
  static constexpr size_t VALUE_MAX = 64;          // as the longest settings value (a 63-character password)
  static constexpr uint32_t MAGIC = 0x3153434Du;   // "MCS1"
  static constexpr size_t IMAGE_MAX = 4 + 1 + Job::ID_CAP + 2 + CAP + 4;
  enum Kind : uint8_t { R_SET = 1, R_CLEAR = 2 };  // set the key to the value; remove the key's whole profile

  struct Rec {
    uint8_t kind, key, idx;   // key: its position in the role's table; idx: the profile index
    const char* value;        // not terminated; len bytes
    uint8_t len;
  };

  ~ConfigSnap() { clear(); }

  void begin(const char* job) {
    clear();
    const size_t n = strlen(job);
    if (n <= Job::ID_CAP) memcpy(_job, job, n + 1);
  }
  const char* job() const { return _job; }
  int count() const { return _n; }

  // False: no room, or a value no CLI line may carry.
  bool add(uint8_t kind, uint8_t key, uint8_t idx, const char* value) {
    const size_t vl = value ? strlen(value) : 0;
    if (vl > VALUE_MAX || !valueOk(value, vl) || _len + 4 + vl > CAP || !_job[0]) return false;
    _d[_len++] = kind; _d[_len++] = key; _d[_len++] = idx; _d[_len++] = (uint8_t)vl;
    if (vl) memcpy(_d + _len, value, vl);
    _len += vl;
    _n++;
    return true;
  }

  // Whether this record is already there (a profile is captured once).
  bool has(uint8_t kind, uint8_t key, uint8_t idx) const { return find(kind, key, idx); }

  bool rec(int i, Rec* r) const {
    size_t at = 0;
    for (int k = 0; at + 4 <= _len; k++) {
      const uint8_t vl = _d[at + 3];
      if (at + 4 + vl > _len) return false;
      if (k == i) { *r = Rec{_d[at], _d[at + 1], _d[at + 2], reinterpret_cast<const char*>(_d + at + 4), vl}; return true; }
      at += 4 + vl;
    }
    return false;
  }

  size_t serialize(uint8_t* out, size_t cap) const {
    const size_t jl = strlen(_job);
    const size_t n = 4 + 1 + jl + 2 + _len + 4;
    if (!jl || cap < n) return 0;
    size_t at = 0;
    put32(out, at, MAGIC);
    out[at++] = (uint8_t)jl;
    memcpy(out + at, _job, jl); at += jl;
    out[at++] = (uint8_t)(_len & 0xFF); out[at++] = (uint8_t)(_len >> 8);
    memcpy(out + at, _d, _len); at += _len;
    put32(out, at, crc32(out, at));
    return at;
  }

  // The whole image, intact, or false with nothing kept: a short or damaged file is never half trusted.
  bool parse(const uint8_t* in, size_t n) {
    clear();
    if (n < 4 + 1 + 1 + 2 + 4 || get32(in, 0) != MAGIC || get32(in, n - 4) != crc32(in, n - 4)) return false;
    const size_t jl = in[4];
    if (!jl || jl > Job::ID_CAP || 5 + jl + 2 > n - 4) return false;
    const size_t dl = in[5 + jl] | ((size_t)in[6 + jl] << 8);
    if (dl > CAP || 7 + jl + dl != n - 4) return false;
    memcpy(_job, in + 5, jl);
    _job[jl] = 0;
    memcpy(_d, in + 7 + jl, dl);
    _len = dl;
    Rec r;
    for (size_t at = 0; at < _len; _n++) {   // every record well formed, every value one a line may carry
      if (at + 4 > _len || _d[at] < R_SET || _d[at] > R_CLEAR || at + 4 + _d[at + 3] > _len) { clear(); return false; }
      r.value = reinterpret_cast<const char*>(_d + at + 4);
      r.len = _d[at + 3];
      if (r.len > VALUE_MAX || !valueOk(r.value, r.len)) { clear(); return false; }
      at += 4 + r.len;
    }
    return true;
  }

  void clear() {
    Iface::wipe(_d, sizeof(_d));
    _len = 0;
    _n = 0;
    _job[0] = 0;
  }

  static bool valueOk(const char* v, size_t n) {
    for (size_t i = 0; i < n; i++)
      if ((uint8_t)v[i] < 0x20 || (uint8_t)v[i] == 0x7F) return false;
    return true;
  }

private:
  bool find(uint8_t kind, uint8_t key, uint8_t idx) const {
    Rec r;
    for (int i = 0; rec(i, &r); i++)
      if (r.kind == kind && r.key == key && r.idx == idx) return true;
    return false;
  }
  static void put32(uint8_t* o, size_t& at, uint32_t v) {
    for (int i = 0; i < 4; i++) o[at++] = (uint8_t)(v >> (8 * i));
  }
  static uint32_t get32(const uint8_t* p, size_t at) {
    return (uint32_t)p[at] | ((uint32_t)p[at + 1] << 8) | ((uint32_t)p[at + 2] << 16) | ((uint32_t)p[at + 3] << 24);
  }
  static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
      c ^= p[i];
      for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
  }

  char _job[Job::ID_CAP + 1] = {0};
  uint8_t _d[CAP];
  size_t _len = 0;
  int _n = 0;
};

}  // namespace Iface
