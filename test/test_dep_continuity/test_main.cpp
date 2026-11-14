#include <gtest/gtest.h>
#include <vector>
#include "helpers/deployment/DepContinuity.h"

using namespace Dep;

static const uint8_t KEY[CONT_KEY] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t NONCE[CONT_NONCE] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                                          0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};

static std::vector<uint8_t> pkg(size_t n) {
  std::vector<uint8_t> p(n);
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(i * 31 + 7);
  return p;
}

TEST(DepContinuity, APackageComesBackWholeWithItsGeneration) {
  for (size_t n : {(size_t)1, (size_t)31, (size_t)32, (size_t)33, (size_t)500, CONT_MAX}) {
    const auto p = pkg(n);
    uint8_t sealed[CONT_SEALED_MAX], out[CONT_MAX];
    const size_t len = contSeal(KEY, NONCE, 41, p.data(), n, sealed, sizeof(sealed));
    ASSERT_EQ(CONT_HDR + n + CONT_TAG, len) << n;
    size_t m = 0;
    uint32_t gen = 0;
    ASSERT_TRUE(contOpen(KEY, sealed, len, out, sizeof(out), &m, &gen)) << n;
    EXPECT_EQ(n, m);
    EXPECT_EQ(41u, gen);
    EXPECT_EQ(0, memcmp(p.data(), out, n));
    EXPECT_NE(0, memcmp(p.data(), sealed + CONT_HDR, n)) << "not stored in the clear";
  }
}

TEST(DepContinuity, AnyChangedByteIsRefusedAndNothingIsWritten) {
  const auto p = pkg(100);
  uint8_t sealed[CONT_SEALED_MAX];
  const size_t len = contSeal(KEY, NONCE, 7, p.data(), p.size(), sealed, sizeof(sealed));
  for (size_t i = 0; i < len; i++) {
    uint8_t t[CONT_SEALED_MAX], out[CONT_MAX];
    memcpy(t, sealed, len);
    t[i] ^= 0x01;
    memset(out, 0xEE, sizeof(out));
    size_t m = 99;
    uint32_t gen = 99;
    EXPECT_FALSE(contOpen(KEY, t, len, out, sizeof(out), &m, &gen)) << "byte " << i;
    EXPECT_EQ(0xEE, out[0]) << "byte " << i;
    EXPECT_EQ(99u, m);
  }
}

TEST(DepContinuity, AnotherKeyOpensNothing) {
  const auto p = pkg(64);
  uint8_t sealed[CONT_SEALED_MAX], out[CONT_MAX];
  const size_t len = contSeal(KEY, NONCE, 1, p.data(), p.size(), sealed, sizeof(sealed));
  uint8_t other[CONT_KEY];
  memcpy(other, KEY, sizeof(other));
  other[31] ^= 0x80;
  size_t m;
  uint32_t gen;
  EXPECT_FALSE(contOpen(other, sealed, len, out, sizeof(out), &m, &gen));
}

TEST(DepContinuity, AFreshNonceGivesOtherBytesAndTheBoundsHold) {
  const auto p = pkg(64);
  uint8_t a[CONT_SEALED_MAX], b[CONT_SEALED_MAX], out[CONT_MAX];
  uint8_t nonce2[CONT_NONCE];
  memcpy(nonce2, NONCE, sizeof(nonce2));
  nonce2[0] ^= 1;
  const size_t la = contSeal(KEY, NONCE, 1, p.data(), p.size(), a, sizeof(a));
  const size_t lb = contSeal(KEY, nonce2, 1, p.data(), p.size(), b, sizeof(b));
  ASSERT_EQ(la, lb);
  EXPECT_NE(0, memcmp(a + CONT_HDR, b + CONT_HDR, p.size())) << "the nonce changes the ciphertext";
  const auto big = pkg(CONT_MAX + 1);
  EXPECT_EQ(0u, contSeal(KEY, NONCE, 1, big.data(), big.size(), a, sizeof(a))) << "one byte past the bound";
  EXPECT_EQ(0u, contSeal(KEY, NONCE, 1, p.data(), 0, a, sizeof(a))) << "an empty package";
  EXPECT_EQ(0u, contSeal(KEY, NONCE, 1, p.data(), p.size(), a, CONT_HDR + p.size() + CONT_TAG - 1)) << "cap too small";
  size_t m;
  uint32_t gen;
  EXPECT_FALSE(contOpen(KEY, a, la - 1, out, sizeof(out), &m, &gen)) << "a cut file";
  EXPECT_FALSE(contOpen(KEY, a, la, out, p.size() - 1, &m, &gen)) << "an out buffer too small";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
