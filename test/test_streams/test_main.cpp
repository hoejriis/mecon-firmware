// The stream layer's pure parts (src/helpers/iface/Streams.h): the loop-gap meter and the observation rate limit.
#include <gtest/gtest.h>
#include "helpers/iface/Streams.h"

using namespace Iface;

TEST(LoopGap, KeepsTheLongestPassUntilRead) {
  LoopGap g;
  g.tick(100);        // the first pass only starts the clock
  EXPECT_EQ(0u, g.max);
  g.tick(150);
  g.tick(400);
  g.tick(410);
  EXPECT_EQ(250u, g.take());
  EXPECT_EQ(0u, g.take());   // a read resets it
  g.tick(500);
  EXPECT_EQ(90u, g.take());
}

TEST(ObsLimiter, TenABurstThenTenASecondWithDropsCounted) {
  ObsLimiter l;
  int sent = 0;
  for (int i = 0; i < 25; i++) sent += l.take(1000);   // one instant: the burst only
  EXPECT_EQ((int)OBS_RATE, sent);
  EXPECT_EQ(15u, l.dropped);
  EXPECT_FALSE(l.take(1050));                          // 50 ms: not yet a whole token
  EXPECT_TRUE(l.take(1100));                           // 100 ms: one token back
  EXPECT_FALSE(l.take(1100));
  int later = 0;
  for (int i = 0; i < 30; i++) later += l.take(5000);  // seconds later: refilled to the burst, no more
  EXPECT_EQ((int)OBS_RATE, later);
}


TEST(LogFeed, FiveASecondThenDroppedAndCountedOnTheNextEvent) {
  LogFeed f;
  Posture::AuditEntry e = {1800000000, Posture::EV_REFUSED, Posture::CALLER_MQTT, Posture::W_CLI, 1};
  int queued = 0;
  for (int i = 0; i < 20; i++) queued += f.push(1000, e);   // one instant: the burst only
  EXPECT_EQ((int)LOGS_RATE, queued);
  EXPECT_EQ(15u, f.limit.dropped);
  Posture::AuditEntry out;
  uint32_t dropped = 99;
  ASSERT_TRUE(f.pop(&out, &dropped));
  EXPECT_EQ(15u, dropped);                                // the first event out carries the count...
  ASSERT_TRUE(f.pop(&out, &dropped));
  EXPECT_EQ(0u, dropped);                                 // ...once
  EXPECT_FALSE(f.push(1100, e));                          // 100 ms: not yet a whole token at 5/s
  EXPECT_TRUE(f.push(1200, e));                           // 200 ms: one token back
}

TEST(LogFeed, AFullQueueDropsAndCounts) {
  LogFeed f;
  Posture::AuditEntry e = {0, Posture::EV_POSTURE_SET, Posture::CALLER_CABLE, 1, 1};
  for (int t = 0; t < LogFeed::QUEUE + 3; t++) f.push(t * 1000, e);   // a token every time, nobody pops
  EXPECT_EQ((int)LogFeed::QUEUE, (int)f.count);
  EXPECT_EQ(3u, f.limit.dropped);
}

TEST(LogFeed, KeepsOrder) {
  LogFeed f;
  for (uint8_t k = 1; k <= 4; k++) {
    Posture::AuditEntry e = {0, Posture::EV_REFUSED, Posture::CALLER_NET, k, k};
    f.push(0, e);
  }
  Posture::AuditEntry out;
  uint32_t d;
  for (uint8_t k = 1; k <= 4; k++) {
    ASSERT_TRUE(f.pop(&out, &d));
    EXPECT_EQ(k, out.what);
  }
  EXPECT_FALSE(f.pop(&out, &d));
}

TEST(LogData, NamesNotCodesAndNullTimeWhileTheClockWasUnset) {
  char buf[200];
  Posture::AuditEntry e = {1800000000, Posture::EV_REFUSED, Posture::CALLER_REMOTE_CLI, Posture::W_KEY_EXPORT, 3};
  ASSERT_GT(formatLogData(buf, sizeof(buf), e, 7), 0);
  EXPECT_STREQ("{\"ts\":1800000000,\"event\":\"refused\",\"caller\":\"remote-cli\",\"what\":\"key-export\",\"count\":3,\"dropped\":7}", buf);
  Posture::AuditEntry p = {12, Posture::EV_POSTURE_SET, Posture::CALLER_CABLE, 1, 1};   // a clock that was never set
  ASSERT_GT(formatLogData(buf, sizeof(buf), p, 0), 0);
  EXPECT_STREQ("{\"ts\":null,\"event\":\"posture\",\"caller\":\"cable\",\"what\":\"hardened\",\"count\":1,\"dropped\":0}", buf);
  EXPECT_EQ(-1, formatLogData(buf, 20, e, 0));   // too small: never truncated
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
