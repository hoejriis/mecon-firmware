#include "DepContinuity.h"
#include <string.h>
#include "DepHmac.h"

namespace Dep {

namespace {

const uint8_t MAGIC[4] = {'M', 'C', 'C', '1'};

void derive(const uint8_t key[CONT_KEY], const char* label, uint8_t out[32]) {
  hmacSha256(key, CONT_KEY, reinterpret_cast<const uint8_t*>(label), strlen(label), out);
}

// in XOR the keystream HMAC(k_enc, nonce || counter), block by block.
void crypt(const uint8_t k_enc[32], const uint8_t nonce[CONT_NONCE], const uint8_t* in, uint8_t* out, size_t n) {
  uint8_t msg[CONT_NONCE + 4], ks[32];
  memcpy(msg, nonce, CONT_NONCE);
  for (size_t off = 0, i = 0; off < n; off += 32, i++) {
    msg[CONT_NONCE] = (uint8_t)(i >> 24); msg[CONT_NONCE + 1] = (uint8_t)(i >> 16);
    msg[CONT_NONCE + 2] = (uint8_t)(i >> 8); msg[CONT_NONCE + 3] = (uint8_t)i;
    hmacSha256(k_enc, 32, msg, sizeof(msg), ks);
    for (size_t j = 0; j < 32 && off + j < n; j++) out[off + j] = in[off + j] ^ ks[j];
  }
  memset(ks, 0, sizeof(ks));
}

void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

}  // namespace

size_t contSeal(const uint8_t key[CONT_KEY], const uint8_t nonce[CONT_NONCE], uint32_t gen, const uint8_t* pkg, size_t n,
                uint8_t* out, size_t cap) {
  if (!pkg || n == 0 || n > CONT_MAX || cap < CONT_HDR + n + CONT_TAG) return 0;
  uint8_t k_enc[32], k_mac[32];
  derive(key, "mecon continuity enc v1", k_enc);
  derive(key, "mecon continuity mac v1", k_mac);
  memcpy(out, MAGIC, 4);
  put32(out + 4, gen);
  out[8] = (uint8_t)n; out[9] = (uint8_t)(n >> 8);
  memcpy(out + 10, nonce, CONT_NONCE);
  crypt(k_enc, nonce, pkg, out + CONT_HDR, n);
  hmacSha256(k_mac, 32, out, CONT_HDR + n, out + CONT_HDR + n);   // the header too: neither length nor generation moves
  memset(k_enc, 0, sizeof(k_enc));
  memset(k_mac, 0, sizeof(k_mac));
  return CONT_HDR + n + CONT_TAG;
}

bool contOpen(const uint8_t key[CONT_KEY], const uint8_t* in, size_t len, uint8_t* pkg, size_t cap, size_t* n,
              uint32_t* gen) {
  if (!in || len < CONT_HDR + 1 + CONT_TAG || memcmp(in, MAGIC, 4) != 0) return false;
  const size_t m = (size_t)in[8] | (size_t)in[9] << 8;
  if (m == 0 || m > CONT_MAX || len != CONT_HDR + m + CONT_TAG || m > cap) return false;
  uint8_t k_enc[32], k_mac[32], tag[32];
  derive(key, "mecon continuity mac v1", k_mac);
  hmacSha256(k_mac, 32, in, CONT_HDR + m, tag);
  const bool ok = constTimeEqual(tag, in + CONT_HDR + m, CONT_TAG);
  memset(k_mac, 0, sizeof(k_mac));
  memset(tag, 0, sizeof(tag));
  if (!ok) return false;
  derive(key, "mecon continuity enc v1", k_enc);
  crypt(k_enc, in + 10, in + CONT_HDR, pkg, m);
  memset(k_enc, 0, sizeof(k_enc));
  *n = m;
  *gen = get32(in + 4);
  return true;
}

}  // namespace Dep
