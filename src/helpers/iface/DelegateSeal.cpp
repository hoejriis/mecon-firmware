#include "DelegateSeal.h"
#include <string.h>
#include <AES.h>
#include "../deployment/DepHmac.h"
#include "Wipe.h"

namespace Iface {
namespace Dl {

namespace {
const char LABEL[] = "mecon delegate-seal v1";

void ctr(const uint8_t k[16], const uint8_t* in, uint8_t* out, size_t n) {
  AES128 aes;
  uint8_t block[16], ks[16];
  aes.setKey(k, 16);
  memset(block, 0, sizeof(block));
  memset(ks, 0, sizeof(ks));   // the native tests' AES is a stub that writes nothing
  for (size_t off = 0; off < n; off += 16) {
    aes.encryptBlock(ks, block);
    for (size_t i = 0; i < 16 && off + i < n; i++) out[off + i] = in[off + i] ^ ks[i];
    for (int i = 15; i >= 0 && ++block[i] == 0; i--) {}   // the next counter value
  }
  wipe(ks, sizeof(ks));
  aes.clear();
}

void tagOf(const uint8_t k[32], const uint8_t* ct, uint8_t out[32]) { Dep::hmacSha256(k + 16, 16, ct, KEY, out); }
}  // namespace

void kdf(const uint8_t shared[32], uint32_t job_id, uint8_t slot, const uint8_t eph[EPH], const uint8_t pub[32], uint8_t k[32]) {
  uint8_t msg[sizeof(LABEL) - 1 + 4 + 1 + EPH + 32];
  size_t o = 0;
  memcpy(msg, LABEL, sizeof(LABEL) - 1); o += sizeof(LABEL) - 1;
  msg[o++] = (uint8_t)(job_id >> 24); msg[o++] = (uint8_t)(job_id >> 16); msg[o++] = (uint8_t)(job_id >> 8); msg[o++] = (uint8_t)job_id;
  msg[o++] = slot;
  memcpy(msg + o, eph, EPH); o += EPH;
  memcpy(msg + o, pub, 32); o += 32;
  Dep::hmacSha256(shared, 32, msg, o, k);
}

bool open(const uint8_t k[32], const uint8_t* ct_tag, uint8_t prv[KEY]) {
  uint8_t tag[32];
  tagOf(k, ct_tag, tag);
  const bool ok = Dep::constTimeEqual(tag, ct_tag + KEY, TAG);
  wipe(tag, sizeof(tag));
  if (!ok) return false;
  ctr(k, ct_tag, prv, KEY);
  return true;
}

void seal(const uint8_t k[32], const uint8_t prv[KEY], uint8_t* ct_tag) {
  uint8_t tag[32];
  ctr(k, prv, ct_tag, KEY);
  tagOf(k, ct_tag, tag);
  memcpy(ct_tag + KEY, tag, TAG);
  wipe(tag, sizeof(tag));
}

}  // namespace Dl
}  // namespace Iface
