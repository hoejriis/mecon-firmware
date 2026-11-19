#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/deployment/DepContinuity.h"
#include "helpers/deployment/DepDelegates.h"

using namespace Dep;

static const uint8_t KEY[DLG_KEY] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t NONCE[DLG_NONCE] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
                                         0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};

static std::vector<uint8_t> records(size_t n) {
  std::vector<uint8_t> p(n * DLG_REC);
  for (size_t i = 0; i < p.size(); i++) p[i] = (uint8_t)(i * 29 + 5);
  return p;
}

TEST(DepDelegates, TheRecordsComeBackWholeAndAreNotStoredInTheClear) {
  for (size_t n = 1; n <= DLG_SLOTS; n++) {
    const auto p = records(n);
    uint8_t sealed[DLG_SEALED_MAX], out[DLG_PLAIN_MAX];
    const size_t len = dlgSeal(KEY, NONCE, p.data(), p.size(), sealed, sizeof(sealed));
    ASSERT_EQ(DLG_HDR + p.size() + DLG_TAG, len) << n;
    size_t m = 0;
    ASSERT_TRUE(dlgOpen(KEY, sealed, len, out, sizeof(out), &m)) << n;
    EXPECT_EQ(p.size(), m);
    EXPECT_EQ(0, memcmp(p.data(), out, m));
    EXPECT_NE(0, memcmp(p.data(), sealed + DLG_HDR, p.size())) << "not stored in the clear";
  }
}

TEST(DepDelegates, AnyChangedByteIsRefusedAndNothingIsWritten) {
  const auto p = records(2);
  uint8_t sealed[DLG_SEALED_MAX];
  const size_t len = dlgSeal(KEY, NONCE, p.data(), p.size(), sealed, sizeof(sealed));
  for (size_t i = 0; i < len; i++) {
    uint8_t t[DLG_SEALED_MAX], out[DLG_PLAIN_MAX];
    memcpy(t, sealed, len);
    t[i] ^= 0x01;
    memset(out, 0xEE, sizeof(out));
    size_t m = 0;
    EXPECT_FALSE(dlgOpen(KEY, t, len, out, sizeof(out), &m)) << i;
    for (size_t j = 0; j < sizeof(out); j++) ASSERT_EQ(0xEE, out[j]) << i;
  }
}

TEST(DepDelegates, AnotherKeyAnotherLengthAndATruncationAreRefused) {
  const auto p = records(1);
  uint8_t sealed[DLG_SEALED_MAX], out[DLG_PLAIN_MAX];
  const size_t len = dlgSeal(KEY, NONCE, p.data(), p.size(), sealed, sizeof(sealed));
  uint8_t other[DLG_KEY];
  memcpy(other, KEY, sizeof(other));
  other[0] ^= 1;
  size_t m = 0;
  EXPECT_FALSE(dlgOpen(other, sealed, len, out, sizeof(out), &m));
  EXPECT_FALSE(dlgOpen(KEY, sealed, len - 1, out, sizeof(out), &m));
  EXPECT_FALSE(dlgOpen(KEY, sealed, len, out, p.size() - 1, &m)) << "a destination too small";
}

TEST(DepDelegates, OnlyWholeRecordsUpToTheLimitAreSealed) {
  const auto p = records(DLG_SLOTS + 1);
  uint8_t sealed[DLG_SEALED_MAX + DLG_REC];
  EXPECT_EQ(0u, dlgSeal(KEY, NONCE, p.data(), DLG_SLOTS * DLG_REC + DLG_REC, sealed, sizeof(sealed))) << "a fifth";
  EXPECT_EQ(0u, dlgSeal(KEY, NONCE, p.data(), DLG_REC - 1, sealed, sizeof(sealed))) << "a part of one";
  EXPECT_EQ(0u, dlgSeal(KEY, NONCE, p.data(), 0, sealed, sizeof(sealed))) << "none";
  EXPECT_EQ(0u, dlgSeal(KEY, NONCE, p.data(), DLG_REC, sealed, DLG_HDR + DLG_REC + DLG_TAG - 1)) << "no room";
}

TEST(DepDelegates, TheContinuityPackagesBytesNeverOpenAsTheTableAndTheOtherWayRound) {
  const auto p = records(1);
  uint8_t sealed[DLG_SEALED_MAX], out[DLG_PLAIN_MAX];
  const size_t len = dlgSeal(KEY, NONCE, p.data(), p.size(), sealed, sizeof(sealed));
  uint8_t pkg[CONT_MAX];
  size_t m = 0;
  uint32_t gen = 0;
  EXPECT_FALSE(contOpen(KEY, sealed, len, pkg, sizeof(pkg), &m, &gen));
  uint8_t cs[CONT_SEALED_MAX];
  const size_t cl = contSeal(KEY, NONCE, 1, p.data(), p.size(), cs, sizeof(cs));
  ASSERT_GT(cl, 0u);
  EXPECT_FALSE(dlgOpen(KEY, cs, cl, out, sizeof(out), &m));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
