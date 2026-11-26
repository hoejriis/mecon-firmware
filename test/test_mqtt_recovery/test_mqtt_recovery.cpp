// The reconnect recovery rule (src/helpers/mqtt/MqttRecovery.h, Issue #651): failed broker connects under a low heap ask
// for one restart, at most once an hour; a success starts the count over; without a low heap there is never a restart.
#include <gtest/gtest.h>
#include "helpers/mqtt/MqttRecovery.h"

namespace {

const uint32_t HOUR = MqttRecovery::MIN_INTERVAL_MS;
const uint8_t N = MqttRecovery::FAILS_NEEDED;

// Fails `n` attempts, 30 s apart from `t`, and returns whether any of them asked for a restart.
bool failMany(MqttRecovery& r, uint32_t& t, int n, bool low) {
  bool asked = false;
  for (int i = 0; i < n; i++) { t += 30000; asked |= r.onFailed(t, low); }
  return asked;
}

TEST(MqttRecovery, ConstantsAreTheDocumentedOnes) {
  EXPECT_EQ(5, N);
  EXPECT_EQ(3600000UL, HOUR);
}

TEST(MqttRecovery, NFailuresUnderALowHeapAskForARestartOnTheNth) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;   // past the first hour of uptime
  EXPECT_FALSE(failMany(r, t, N - 1, true));   // N-1 is not enough
  EXPECT_TRUE(r.onFailed(t += 30000, true));   // the Nth is
}

TEST(MqttRecovery, FailuresWithoutALowHeapNeverAskForARestart) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;
  EXPECT_FALSE(failMany(r, t, 50, false));   // a broker that is simply down, with a healthy heap
  EXPECT_EQ(50, r.fails());
}

TEST(MqttRecovery, ALowHeapWithFewFailuresIsNotEnough) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;
  EXPECT_FALSE(failMany(r, t, N - 1, true));
}

TEST(MqttRecovery, TheHeapIsLookedAtOnTheFailureThatWouldTrigger) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;
  EXPECT_FALSE(failMany(r, t, N + 3, false));   // many failures while the heap was fine
  EXPECT_TRUE(r.onFailed(t += 30000, true));     // the first one that finds it low asks
}

TEST(MqttRecovery, ASuccessStartsTheCountOver) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;
  EXPECT_FALSE(failMany(r, t, N - 1, true));
  r.onConnected();
  EXPECT_EQ(0, r.fails());
  EXPECT_FALSE(failMany(r, t, 1, true));       // one failure after the success is the first, not the Nth
  EXPECT_FALSE(failMany(r, t, N - 2, true));
  EXPECT_TRUE(r.onFailed(t += 30000, true));   // and N after it asks
}

TEST(MqttRecovery, NoRestartInTheFirstHourAfterBoot) {
  MqttRecovery r; r.begin();
  uint32_t t = 0;
  EXPECT_FALSE(failMany(r, t, 40, true));      // 20 minutes of failures under a low heap
  EXPECT_LT(t, HOUR);
  t = HOUR - 1;
  EXPECT_FALSE(r.onFailed(t, true));
  EXPECT_TRUE(r.onFailed(HOUR, true));          // the hour is up: the count is long past N
}

TEST(MqttRecovery, AtMostOneRestartAnHour) {
  MqttRecovery r; r.begin();
  uint32_t t = HOUR;
  EXPECT_TRUE(failMany(r, t, N, true));
  const uint32_t restart_at = t;
  // A device that did not restart (or a caller that asks again) gets no second answer inside the hour.
  EXPECT_FALSE(failMany(r, t, 100, true));
  EXPECT_LT(t - restart_at, HOUR);
  EXPECT_FALSE(r.onFailed(restart_at + HOUR - 1, true));
  EXPECT_TRUE(r.onFailed(restart_at + HOUR, true));   // an hour on, with the failures still counted, it asks again
}

TEST(MqttRecovery, TheCapSurvivesTheMillisecondCounterWrapping) {
  MqttRecovery r; r.begin();
  r.tick(HOUR);                                 // armed once, early
  uint32_t t = 0xFFFFFF00u;                     // about 49.7 days on
  r.tick(t);
  t += 0x200;                                   // the counter has wrapped to a small number
  EXPECT_TRUE(failMany(r, t, N, true));         // still armed: the wrap did not close it for another hour
}

TEST(MqttRecovery, TheFailureCountSaturatesInsteadOfWrapping) {
  MqttRecovery r; r.begin();
  for (int i = 0; i < 300; i++) r.onFailed(1000, true);   // inside the first hour: nothing fires, the count keeps counting
  EXPECT_EQ(255, r.fails());
  EXPECT_TRUE(r.onFailed(HOUR, true));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
