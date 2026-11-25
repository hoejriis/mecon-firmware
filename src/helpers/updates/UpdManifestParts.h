#pragma once
// A release manifest sent in parts (docs/contract/LORA_UPDATES.md section 5): a signed stage of the longest manifest is more
// than a LoRa message holds, so update.manifest.part carries it in at most 4 parts. The bytes of the parts, joined in order
// of `part` (1 to `parts`), are the manifest text followed by its 64-byte signature, and `sha256` is the SHA-256 of the
// text, the key the manifest is staged under.
//
// One RAM slot, keyed by `sha256`. A part for another hash, or a part count that changes, discards what is held and
// starts again with the new part; a part already held is replaced. Header-only and platform-free: the native tests drive
// it. The slot's 704 bytes are on the heap, taken with the first part and freed when the slot is done or discarded.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "UpdB64.h"

namespace Upd {

class ManifestParts {
public:
  static const unsigned MAX_PARTS = 4;
  static const size_t PART_MAX = 176;                        // decoded bytes in one part: 4 x 176 = a 640-byte text and its 64-byte signature
  static const size_t TOTAL_MAX = MAX_PARTS * PART_MAX;

  enum Verdict : uint8_t {
    MP_BAD,        // not canonical base64, an empty or over-long part, a part index outside 1..parts, or parts outside 1..4
    MP_NO_MEMORY,
    MP_MORE,       // kept; other parts are still missing
    MP_COMPLETE    // every part is held: join() has the whole
  };

  ~ManifestParts() { discard(); }

  Verdict add(const uint8_t sha256[32], unsigned part, unsigned parts, const char* b64, size_t b64_len) {
    if (parts < 1 || parts > MAX_PARTS || part < 1 || part > parts) return MP_BAD;
    uint8_t bytes[PART_MAX];
    const long n = base64Decode(b64, b64_len, bytes, sizeof(bytes));
    if (n <= 0) return MP_BAD;
    // Another manifest, or another way of cutting this one: what was held is not this one's.
    if (_data && (memcmp(_sha, sha256, 32) != 0 || _parts != parts)) discard();
    if (!_data) {
      _data = static_cast<uint8_t*>(malloc(TOTAL_MAX));
      if (!_data) return MP_NO_MEMORY;
      memcpy(_sha, sha256, 32);
      _parts = (uint8_t)parts;
      _have = 0;
      memset(_len, 0, sizeof(_len));
    }
    memcpy(_data + (part - 1) * PART_MAX, bytes, (size_t)n);
    _len[part - 1] = (uint8_t)n;
    _have |= (uint8_t)(1u << (part - 1));
    return _have == (uint8_t)((1u << _parts) - 1) ? MP_COMPLETE : MP_MORE;
  }

  // The parts held so far, and of how many; both 0 with nothing held.
  unsigned held() const { unsigned c = 0; for (unsigned i = 0; i < MAX_PARTS; i++) c += (_have >> i) & 1u; return c; }
  unsigned parts() const { return _data ? _parts : 0; }
  const uint8_t* sha() const { return _sha; }
  bool open() const { return _data != nullptr; }

  // Every part joined in order into out; the length, or 0 when parts are missing or out does not hold them.
  size_t join(uint8_t* out, size_t cap) const {
    if (!_data || _have != (uint8_t)((1u << _parts) - 1)) return 0;
    size_t total = 0;
    for (unsigned i = 0; i < _parts; i++) total += _len[i];
    if (total > cap) return 0;
    size_t o = 0;
    for (unsigned i = 0; i < _parts; i++) { memcpy(out + o, _data + i * PART_MAX, _len[i]); o += _len[i]; }
    return o;
  }

  void discard() {
    if (_data) { memset(_data, 0, TOTAL_MAX); free(_data); }
    _data = nullptr;
    _have = 0;
    _parts = 0;
  }

private:
  uint8_t* _data = nullptr;
  uint8_t _sha[32] = {};
  uint8_t _parts = 0, _have = 0;
  uint8_t _len[MAX_PARTS] = {};
};

}  // namespace Upd
