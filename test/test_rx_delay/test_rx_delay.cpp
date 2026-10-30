// The receive delay in float (src/helpers/RxDelay.h) against upstream's double formula, over the inputs the dispatcher
// gives it: packetScore() is clamped to 0..1, and a packet's estimated airtime is at most about 10 s (255 bytes, SF12,
// 62.5 kHz). A node on this firmware must wait as long as an upstream node would for the same packet.
#include <gtest/gtest.h>
#include <math.h>
#include <stdlib.h>
#include "helpers/RxDelay.h"

static int upstream(float score, uint32_t air_time) {   // stock Dispatcher::calcRxDelay, verbatim
  return (int) ((pow(10, 0.85f - score) - 1.0) * air_time);
}

TEST(RxDelay, MatchesUpstreamToOneMillisecondEverywhere) {
  long checked = 0, equal = 0;
  int worst = 0;
  for (int s = 0; s <= 1000; s++) {
    const float score = s / 1000.0f;
    for (uint32_t air = 0; air <= 20000; air += (air < 200 ? 1 : 7)) {
      const int d = RxDelay::ms(score, air) - upstream(score, air);
      if (abs(d) > worst) worst = abs(d);
      if (d == 0) equal++;
      checked++;
    }
  }
  EXPECT_LE(worst, 1) << "float and double may differ only where the product sits on an integer";
  EXPECT_GE(equal * 1000, checked * 999) << equal << " of " << checked << " equal";
}

TEST(RxDelay, TheEndsOfTheRange) {
  EXPECT_EQ(upstream(0.85f, 1000), RxDelay::ms(0.85f, 1000));   // 10^0 - 1: no wait
  EXPECT_EQ(0, RxDelay::ms(0.85f, 1000));
  EXPECT_LT(RxDelay::ms(1.0f, 1000), 0);                        // above 0.85: negative, handled at once
  EXPECT_NEAR(upstream(0.0f, 10000), RxDelay::ms(0.0f, 10000), 1);   // the longest wait
  EXPECT_EQ(0, RxDelay::ms(0.3f, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
