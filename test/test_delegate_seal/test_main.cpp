#include <gtest/gtest.h>
#include <string.h>
#include "helpers/deployment/DepHmac.h"
#include "helpers/iface/DelegateSeal.h"

// The public vector of mecon-dl-v1 (test/fixtures/delegate_seal_v1.json, made by tools/mecon_dl_vectors.py). The native
// build's AES is a stub, so these tests check what is plain HMAC-SHA-256: the KDF, which binds every part of the context, and
// the tag, which is checked before anything is decrypted. The cipher's output is checked against the vector in the simulator
// (sim/tests/test_delegates.py), where the firmware's own AES runs.
using namespace Iface::Dl;

static const uint8_t SHARED[32] = {0x1a, 0xc6, 0x4d, 0x9d, 0x66, 0x5e, 0xec, 0xe2, 0x45, 0xaa, 0xc9, 0xef, 0x83, 0xeb, 0xe0, 0xbe, 0x89, 0xb1, 0xc6, 0x9d, 0xcd, 0x1f, 0x68, 0xa7, 0xac, 0x9d, 0xac, 0x34, 0x1a, 0x9a, 0xe7, 0x31};
static const uint8_t EPH_PUB[32] = {0xc6, 0xbc, 0xea, 0x0c, 0xec, 0x47, 0x0c, 0x7c, 0xab, 0x76, 0xc3, 0x88, 0x8b, 0xa3, 0x0f, 0xba, 0xa3, 0x1c, 0x19, 0x45, 0xd0, 0x4a, 0x76, 0x89, 0xb8, 0xf9, 0x03, 0xcc, 0x9b, 0x08, 0x0a, 0x6f};
static const uint8_t PUB[32] = {0xcd, 0x1e, 0xb2, 0x06, 0x4c, 0x1b, 0xc5, 0x5d, 0xa0, 0x50, 0x07, 0x9e, 0xd3, 0xbe, 0xf5, 0x88, 0xd2, 0x90, 0x07, 0x36, 0xb8, 0xf2, 0xe1, 0x4b, 0xc5, 0x58, 0x84, 0xc3, 0x96, 0x8c, 0x2a, 0xdd};
static const uint8_t K_EXPECT[32] = {0x33, 0x06, 0xa4, 0x48, 0x00, 0xb1, 0x3e, 0x5f, 0xde, 0xf7, 0x84, 0x0f, 0x84, 0x96, 0x7c, 0x76, 0x98, 0xe9, 0x71, 0x37, 0xd6, 0x1a, 0x36, 0xfa, 0x53, 0x89, 0xd7, 0x44, 0xc6, 0x84, 0x61, 0xe5};
static const uint8_t CT_TAG[KEY + TAG] = {0x00, 0x95, 0xaa, 0xf2, 0xa8, 0xae, 0x9c, 0x43, 0x2b, 0x72, 0x1d, 0x98, 0x22, 0xc4, 0xe9, 0x2b, 0x4f, 0x36, 0xfe, 0xd2, 0x29, 0xb3, 0x9f, 0xfc, 0xcb, 0x18, 0xd6, 0xfd, 0xcc, 0xb1, 0x5d, 0x2f, 0x1b, 0x9a, 0xad, 0xbc, 0xde, 0xb5, 0x06, 0x71, 0x97, 0xa9, 0x7c, 0x07, 0x8a, 0x65, 0xde, 0xc6, 0xe4, 0x7e, 0x85, 0xb8, 0xa5, 0x14, 0xa5, 0x35, 0xdb, 0xac, 0x06, 0x96, 0x9b, 0x16, 0x2f, 0xc5, 0x7c, 0x3a, 0xc5, 0x2d, 0x5a, 0xff, 0x26, 0x4d, 0xa1, 0xcf, 0xe3, 0x2e, 0x0d, 0xb2, 0x6d, 0x4b};
static const uint32_t JOB = 2345;
static const uint8_t SLOT = 1;

static void kdfOf(uint32_t job, uint8_t slot, const uint8_t* eph, const uint8_t* pub, uint8_t* k) {
  kdf(SHARED, job, slot, eph, pub, k);
}

TEST(DelegateSeal, TheKdfIsThePublishedVector) {
  uint8_t k[32];
  kdfOf(JOB, SLOT, EPH_PUB, PUB, k);
  EXPECT_EQ(0, memcmp(k, K_EXPECT, 32));
}

TEST(DelegateSeal, EveryPartOfTheContextChangesTheKey) {
  uint8_t k[32], other[32];
  kdfOf(JOB, SLOT, EPH_PUB, PUB, k);
  kdfOf(JOB + 1, SLOT, EPH_PUB, PUB, other);
  EXPECT_NE(0, memcmp(k, other, 32)) << "job";
  kdfOf(JOB, SLOT + 1, EPH_PUB, PUB, other);
  EXPECT_NE(0, memcmp(k, other, 32)) << "slot";
  uint8_t e2[32], p2[32];
  memcpy(e2, EPH_PUB, 32); e2[31] ^= 1;
  kdfOf(JOB, SLOT, e2, PUB, other);
  EXPECT_NE(0, memcmp(k, other, 32)) << "ephemeral key";
  memcpy(p2, PUB, 32); p2[0] ^= 1;
  kdfOf(JOB, SLOT, EPH_PUB, p2, other);
  EXPECT_NE(0, memcmp(k, other, 32)) << "delegated public key";
  uint8_t s2[32];
  memcpy(s2, SHARED, 32); s2[5] ^= 1;
  kdf(s2, JOB, SLOT, EPH_PUB, PUB, other);
  EXPECT_NE(0, memcmp(k, other, 32)) << "shared secret";
}

TEST(DelegateSeal, TheTagOfTheVectorVerifiesUnderItsKeyOnly) {
  uint8_t prv[KEY];
  memset(prv, 0xEE, sizeof(prv));
  EXPECT_TRUE(open(K_EXPECT, CT_TAG, prv));
  uint8_t wrong[32];
  kdfOf(JOB + 1, SLOT, EPH_PUB, PUB, wrong);
  memset(prv, 0xEE, sizeof(prv));
  EXPECT_FALSE(open(wrong, CT_TAG, prv));
  for (size_t i = 0; i < sizeof(prv); i++) EXPECT_EQ(0xEE, prv[i]) << "nothing is written for a refused seal";
}

TEST(DelegateSeal, AnyChangedByteOfTheCiphertextOrTheTagIsRefusedAndNothingIsWritten) {
  for (size_t i = 0; i < sizeof(CT_TAG); i++) {
    uint8_t t[KEY + TAG], prv[KEY];
    memcpy(t, CT_TAG, sizeof(t));
    t[i] ^= 0x01;
    memset(prv, 0xEE, sizeof(prv));
    EXPECT_FALSE(open(K_EXPECT, t, prv)) << i;
    for (size_t j = 0; j < sizeof(prv); j++) ASSERT_EQ(0xEE, prv[j]) << i;
  }
}

TEST(DelegateSeal, ALabelOtherThanTheKeyExportOneNeverOpensIt) {
  // mecon-kx-v1 derives K = HMAC(shared, "mecon key-export v1" || job); the same shared secret and job give another key here
  uint8_t kx_msg[19 + 4] = {'m','e','c','o','n',' ','k','e','y','-','e','x','p','o','r','t',' ','v','1', 0, 0, (uint8_t)(JOB >> 8), (uint8_t)JOB};
  uint8_t kx[32], prv[KEY];
  Dep::hmacSha256(SHARED, 32, kx_msg, sizeof(kx_msg), kx);
  EXPECT_FALSE(open(kx, CT_TAG, prv));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
