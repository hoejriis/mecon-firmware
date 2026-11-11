#include "KeyWrap.h"
#include <string.h>
#include <AES.h>
#include <SHA256.h>
#include "../../Identity.h"
#include "Wipe.h"

namespace Iface {

namespace {
const char LABEL[] = "mecon key-export v1";
}

void wrapKeyForMember(const mesh::LocalIdentity& node, const uint8_t member_pub[32], uint32_t job_id, const uint8_t* key,
                      size_t n, uint8_t* out) {
  uint8_t shared[32], k[32], block[16], ks[16];
  node.calcSharedSecret(shared, member_pub);
  const uint8_t job[4] = {(uint8_t)(job_id >> 24), (uint8_t)(job_id >> 16), (uint8_t)(job_id >> 8), (uint8_t)job_id};
  SHA256 sha;
  sha.resetHMAC(shared, sizeof(shared));
  sha.update(LABEL, sizeof(LABEL) - 1);
  sha.update(job, sizeof(job));
  sha.finalizeHMAC(shared, sizeof(shared), k, sizeof(k));
  AES128 aes;
  aes.setKey(k, 16);
  memset(block, 0, sizeof(block));
  for (size_t off = 0; off < n; off += 16) {
    aes.encryptBlock(ks, block);
    for (size_t i = 0; i < 16 && off + i < n; i++) out[off + i] = key[off + i] ^ ks[i];
    for (int i = 15; i >= 0 && ++block[i] == 0; i--) {}   // the next counter value
  }
  sha.resetHMAC(k + 16, 16);
  sha.update(out, n);
  uint8_t tag[32];
  sha.finalizeHMAC(k + 16, 16, tag, sizeof(tag));
  memcpy(out + n, tag, KX_TAG);
  wipe(shared, sizeof(shared));
  wipe(k, sizeof(k));
  wipe(ks, sizeof(ks));
  wipe(tag, sizeof(tag));
  aes.clear();
}

}  // namespace Iface
