#pragma once
// Strict standard base64 (RFC 4648 section 4, with padding) for update.chunk's data. Canonical only: the length is a
// multiple of 4, "=" appears only as the final padding, and the bits the padding leaves over are zero, so no two strings
// decode to the same bytes. Header-only and platform-free: the native tests drive it.
#include <stddef.h>
#include <stdint.h>

namespace Upd {

inline int b64v(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// The number of bytes written to out, or -1 for anything that is not canonical base64 or does not fit cap.
inline long base64Decode(const char* s, size_t len, uint8_t* out, size_t cap) {
  if (!s || len % 4 != 0) return -1;
  size_t pad = 0;
  if (len && s[len - 1] == '=') pad = (len >= 2 && s[len - 2] == '=') ? 2 : 1;
  const size_t n = len / 4 * 3 - pad;
  if (n > cap) return -1;
  size_t o = 0;
  for (size_t i = 0; i < len; i += 4) {
    const bool last = i + 4 == len;
    int v[4];
    for (int k = 0; k < 4; k++) {
      const bool padded = last && k >= 4 - (int)pad;
      v[k] = padded ? (s[i + k] == '=' ? 0 : -1) : b64v(s[i + k]);
      if (v[k] < 0) return -1;
    }
    const uint32_t g = (uint32_t)v[0] << 18 | (uint32_t)v[1] << 12 | (uint32_t)v[2] << 6 | (uint32_t)v[3];
    if (last && pad == 2 && (g & 0xFFFF) != 0) return -1;   // the leftover bits of "xx==" are zero
    if (last && pad == 1 && (g & 0xFF) != 0) return -1;     // and of "xxx="
    out[o++] = (uint8_t)(g >> 16);
    if (!(last && pad == 2)) out[o++] = (uint8_t)(g >> 8);
    if (!(last && pad >= 1)) out[o++] = (uint8_t)g;
  }
  return (long)o;
}

}  // namespace Upd
