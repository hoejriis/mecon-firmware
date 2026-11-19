#include <gtest/gtest.h>
#include <string.h>
#include "helpers/iface/FmcPing.h"

using namespace Iface;

TEST(FmcParse, PingAloneIsABroadcastWithoutRegardToCase) {
  EXPECT_EQ(fmcParse("Ann: ping", "n1"), FMC_ALL);
  EXPECT_EQ(fmcParse("Ann: PiNg", "n1"), FMC_ALL);
  EXPECT_EQ(fmcParse("Ann:   ping  ", "n1"), FMC_ALL);   // spaces after the separator and a trailing ping name of nothing
  EXPECT_EQ(fmcParse("Ann: ping ", "n1"), FMC_ALL);
}

TEST(FmcParse, ANamedPingMatchesOnlyThatNameWithoutRegardToCase) {
  EXPECT_EQ(fmcParse("Ann: ping Hill-1", "hill-1"), FMC_ONE);
  EXPECT_EQ(fmcParse("Ann: PING   HILL-1  ", " hill-1 "), FMC_ONE);
  EXPECT_EQ(fmcParse("Ann: ping Hill-1", "Hill-2"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: ping Hill", "Hill-1"), FMC_NONE);       // a prefix is not a match
  EXPECT_EQ(fmcParse("Ann: ping Hill-1x", "Hill-1"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: ping two words", "two words"), FMC_ONE);
}

TEST(FmcParse, NothingElseIsAPing) {
  EXPECT_EQ(fmcParse("ping", "n1"), FMC_NONE);              // no sender: not a channel text
  EXPECT_EQ(fmcParse("Ann: pinging", "n1"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: pingn1", "n1"), FMC_NONE);        // the word is followed by a space, then the name
  EXPECT_EQ(fmcParse("Ann: pinging", "ging"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: ping-pong", "n1"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: please ping", "n1"), FMC_NONE);
  EXPECT_EQ(fmcParse("Ann: pong v1 up 3s", "n1"), FMC_NONE);   // a reply is never read as a ping
  EXPECT_EQ(fmcParse("Ann: ", "n1"), FMC_NONE);
}

TEST(FmcBuild, OnlyKnownFieldsAndNeverPastTheBuffer) {
  FmcFacts f;
  f.version = "v1.18.0-mecon.0.3.9";
  f.uptime_s = 3 * 86400 + 4 * 3600 + 5;
  f.battery_mv = 4052;
  f.temp_c = 22.54f;
  f.counts = true; f.rx = 120; f.tx = 45;
  char b[FMC_REPLY_MAX + 1];
  EXPECT_STREQ((fmcBuild(b, sizeof(b), f), b), "pong v1.18.0-mecon.0.3.9 up 3d4h bat 4.05V temp 22.5C rx 120 tx 45 ok");
  FmcFacts g;                       // nothing known but the uptime
  g.uptime_s = 42;
  EXPECT_STREQ((fmcBuild(b, sizeof(b), g), b), "pong up 42s");
  g.uptime_s = 125; g.counts = true; g.err_flags = 2; g.temp_c = -3.04f;
  EXPECT_STREQ((fmcBuild(b, sizeof(b), g), b), "pong up 2m5s temp -3.0C rx 0 tx 0 err 2");
  char tiny[12];
  const size_t n = fmcBuild(tiny, sizeof(tiny), f);
  EXPECT_LT(n, sizeof(tiny));
  EXPECT_EQ(strlen(tiny), n);
}

TEST(FmcPing, ABroadcastIsAnsweredOnceInFiveMinutesAfterItsJitter) {
  FmcPing p;
  EXPECT_TRUE(p.onText("A: ping", "n1", 1000, 12345, 1));
  EXPECT_FALSE(p.due(1000 + 12344));
  EXPECT_TRUE(p.due(1000 + 12345));
  EXPECT_FALSE(p.due(1000 + 12346));                                   // taken once
  const uint32_t sent = 1000 + 12345;                                   // the gap runs from the send
  EXPECT_FALSE(p.onText("A: ping", "n1", sent + 1000, 0, 2));           // inside five minutes
  EXPECT_FALSE(p.onText("A: ping", "n1", sent + FMC_BROADCAST_GAP_MS - 1, 0, 3));
  EXPECT_TRUE(p.onText("A: ping", "n1", sent + FMC_BROADCAST_GAP_MS, 0, 4));
}

TEST(FmcPing, TheJitterNeverPassesThirtySeconds) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 0xFFFFFFFFu, 1));
  EXPECT_TRUE(p.due(FMC_BROADCAST_JITTER_MS));
}

TEST(FmcPing, ATargetedPingIsNotHeldByTheBroadcastGapButHasItsOwn) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 0, 1));
  ASSERT_TRUE(p.due(0));
  EXPECT_TRUE(p.onText("A: ping n1", "n1", 100, 0, 2));
  ASSERT_TRUE(p.due(100));
  EXPECT_FALSE(p.onText("A: ping n1", "n1", 100 + FMC_TARGET_GAP_MS - 1, 0, 3));
  EXPECT_TRUE(p.onText("A: ping n1", "n1", 100 + FMC_TARGET_GAP_MS, 0, 4));
}

TEST(FmcPing, ThePingForAnotherNameSchedulesNothing) {
  FmcPing p;
  EXPECT_FALSE(p.onText("A: ping n2", "n1", 0, 0, 1));
  EXPECT_FALSE(p.pending());
}

TEST(FmcPing, TheSamePingAgainIsOneAnswerButANewOneUnderAnotherStampIsNot) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping n1", "n1", 0, 0, 7));
  ASSERT_TRUE(p.due(0));
  EXPECT_FALSE(p.onText("A: ping n1", "n1", 20000, 0, 7));   // after the gap, but the same ping
  EXPECT_TRUE(p.onText("A: ping n1", "n1", 20000, 0, 8));
}

TEST(FmcPing, OneReplyWaitsAtATimeAndADroppedOneDoesNotCountAgainstTheGap) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 5000, 1));
  EXPECT_FALSE(p.onText("B: ping n1", "n1", 10, 0, 2));       // the queue is one deep
  ASSERT_TRUE(p.due(5000));
  p.dropped();                                               // no airtime: nothing went out
  EXPECT_TRUE(p.onText("A: ping", "n1", 6000, 0, 3));
}

TEST(FmcPing, AReplyWithNoAirtimeWaitsAndTriesAgainThenGivesUp) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 0, 1));
  uint32_t t = 0;
  for (int i = 0; i < FMC_RETRIES; i++) {
    ASSERT_TRUE(p.due(t));
    ASSERT_TRUE(p.defer(t));
    EXPECT_FALSE(p.due(t + FMC_RETRY_MS - 1));
    t += FMC_RETRY_MS;
  }
  ASSERT_TRUE(p.due(t));
  EXPECT_FALSE(p.defer(t));                                          // the last try has failed: given up
  EXPECT_FALSE(p.pending());
  EXPECT_TRUE(p.onText("A: ping", "n1", t + 1, 0, 2));               // and the gap was not used up
}

TEST(FmcPing, ThePingIsHeldByTheGapFromTheSendNotFromTheArrival) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 20000, 1));               // sent at 20 s after a jitter
  ASSERT_TRUE(p.due(20000));
  EXPECT_FALSE(p.onText("A: ping", "n1", 20000 + FMC_BROADCAST_GAP_MS - 1, 0, 2));
}

TEST(FmcPing, ACancelledReplyIsGone) {
  FmcPing p;
  ASSERT_TRUE(p.onText("A: ping", "n1", 0, 100, 1));
  p.cancel();
  EXPECT_FALSE(p.due(100000));
}

TEST(FmcPing, TheClockMayWrap) {
  FmcPing p;
  const uint32_t t = 0xFFFFFF00u;
  ASSERT_TRUE(p.onText("A: ping", "n1", t, 0x400, 1));
  EXPECT_FALSE(p.due(t + 0x3FF));
  EXPECT_TRUE(p.due(t + 0x400));
  EXPECT_FALSE(p.onText("A: ping", "n1", t + 0x500, 0, 2));    // inside the gap across the wrap
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
