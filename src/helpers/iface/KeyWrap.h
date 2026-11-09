#pragma once
// mecon-kx-v1: a node private key wrapped for one Deployment member (identity.key.export on an enrolled device;
// OPERATIONS.md, SECURITY_AND_AUTHORITY.md). Only that member's private key unwraps it, so a broker, a log or a relay that
// carries the signed result never holds a usable key.
//
//   shared = X25519 of the node key and the member's key (MeshCore's calcSharedSecret: Ed25519 keys taken to Curve25519)
//   K      = HMAC-SHA256(shared, "mecon key-export v1" || job_id as 4 bytes big-endian)
//   ct     = AES-128-CTR(K[0..15], counter block starting at zero, incremented as one 128-bit big-endian number)
//   tag    = HMAC-SHA256(K[16..31], ct), its first 16 bytes
//   wrap   = ct || tag
//
// K is never the key a direct message between the two would use, and differs per export (the job id), so the zero
// counter is never reused under one key. The tag is full length: the wrap stands on its own, besides the signed result.

#include <stddef.h>
#include <stdint.h>

namespace mesh { class LocalIdentity; }

namespace Iface {

const size_t KX_TAG = 16;

// out takes n + KX_TAG bytes. The intermediate secrets are wiped.
void wrapKeyForMember(const mesh::LocalIdentity& node, const uint8_t member_pub[32], uint32_t job_id, const uint8_t* key,
                      size_t n, uint8_t* out);

}  // namespace Iface
