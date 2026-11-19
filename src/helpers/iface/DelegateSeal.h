#pragma once
// mecon-dl-v1: the private key of a delegated receive identity, sealed to this device's node key (identity.delegate.put;
// AUTHORITY.md section 10b). A manager makes an ephemeral Ed25519 key pair and agrees a secret with the node's public key,
// so only the node key opens it, whatever carries the request (a broker, a log or a relay holds only the sealed bytes).
//
//   shared = X25519 of the ephemeral key and the node key (MeshCore's calcSharedSecret: Ed25519 keys taken to Curve25519)
//   K      = HMAC-SHA256(shared, "mecon delegate-seal v1" || job_id as 4 bytes big-endian || slot (1 byte) || eph (32) || pub (32))
//   ct     = AES-128-CTR(K[0..15], counter block starting at zero, incremented as one 128-bit big-endian number)
//   tag    = HMAC-SHA256(K[16..31], ct), its first 16 bytes
//   sealed = eph (32) || ct (64) || tag (16)
//
// The label differs from mecon-kx-v1's ("mecon key-export v1"), and K binds the job, the slot, the ephemeral key and the
// delegated public key, so a sealed key cannot be replayed under another job, put in another slot or paired with another
// public key, and an exported key can never be mistaken for one. The tag is checked before anything is decrypted.
// Pure logic on Dep::hmacSha256 and AES: the KDF and tag run in the native unit tests.

#include <stddef.h>
#include <stdint.h>

namespace Iface {
namespace Dl {

const size_t EPH = 32;
const size_t KEY = 64;       // MeshCore's private key form
const size_t TAG = 16;
const size_t SEALED = EPH + KEY + TAG;

// K from the shared secret (32 bytes) and the context.
void kdf(const uint8_t shared[32], uint32_t job_id, uint8_t slot, const uint8_t eph[EPH], const uint8_t pub[32], uint8_t k[32]);
// The key from sealed's ct || tag part (KEY + TAG bytes) under k: the tag first, in time that does not depend on it, and
// prv is written only when it verifies.
bool open(const uint8_t k[32], const uint8_t* ct_tag, uint8_t prv[KEY]);
// The sealing side, used by the tests (a manager seals with its own tools).
void seal(const uint8_t k[32], const uint8_t prv[KEY], uint8_t* ct_tag);

}  // namespace Dl
}  // namespace Iface
