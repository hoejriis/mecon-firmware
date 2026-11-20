// The battery level classifier (src/helpers/BatteryLevel.h): thresholds, hysteresis, the 60 s mean, unknown and invalid
// readings, and the change counter.
#include <gtest/gtest.h>
#include <stdint.h>
#include "helpers/BatteryLevel.h"

namespace {
// One reading every 5 s, the way the loop feeds it.
struct Feed {
  BatteryLevel b;
  uint32_t now = 0;
  void one(uint16_t mv) { b.sample(now, mv); now += BatteryLevel::SAMPLE_MS; }
  void window(uint16_t mv) { for (int i = 0; i < BatteryLevel::WINDOW_READINGS; i++) one(mv); }
};
}  // namespace

TEST(BatteryLevel, UnknownUntilAWindowIsFull) {
  Feed f;
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
  for (int i = 0; i < BatteryLevel::WINDOW_READINGS - 1; i++) f.one(4000);
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
  f.one(4000);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
}

TEST(BatteryLevel, TheThresholdsAreStrict) {
  { Feed f; f.window(3550); EXPECT_EQ(BatteryLevel::OK, f.b.level()); }
  { Feed f; f.window(3549); EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level()); }
  { Feed f; f.window(3400); EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level()); }
  { Feed f; f.window(3399); EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level()); }
}

TEST(BatteryLevel, GoesDownAtOnceAndUpOnlyWithHysteresis) {
  Feed f;
  f.window(4000); EXPECT_EQ(BatteryLevel::OK, f.b.level());
  f.window(3500); EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  f.window(3649); EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());   // above 3550 but not 100 mV above
  f.window(3650); EXPECT_EQ(BatteryLevel::OK, f.b.level());
  f.window(3300); EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());    // ok straight to critical
  f.window(3499); EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3500); EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());   // 3400 + 100
  f.window(3300); EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3650); EXPECT_EQ(BatteryLevel::OK, f.b.level());          // critical straight to ok
}

TEST(BatteryLevel, ADipShorterThanTheWindowDoesNotTripIt) {
  Feed f;
  f.window(4000);
  // a TX sag: 3 of the 12 readings at 3000 mV, the rest at 4000: mean 3750
  for (int i = 0; i < 3; i++) f.one(3000);
  for (int i = 0; i < 9; i++) f.one(4000);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
  // the level follows the mean, not the last reading
  for (int i = 0; i < 11; i++) f.one(3300);
  f.one(4000);                 // mean 3358: critical, though the last reading is high
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  for (int i = 0; i < 11; i++) f.one(4000);
  f.one(3000);                 // mean 3917: ok, though the last reading is low
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
}

TEST(BatteryLevel, TheLevelOnlyChangesWhenAWindowCloses) {
  Feed f;
  f.window(4000);
  for (int i = 0; i < BatteryLevel::WINDOW_READINGS - 1; i++) f.one(3000);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
  f.one(3000);
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
}

TEST(BatteryLevel, InvalidReadingsAreLeftOutOfTheMean) {
  Feed f;
  for (int i = 0; i < 4; i++) f.one(0);            // a board that cannot measure
  for (int i = 0; i < 8; i++) f.one(4000);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());        // 8 valid readings: enough, and the zeros did not pull the mean down
  f.window(4000);
  for (int i = 0; i < 2; i++) f.one(65535);        // out of range above
  for (int i = 0; i < 2; i++) f.one(1999);         // out of range below
  for (int i = 0; i < 8; i++) f.one(4200);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
}

TEST(BatteryLevel, TheRangeEdgesAreValid) {
  { Feed f; f.window(2000); EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level()); }
  { Feed f; f.window(4600); EXPECT_EQ(BatteryLevel::OK, f.b.level()); }
  { Feed f; f.window(1999); EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level()); }
  { Feed f; f.window(4601); EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level()); }
}

TEST(BatteryLevel, NoValidReadingIsUnknownNeverCritical) {
  Feed f;
  f.window(0);
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
  f.window(4000);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
  f.window(0);
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
}

TEST(BatteryLevel, FewerThanEightValidReadingsIsUnknown) {
  Feed f;
  f.window(4000);
  for (int i = 0; i < 5; i++) f.one(0);
  for (int i = 0; i < 7; i++) f.one(4000);
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
}

TEST(BatteryLevel, AFaultKeepsTheHysteresisOfTheLastLevel) {
  Feed f;
  f.window(3300);
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(0);
  EXPECT_EQ(BatteryLevel::UNKNOWN, f.b.level());
  f.window(3450);                                  // above 3400 but not 100 mV above: still critical
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
}

TEST(BatteryLevel, CountsEveryChangeOfTheReportedLevel) {
  Feed f;
  EXPECT_EQ(0u, f.b.changes());
  f.window(4000);                                  // unknown to ok
  EXPECT_EQ(1u, f.b.changes());
  f.window(4000);
  EXPECT_EQ(1u, f.b.changes());                    // no change, no count
  f.window(3500);
  EXPECT_EQ(2u, f.b.changes());
  f.window(3300);
  EXPECT_EQ(3u, f.b.changes());
  f.window(0);                                     // to unknown is a change too
  EXPECT_EQ(4u, f.b.changes());
  f.window(3300);
  EXPECT_EQ(5u, f.b.changes());
  f.window(3300);
  EXPECT_EQ(5u, f.b.changes());
}

TEST(BatteryLevel, AReadingIsDueEveryFiveSecondsAcrossAMillisWrap) {
  BatteryLevel b;
  EXPECT_TRUE(b.due(0));
  b.sample(0xFFFFF000u, 4000);
  EXPECT_FALSE(b.due(0xFFFFF000u + 4999));
  EXPECT_TRUE(b.due(0xFFFFF000u + 5000));
  EXPECT_TRUE(b.due(0x00001000u));                 // after the wrap
}

TEST(BatteryLevel, TheNamesAndTheMethod) {
  EXPECT_STREQ("ok", BatteryLevel::name(BatteryLevel::OK));
  EXPECT_STREQ("low", BatteryLevel::name(BatteryLevel::LOW_LEVEL));
  EXPECT_STREQ("critical", BatteryLevel::name(BatteryLevel::CRITICAL));
  EXPECT_STREQ("unknown", BatteryLevel::name(BatteryLevel::UNKNOWN));
  EXPECT_STREQ("mv_mean_60s", BatteryLevel::method());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
