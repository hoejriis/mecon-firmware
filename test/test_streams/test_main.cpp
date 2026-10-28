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


using Posture::AuditRing;
using Posture::AuditEntry;

TEST(LogFeed, SendsOnlyWhatWasRecordedSinceTheLastPoll) {
  AuditRing r;
  LogFeed f;
  AuditEntry out[AuditRing::CAPACITY];
  uint32_t d = 0;
  r.add(10, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_CLI);   // history: before anyone listened
  f.skip(r);
  EXPECT_EQ(0, f.poll(1000, r, out, &d));
  r.add(20, Posture::EV_POSTURE_SET, Posture::CALLER_CABLE, 1);
  r.add(21, Posture::EV_MODE_SET, Posture::CALLER_BUTTON, 1);
  ASSERT_EQ(2, f.poll(1000, r, out, &d));
  EXPECT_EQ(20u, out[0].ts);                // oldest first
  EXPECT_EQ(21u, out[1].ts);
  EXPECT_EQ(0u, d);
  EXPECT_EQ(0, f.poll(1000, r, out, &d));   // nothing new
}

TEST(LogFeed, AFoldedRepeatIsSentAgainWithItsNewCountAndOnlyThatEntry) {
  AuditRing r;
  LogFeed f;
  AuditEntry out[AuditRing::CAPACITY];
  uint32_t d = 0;
  r.add(1, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_KEY_EXPORT);
  r.add(2, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_BLE_START);
  f.skip(r);
  r.add(3, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_KEY_EXPORT);   // folds into the first
  ASSERT_EQ(1, f.poll(0, r, out, &d));      // not the untouched entry that now sits beside it
  EXPECT_EQ(Posture::W_KEY_EXPORT, out[0].what);
  EXPECT_EQ(2, out[0].count);
}

TEST(LogFeed, RecordsTheRingNoLongerShowsAreCountedAsDropped) {
  AuditRing r;
  LogFeed f;
  AuditEntry out[AuditRing::CAPACITY];
  uint32_t d = 0;
  f.skip(r);
  for (int i = 0; i < 3; i++) r.add(i + 1, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_KEY_EXPORT);   // one entry, 3 records
  ASSERT_EQ(1, f.poll(0, r, out, &d));
  EXPECT_EQ(3, out[0].count);
  EXPECT_EQ(2u, d);                         // the two intermediate records
}

TEST(LogFeed, FiveASecondThenDroppedAndCountedOnTheNextEvent) {
  AuditRing r;
  LogFeed f;
  AuditEntry out[AuditRing::CAPACITY];
  uint32_t d = 0;
  f.skip(r);
  for (int i = 0; i < 20; i++) r.add(i, Posture::EV_REFUSED, Posture::CALLER_NET, (uint8_t)(100 + i));   // 20 distinct
  ASSERT_EQ((int)LOGS_RATE, f.poll(1000, r, out, &d));   // one instant: the burst only
  EXPECT_EQ(15u, d);
  r.add(50, Posture::EV_REFUSED, Posture::CALLER_NET, 1);
  EXPECT_EQ(0, f.poll(1100, r, out, &d));                // 100 ms: not yet a whole token at 5/s
  r.add(51, Posture::EV_REFUSED, Posture::CALLER_NET, 2);
  ASSERT_EQ(1, f.poll(1200, r, out, &d));                // 200 ms: one token
  EXPECT_EQ(1u, d);                                      // the one refused at 1100 ms
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
