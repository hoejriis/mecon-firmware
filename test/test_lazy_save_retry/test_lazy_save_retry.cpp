// The retry schedule of the Companion's delayed contacts save after a write error (src/helpers/LazySaveRetry.h).
#include <gtest/gtest.h>
#include "helpers/LazySaveRetry.h"

TEST(LazySaveRetry, RetriesThreeTimesFurtherApartEachTime) {
  EXPECT_EQ(5000u, LazySaveRetry::delayMs(1));
  EXPECT_EQ(15000u, LazySaveRetry::delayMs(2));
  EXPECT_EQ(45000u, LazySaveRetry::delayMs(3));
}

TEST(LazySaveRetry, GivesUpOnceTheRetriesAreSpent) {
  EXPECT_EQ(0u, LazySaveRetry::delayMs(4)) << "a file system that keeps failing must not keep the board awake";
  EXPECT_EQ(0u, LazySaveRetry::delayMs(255));
}

TEST(LazySaveRetry, AttemptZeroIsNotARetry) {
  EXPECT_EQ(0u, LazySaveRetry::delayMs(0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
