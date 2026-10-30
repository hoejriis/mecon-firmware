// The 64-bit uptime (src/helpers/Uptime.h): it keeps counting where a 32-bit millis() wraps after 49.7 days. One
// accumulator per process, so the cases share it and run in this order.
#include <gtest/gtest.h>
#include <stdint.h>
#include "helpers/Uptime.h"

TEST(Uptime, CountsFromBoot) {
  EXPECT_EQ(0u, Uptime::ms(0));
  EXPECT_EQ(1500u, Uptime::ms(1500));
  EXPECT_EQ(1u, Uptime::secs(1999));
}

TEST(Uptime, KeepsCountingAcrossTheWrap) {
  const uint32_t before = 0xFFFFF000u;                  // 4,096 ms before millis() wraps
  const uint64_t at = Uptime::ms(before);
  EXPECT_EQ((uint64_t)before, at);
  const uint64_t after = Uptime::ms(0x00001000u);       // 4,096 ms after it
  EXPECT_EQ(at + 8192u, after);                         // millis() went back to 4,096; the uptime did not
  EXPECT_EQ((0x100000000ull + 0x1000u) / 1000, Uptime::secs(0x00001000u));
}

TEST(Uptime, AWrapEveryFewWeeksAddsUp) {
  // three more wraps, one call per wrap: 3 x 2^32 ms more
  const uint64_t start = Uptime::ms(0x00001000u);
  for (int i = 0; i < 3; i++) {
    Uptime::ms(0x80001000u);
    Uptime::ms(0x00001000u);
  }
  EXPECT_EQ(start + 3ull * 0x100000000ull, Uptime::ms(0x00001000u));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
