#pragma once
// SHA-256 and HMAC-SHA-256 (FIPS 180-4, RFC 2104), for the local unlock challenge (docs/contract/AUTHORITY.md section
// 10). Self-contained on purpose: the SHA256 class the firmware links is the Crypto library's, which the host tests
// replace with a non-cryptographic mock, so the one place that must be provable against the RFC 4231 vectors carries its
// own. About 1 KB of flash. Pure logic: runs in the native unit tests.
#include <stddef.h>
#include <stdint.h>

namespace Dep {

const size_t HMAC_LEN = 32;

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]);
// n bytes compared in time that does not depend on where they differ.
bool constTimeEqual(const uint8_t* a, const uint8_t* b, size_t n);

}  // namespace Dep
