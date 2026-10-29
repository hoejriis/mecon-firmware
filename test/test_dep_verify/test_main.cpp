// The authority layer's verify (src/helpers/deployment/DepVerify.h): strict encodings refused before any verifier runs,
// so a malleated copy of a valid signature (S + L, a non-canonical R) is refused on every build (#158).
#include <gtest/gtest.h>
#include <string.h>
#include "helpers/deployment/DepVerify.h"
#include "ed_25519.h"

namespace {

struct Signed {
  uint8_t pub[32], prv[64], sig[64];
  const uint8_t* msg = reinterpret_cast<const uint8_t*>("MECON test message");
  size_t len = 18;
  Signed() {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 7 + 1);
    ed25519_create_keypair(pub, prv, seed);
    ed25519_sign(sig, msg, len, pub, prv);
  }
};

// S + L, little-endian: the same scalar mod L, so a verifier without the bound accepts it.
void addL(uint8_t s[32]) {
  static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
  unsigned c = 0;
  for (int i = 0; i < 32; i++) {
    c += s[i] + L[i];
    s[i] = (uint8_t)c;
    c >>= 8;
  }
}

}  // namespace

TEST(DepVerify, AValidSignatureVerifies) {
  Signed s;
  EXPECT_TRUE(Dep::verifySig(s.sig, s.msg, s.len, s.pub));
  uint8_t other[64];
  memcpy(other, s.sig, 64);
  other[5] ^= 1;
  EXPECT_FALSE(Dep::verifySig(other, s.msg, s.len, s.pub));
}

TEST(DepVerify, ScalarPlusTheOrderIsRefused) {
  Signed s;
  uint8_t m[64];
  memcpy(m, s.sig, 64);
  addL(m + 32);
  ASSERT_EQ(0, m[63] & 0xe0) << "still below 2^253, which the reference verifier alone would accept";
  EXPECT_EQ(1, ed25519_verify(m, s.msg, s.len, s.pub)) << "the reference verifier does accept it";
  EXPECT_FALSE(Dep::verifySig(m, s.msg, s.len, s.pub));
}

TEST(DepVerify, EncodingBoundsAreExact) {
  uint8_t s[32] = {0xec, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};   // L - 1
  EXPECT_TRUE(Dep::canonicalS(s));
  s[0] = 0xed;   // L
  EXPECT_FALSE(Dep::canonicalS(s));
  uint8_t r[32];
  memset(r, 0xff, 32);
  r[31] = 0x7f;
  r[0] = 0xec;   // p - 1
  EXPECT_TRUE(Dep::canonicalPoint(r));
  r[0] = 0xed;   // p
  EXPECT_FALSE(Dep::canonicalPoint(r));
  r[31] = 0xff;   // the sign bit set does not change y
  EXPECT_FALSE(Dep::canonicalPoint(r));
  r[31] = 0x7e;
  EXPECT_TRUE(Dep::canonicalPoint(r));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
