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

inline bool verifySig(const uint8_t sig[64], const uint8_t* msg, size_t len, const uint8_t key[32]) {
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
