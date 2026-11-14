#pragma once
// The Deployment continuity package at rest (SECURITY_AND_AUTHORITY, "Protected continuity material"; #118). The firmware
// never reads the package: a manager encrypts it under the user's passphrase before it arrives. The device seals that
// ciphertext again under a per-device wrapping key it never exports, so that destroying the key destroys the package,
// whatever flash still holds. Self-contained on HMAC-SHA-256 (DepHmac): a keystream in counter mode and a tag, each under
// its own key derived from the wrapping key, with a fresh nonce on every seal. Pure logic: runs in the native unit tests.
#include <stddef.h>
#include <stdint.h>

namespace Dep {

const size_t CONT_MAX = 768;        // the largest package: its export, base64 and signed, still fits one result
const size_t CONT_KEY = 32;         // the wrapping key
const size_t CONT_NONCE = 16;
const size_t CONT_HDR = 4 + 4 + 2 + CONT_NONCE;   // magic, generation, length, nonce
const size_t CONT_TAG = 32;
const size_t CONT_SEALED_MAX = CONT_HDR + CONT_MAX + CONT_TAG;

// The sealed form of n bytes (1..CONT_MAX) at generation gen; its length, or 0 when it does not fit cap.
size_t contSeal(const uint8_t key[CONT_KEY], const uint8_t nonce[CONT_NONCE], uint32_t gen, const uint8_t* pkg, size_t n,
                uint8_t* out, size_t cap);
// The package back, its tag checked first (in time that does not depend on the tag); false when anything is wrong, and
// then nothing is written to pkg.
bool contOpen(const uint8_t key[CONT_KEY], const uint8_t* in, size_t len, uint8_t* pkg, size_t cap, size_t* n,
              uint32_t* gen);

}  // namespace Dep
