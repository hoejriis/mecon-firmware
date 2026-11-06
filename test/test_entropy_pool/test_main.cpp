#include <gtest/gtest.h>
#include <string.h>
#include "helpers/EntropyPool.h"

static void seeded(EntropyPool& p, uint8_t fill = 0x5a) {
  uint8_t s[EntropyPool::POOL_SIZE];
  memset(s, fill, sizeof(s));
  p.seed(s);
}

TEST(EntropyPool, BeforeASeedItLeavesTheDrawAsItIs) {
  EntropyPool p;
  uint8_t d[8];
  memset(d, 0x11, sizeof(d));
  p.mix(d, sizeof(d));
  for (uint8_t b : d) EXPECT_EQ(b, 0x11);
}

TEST(EntropyPool, AWeakSourceStillGivesDifferentDraws) {
  EntropyPool p;
  seeded(p);
  uint8_t a[32] = {0}, b[32] = {0};   // a source that always reads zero
  p.mix(a, sizeof(a));
  p.mix(b, sizeof(b));
  EXPECT_NE(memcmp(a, b, sizeof(a)), 0) << "the seed moves on after every draw";
  const uint8_t zero[32] = {0};
  EXPECT_NE(memcmp(a, zero, sizeof(a)), 0);
}

TEST(EntropyPool, ItDoesNotMakeAStrongSourceWeaker) {
  EntropyPool p, q;
  seeded(p);
  seeded(q);
  uint8_t a[32], b[32];
  memset(a, 0, sizeof(a));
  memset(b, 0xff, sizeof(b));
  p.mix(a, sizeof(a));
  q.mix(b, sizeof(b));
  for (size_t i = 0; i < sizeof(a); i++) EXPECT_EQ((uint8_t)(a[i] ^ 0xff), b[i]) << "XORed in: the source still shows";
}

TEST(EntropyPool, EveryByteOfALongDrawIsMixedAndBlocksDiffer) {
  EntropyPool p;
  seeded(p);
  uint8_t d[100] = {0};
  p.mix(d, sizeof(d));
  EXPECT_NE(memcmp(d, d + 32, 32), 0) << "a counter per block";
  uint8_t tail[4] = {0};
  EXPECT_NE(memcmp(d + 96, tail, 4), 0) << "the last, partial block";
}

TEST(EntropyPool, DifferentSeedsGiveDifferentStreams) {
  EntropyPool p, q;
  seeded(p, 1);
  seeded(q, 2);
  uint8_t a[32] = {0}, b[32] = {0};
  p.mix(a, sizeof(a));
  q.mix(b, sizeof(b));
  EXPECT_NE(memcmp(a, b, sizeof(a)), 0);
}

TEST(EntropyPool, TheNextSeedIsNotTheOutputOfTheDraw) {
  EntropyPool p, q;
  seeded(p);
  seeded(q);
  uint8_t first[32] = {0}, second[32] = {0}, from_output[32] = {0};
  p.mix(first, sizeof(first));
  p.mix(second, sizeof(second));
  q.seed(first);   // someone who saw a draw must not be able to follow on from it
  q.mix(from_output, sizeof(from_output));
  EXPECT_NE(memcmp(second, from_output, sizeof(second)), 0);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
