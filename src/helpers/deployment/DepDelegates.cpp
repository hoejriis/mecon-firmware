#include "DepDelegates.h"
#include <string.h>
#include "DepHmac.h"

namespace Dep {

namespace {

const uint8_t MAGIC[4] = {'M', 'D', 'G', '1'};

void derive(const uint8_t key[DLG_KEY], const char* label, uint8_t out[32]) {
  hmacSha256(key, DLG_KEY, reinterpret_cast<const uint8_t*>(label), strlen(label), out);
}

void crypt(const uint8_t k_enc[32], const uint8_t nonce[DLG_NONCE], const uint8_t* in, uint8_t* out, size_t n) {
  uint8_t msg[DLG_NONCE + 4], ks[32];
  memcpy(msg, nonce, DLG_NONCE);
  for (size_t off = 0, i = 0; off < n; off += 32, i++) {
    msg[DLG_NONCE] = (uint8_t)(i >> 24); msg[DLG_NONCE + 1] = (uint8_t)(i >> 16);
    msg[DLG_NONCE + 2] = (uint8_t)(i >> 8); msg[DLG_NONCE + 3] = (uint8_t)i;
    hmacSha256(k_enc, 32, msg, sizeof(msg), ks);
    for (size_t j = 0; j < 32 && off + j < n; j++) out[off + j] = in[off + j] ^ ks[j];
  }
  memset(ks, 0, sizeof(ks));
}

}  // namespace

size_t dlgSeal(const uint8_t key[DLG_KEY], const uint8_t nonce[DLG_NONCE], const uint8_t* plain, size_t n, uint8_t* out,
               size_t cap) {
  if (!plain || n == 0 || n > DLG_PLAIN_MAX || n % DLG_REC != 0 || cap < DLG_HDR + n + DLG_TAG) return 0;
  uint8_t k_enc[32], k_mac[32];
  derive(key, "mecon delegates enc v1", k_enc);
  derive(key, "mecon delegates mac v1", k_mac);
  memcpy(out, MAGIC, 4);
  out[4] = (uint8_t)n; out[5] = (uint8_t)(n >> 8);
  memcpy(out + 6, nonce, DLG_NONCE);
  crypt(k_enc, nonce, plain, out + DLG_HDR, n);
  hmacSha256(k_mac, 32, out, DLG_HDR + n, out + DLG_HDR + n);   // the header too: the length cannot move
  memset(k_enc, 0, sizeof(k_enc));
  memset(k_mac, 0, sizeof(k_mac));
  return DLG_HDR + n + DLG_TAG;
}

bool dlgOpen(const uint8_t key[DLG_KEY], const uint8_t* in, size_t len, uint8_t* plain, size_t cap, size_t* n) {
  if (!in || len < DLG_HDR + DLG_REC + DLG_TAG || memcmp(in, MAGIC, 4) != 0) return false;
  const size_t m = (size_t)in[4] | (size_t)in[5] << 8;
  if (m == 0 || m > DLG_PLAIN_MAX || m % DLG_REC != 0 || len != DLG_HDR + m + DLG_TAG || m > cap) return false;
  uint8_t k_enc[32], k_mac[32], tag[32];
  derive(key, "mecon delegates mac v1", k_mac);
  hmacSha256(k_mac, 32, in, DLG_HDR + m, tag);
  const bool ok = constTimeEqual(tag, in + DLG_HDR + m, DLG_TAG);
  memset(k_mac, 0, sizeof(k_mac));
  memset(tag, 0, sizeof(tag));
  if (!ok) return false;
  derive(key, "mecon delegates enc v1", k_enc);
  crypt(k_enc, in + 6, in + DLG_HDR, plain, m);
  memset(k_enc, 0, sizeof(k_enc));
  *n = m;
  return true;
}

}  // namespace Dep
