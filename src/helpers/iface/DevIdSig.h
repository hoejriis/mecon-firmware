#pragma once
// The signed device_id binding (docs/contract/AUTHORITY.md section 3.5): the node key's signature over
//
//   "MECON-DEVID-v1" 0x00 <device_id, 16 bytes> <node key, 32 bytes> [<nonce bytes>]
//
// so a Backend that verifies it against the node key it holds for a login knows that key asserts this device_id. It
// binds the key, so a replay under another login fails; the optional nonce (1 to 32 of [0-9A-Za-z._-]) proves the key
// is held now. Sent by `get device.id [nonce]` and in `hello` (device_id_sig, and device_id_sig_nonce for args.nonce),
// enrolled or not. The native signing frame refuses data that starts with "MECON-", so a host cannot mint one.
// Header-only with no dependencies, like DeviceId.h, so the CLI of every role can include it.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Iface {

const char DEVID_DOMAIN[] = "MECON-DEVID-v1";
const size_t DEVID_NONCE_MAX = 32;
// The signed bytes for the longest nonce: domain and its NUL, id, key, nonce.
const size_t DEVID_MSG_MAX = sizeof(DEVID_DOMAIN) + 16 + 32 + DEVID_NONCE_MAX;

// True for 1 to DEVID_NONCE_MAX characters of [0-9A-Za-z._-]; len excludes any terminator.
inline bool devIdNonceValid(const char* nonce, size_t len) {
  if (!nonce || len < 1 || len > DEVID_NONCE_MAX) return false;
  for (size_t i = 0; i < len; i++) {
    const char c = nonce[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'))
      return false;
  }
  return true;
}

// Writes the signed bytes into out (DEVID_MSG_MAX bytes) and returns their length, or 0 when device_id_hex is not
// 32 hex characters or the nonce (null or "" for none) is not valid.
inline size_t devIdSignedBytes(uint8_t* out, const char* device_id_hex, const uint8_t node_key[32], const char* nonce) {
  if (!device_id_hex || strlen(device_id_hex) != 32) return 0;
  const size_t nl = nonce ? strlen(nonce) : 0;
  if (nl && !devIdNonceValid(nonce, nl)) return 0;
  size_t n = sizeof(DEVID_DOMAIN);   // with its NUL
  memcpy(out, DEVID_DOMAIN, n);
  for (int i = 0; i < 16; i++) {
    uint8_t v = 0;
    for (int j = 0; j < 2; j++) {
      const char c = device_id_hex[2 * i + j];
      uint8_t d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return 0;
      v = (uint8_t)(v << 4 | d);
    }
    out[n++] = v;
  }
  memcpy(out + n, node_key, 32);
  n += 32;
  memcpy(out + n, nonce, nl);
  return n + nl;
}

// Standard base64 with padding of a 64-byte signature: 88 characters and a NUL.
inline void devIdBase64(char out[89], const uint8_t sig[64]) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < 64; i += 3) {
    const uint32_t v = (uint32_t)sig[i] << 16 | (i + 1 < 64 ? (uint32_t)sig[i + 1] << 8 : 0) | (i + 2 < 64 ? sig[i + 2] : 0);
    out[o++] = T[v >> 18 & 63];
    out[o++] = T[v >> 12 & 63];
    out[o++] = i + 1 < 64 ? T[v >> 6 & 63] : '=';
    out[o++] = i + 2 < 64 ? T[v & 63] : '=';
  }
  out[o] = 0;
}

// The signature into out (89 bytes), signed by id (a mesh::LocalIdentity: its pub_key and sign()). False, with out
// empty, when the bytes could not be built (no device_id yet, or a bad nonce).
template <class Id>
bool signDeviceId(char out[89], const Id& id, const char* device_id_hex, const char* nonce) {
  uint8_t msg[DEVID_MSG_MAX];
  out[0] = 0;
  const size_t n = devIdSignedBytes(msg, device_id_hex, id.pub_key, nonce);
  if (!n) return false;
  uint8_t sig[64];
  id.sign(sig, msg, (int)n);
  devIdBase64(out, sig);
  return true;
}

}  // namespace Iface
