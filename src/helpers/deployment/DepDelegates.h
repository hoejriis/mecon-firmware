#pragma once
// The delegated receive identities at rest (AUTHORITY.md section 10b; Issue #552). The table of up to four {slot, public key,
// private key} records is sealed under a per-device wrapping key the device makes itself and never exports, so destroying
// that key destroys every delegated key, whatever flash still holds. Self-contained on HMAC-SHA-256 (DepHmac): a keystream
// in counter mode and a tag, each under its own key derived from the wrapping key, with a fresh nonce on every seal. The
// labels differ from the continuity package's, so one file's bytes never open as the other. Pure logic: runs in the native
// unit tests.
#include <stddef.h>
#include <stdint.h>

namespace Dep {

const size_t DLG_SLOTS = 4;          // the most delegated identities; reported in the capabilities
const size_t DLG_KEY = 32;           // the wrapping key
const size_t DLG_NONCE = 16;
const size_t DLG_REC = 1 + 32 + 64;  // slot, public key, private key
const size_t DLG_PLAIN_MAX = DLG_SLOTS * DLG_REC;
const size_t DLG_HDR = 4 + 2 + DLG_NONCE;   // magic, length, nonce
const size_t DLG_TAG = 32;
const size_t DLG_SEALED_MAX = DLG_HDR + DLG_PLAIN_MAX + DLG_TAG;

// The sealed form of n bytes (a whole number of records, 1..DLG_SLOTS of them); its length, or 0 when it does not fit cap
// or n is not a whole number of records.
size_t dlgSeal(const uint8_t key[DLG_KEY], const uint8_t nonce[DLG_NONCE], const uint8_t* plain, size_t n, uint8_t* out,
               size_t cap);
// The records back, the tag checked first (in time that does not depend on it); false when anything is wrong, and then
// nothing is written to plain.
bool dlgOpen(const uint8_t key[DLG_KEY], const uint8_t* in, size_t len, uint8_t* plain, size_t cap, size_t* n);

}  // namespace Dep
