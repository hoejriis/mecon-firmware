#pragma once
// The one Ed25519 verify of the authority layer, chosen as mesh::Identity::verify chooses (src/Identity.cpp):
// - nRF52840 (USE_CC310_HW_CRYPTO): the CryptoCell CC310 hardware, about 700 bytes of stack. The software verifiers
//   need about 3 KB, which overflowed the Adafruit core's 4 KB loop stack on a first provisioning (#158).
// - other firmware: the Crypto library's Ed25519::verify, as upstream uses for every signature it checks.
// - the native unit tests, which link neither: the bundled reference implementation (lib/ed25519).
// The message is passed as bytes; a signature or key of the wrong length is the caller's to refuse before this.
#include <stddef.h>
#include <stdint.h>

#if defined(USE_CC310_HW_CRYPTO)
  #include <Adafruit_nRFCrypto.h>
  #include "nrf_cc310/include/crys_ec_edw_api.h"
#elif defined(__has_include)
  #if __has_include(<Ed25519.h>)
    #include <Ed25519.h>
    #define DEP_VERIFY_CRYPTO_LIB 1
  #endif
#endif
#if !defined(USE_CC310_HW_CRYPTO) && !defined(DEP_VERIFY_CRYPTO_LIB)
  #include <ed_25519.h>
#endif

namespace Dep {

// Strict encodings (RFC 8032 section 5.1.7), checked before any verifier: S below the group order L, and R a
// canonical point encoding (its y below p). The verifiers differ here: the reference implementation refused only
// S >= 2^253 and compared R as bytes; the Crypto library bounds neither; the CC310 is closed. Each is a malleation of a
// valid signature, never a forgery, and replay protection keys on the job id, not the signature. But which signatures
// the device accepts must not depend on the build.
inline bool canonicalS(const uint8_t s[32]) {
  static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
  for (int i = 31; i >= 0; i--) {
    if (s[i] < L[i]) return true;
    if (s[i] > L[i]) return false;
  }
  return false;   // S == L
}

inline bool canonicalPoint(const uint8_t r[32]) {
  // y is the low 255 bits; p = 2^255 - 19, so y >= p only when bits 8..254 are all ones and the low byte is >= 0xed.
  if ((r[31] & 0x7f) != 0x7f) return true;
  for (int i = 30; i >= 1; i--)
    if (r[i] != 0xff) return true;
  return r[0] < 0xed;
}

inline bool verifySig(const uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t key[32]) {
  if (!canonicalS(sig + 32) || !canonicalPoint(sig)) return false;
#if defined(USE_CC310_HW_CRYPTO)
  static CRYS_ECEDW_TempBuff_t tmp;   // the CC310 workspace: static, as in Identity::verify (one caller at a time)
  return CRYS_ECEDW_Verify(const_cast<uint8_t*>(sig), CRYS_ECEDW_SIGNATURE_BYTES, const_cast<uint8_t*>(key),
                           CRYS_ECEDW_MOD_SIZE_IN_BYTES, const_cast<uint8_t*>(msg), len, &tmp) == CRYS_OK;
#elif defined(DEP_VERIFY_CRYPTO_LIB)
  return Ed25519::verify(sig, key, msg, len);
#else
  return ed25519_verify(sig, msg, len, key) == 1;
#endif
}

}  // namespace Dep
