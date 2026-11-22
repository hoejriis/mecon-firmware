// The battery guard's protective sleep (src/helpers/BatterySleep.h) and the busy gate (src/helpers/BusyGate.h): when the
// loop sleeps, what a wake decides, the block in RTC memory, probation and back-off.
#include <gtest/gtest.h>
#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/BatterySleep.h"

using namespace BatterySleep;

namespace {

// A level tracker fed whole 60 s windows of one reading each.
struct Feed {
  BatteryLevel b;
  uint32_t now = 0;
  void window(uint16_t mv) {
    for (int i = 0; i < BatteryLevel::WINDOW_READINGS; i++) {
      b.sample(now, mv);
      now += BatteryLevel::SAMPLE_MS;
    }
  }
  void on() { b.settings().enabled = 1; }
};

// The board as the boot guard sees it: readings from a script (the last one repeats), waits and sleeps recorded.
struct Board {
  std::vector<uint16_t> script;
  size_t reads = 0;
  std::vector<uint32_t> waits, sleeps;
  bool arm_ok = true;
  Io io() {
    Io io;
    io.read = [](void* u) -> uint16_t {
      Board* b = static_cast<Board*>(u);
      const uint16_t v = b->script.empty() ? 0 : b->script[b->reads < b->script.size() ? b->reads : b->script.size() - 1];
      b->reads++;
      return v;
    };
    io.wait = [](void* u, uint32_t ms) { static_cast<Board*>(u)->waits.push_back(ms); };
    io.sleep = [](void* u, uint32_t secs) -> bool {
      Board* b = static_cast<Board*>(u);
      if (!b->arm_ok) return false;
      b->sleeps.push_back(secs);
      return true;
    };
    io.user = this;
    return io;
  }
  // n bursts of one value each, then `rest`
  void bursts(std::initializer_list<uint16_t> each) {
    for (uint16_t v : each)
      for (int i = 0; i < BURST_READINGS; i++) script.push_back(v);
  }
};

struct Busy {
  BusyGate::Inputs in = {false, false, false, false};
  static const char* fn(void* u, bool emergency) { return BusyGate::reason(static_cast<Busy*>(u)->in, emergency); }
};

// A block as the loop leaves it with the defaults on: 3550 / 3400 / 100, wake 900.
State armedBlock() {
  State s;
  clear(s);
  s.enabled = 1;
  s.low_mv = 3550;
  s.critical_mv = 3400;
  s.margin_mv = 100;
  s.wake_s = 900;
  s.interval_s = 900;
  seal(s);
  return s;
}

State asleepBlock() {
  State s = armedBlock();
  markAsleep(s, E_CRITICAL);
  return s;
}

std::string eff(const Controller& c) {
  char b[48];
  c.effective(b, sizeof(b));
  return b;
}

// Run the controller for one window at mv; returns the seconds it asked to sleep (0: none).
// A pass now and one SETTLE_MS later, the last check before a sleep.
uint32_t settled(Feed& f, Controller& c, State& s, Busy* busy = nullptr, bool external = false, uint8_t note = V_NONE) {
  const uint32_t r = c.tick(f.now, s, f.b, external, 0, busy ? Busy::fn : nullptr, busy, note);
  return r ? r : c.tick(f.now + SETTLE_MS, s, f.b, external, 0, busy ? Busy::fn : nullptr, busy, note);
}
uint32_t windowTick(Feed& f, Controller& c, State& s, uint16_t mv, Busy* busy = nullptr, bool external = false) {
  f.window(mv);
  return settled(f, c, s, busy, external);
}

}  // namespace

// ---------------------------------------------------------------- the busy gate

TEST(BusyGate, EachInputHoldsTheSleepAndOnlyASaveHoldsItAtTheEmergencyLevel) {
  BusyGate::Inputs in = {false, false, false, false};
  EXPECT_EQ(BusyGate::reason(in, false), nullptr);
  in.job_open = true;
  EXPECT_STREQ(BusyGate::reason(in, false), "job-open");
  EXPECT_EQ(BusyGate::reason(in, true), nullptr);
  in = {false, false, true, false};
  EXPECT_STREQ(BusyGate::reason(in, false), "update-transfer");
  EXPECT_EQ(BusyGate::reason(in, true), nullptr);
  in = {false, false, false, true};
  EXPECT_STREQ(BusyGate::reason(in, false), "update-trial");
  EXPECT_EQ(BusyGate::reason(in, true), nullptr);
  in = {true, true, true, true};
  EXPECT_STREQ(BusyGate::reason(in, false), "flash-write");
  EXPECT_STREQ(BusyGate::reason(in, true), "flash-write");
}

TEST(BusyGate, AFlashScopeCountsForItsLength) {
  EXPECT_EQ(BusyGate::flashWrites(), 0);
  {
    BusyGate::FlashScope a;
    EXPECT_EQ(BusyGate::flashWrites(), 1);
    { BusyGate::FlashScope b; EXPECT_EQ(BusyGate::flashWrites(), 2); }
    EXPECT_EQ(BusyGate::flashWrites(), 1);
  }
  EXPECT_EQ(BusyGate::flashWrites(), 0);
}

// ---------------------------------------------------------------- the block

TEST(Block, ASealedBlockIsValidAndAnyChangedByteIsNot) {
  State s = armedBlock();
  EXPECT_TRUE(valid(s));
  for (size_t i = 0; i < offsetof(State, crc); i++) {
    State d = s;
    reinterpret_cast<uint8_t*>(&d)[i] ^= 0x10;
    EXPECT_FALSE(valid(d)) << "byte " << i;
  }
}

TEST(Block, TheRecoveryVoltageIsDerivedAndABumpIsCapped) {
  EXPECT_EQ(recoverMv(3550, 3400, 100, 0), 3650);   // low + margin
  EXPECT_EQ(recoverMv(3450, 3350, 50, 0), 3600);    // critical + 250 is the larger
  EXPECT_EQ(recoverMv(3550, 3400, 100, 50), 3700);
  EXPECT_EQ(recoverMv(3550, 3400, 100, 1000), 3850);   // the bump is at most 200
  EXPECT_EQ(recoverMv(3900, 3700, 100, 200), 4000);    // and never takes it above 4000
  EXPECT_EQ(recoverMv(4100, 4000, 100, 200), 4250);    // unless the base is already there: never below the base
}

// ---------------------------------------------------------------- the guard off

TEST(Off, WithTheGuardOffNothingSleepsAndNoBlockIsKeptWhateverTheBattery) {
  Feed f;
  Controller c;
  State s;
  memset(&s, 0xA5, sizeof(s));   // a power-on's random RTC memory
  for (int i = 0; i < 30; i++) {
    EXPECT_EQ(windowTick(f, c, s, 2900), 0u) << "window " << i;
  }
  EXPECT_EQ(f.b.level(), BatteryLevel::CRITICAL);
  EXPECT_EQ(eff(c), "off");
  EXPECT_FALSE(valid(s));
  EXPECT_EQ(s.magic, 0u);
  EXPECT_EQ(c.state(), nullptr);
}

TEST(Off, TheBootGuardWithNoBlockReadsNothingAndGoesOn) {
  State s;
  memset(&s, 0x5A, sizeof(s));
  Board b;
  b.script = {3000};
  Boot r = boot(s, W_TIMER, false, b.io());
  EXPECT_EQ(r.outcome, O_CONTINUE);
  EXPECT_EQ(r.note, V_NONE);
  EXPECT_EQ(b.reads, 0u);
  EXPECT_TRUE(b.sleeps.empty());
  clear(s);
  r = boot(s, W_BROWNOUT, false, b.io());
  EXPECT_EQ(r.outcome, O_CONTINUE);
  EXPECT_EQ(b.reads, 0u);
}

// ---------------------------------------------------------------- entering the sleep

TEST(Entry, ACriticalLevelSleepsOnlyAfterThreeWindows) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  EXPECT_EQ(windowTick(f, c, s, 3700), 0u);
  EXPECT_EQ(eff(c), "armed");
  EXPECT_EQ(windowTick(f, c, s, 3300), 0u);
  EXPECT_EQ(eff(c), "confirming");
  EXPECT_EQ(windowTick(f, c, s, 3300), 0u);
  EXPECT_EQ(windowTick(f, c, s, 3300), 900u);
  EXPECT_EQ(s.phase, PH_ASLEEP);
  EXPECT_EQ(s.entry, E_CRITICAL);
  EXPECT_EQ(s.sleeps, 1);
  EXPECT_TRUE(valid(s));
}

TEST(Entry, TheLastCheckComesTwoSecondsAfterTheFirstAndAJobOpenedMeanwhileHoldsIt) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  Busy busy;
  for (int i = 0; i < 3; i++) {
    f.window(3300);
    EXPECT_EQ(c.tick(f.now, s, f.b, false, 0, Busy::fn, &busy), 0u);
  }
  EXPECT_EQ(c.tick(f.now + SETTLE_MS - 1, s, f.b, false, 0, Busy::fn, &busy), 0u);
  busy.in.job_open = true;
  EXPECT_EQ(c.tick(f.now + SETTLE_MS, s, f.b, false, 0, Busy::fn, &busy), 0u);
  busy.in.job_open = false;
  EXPECT_EQ(c.tick(f.now + SETTLE_MS + 1, s, f.b, false, 0, Busy::fn, &busy), 0u) << "the two seconds start again";
  EXPECT_EQ(c.tick(f.now + 2 * SETTLE_MS + 1, s, f.b, false, 0, Busy::fn, &busy), 900u);
}

TEST(Entry, AWindowBackAboveCriticalStartsTheCountAgain) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  windowTick(f, c, s, 3300);
  windowTick(f, c, s, 3300);
  EXPECT_EQ(windowTick(f, c, s, 3600), 0u);   // 3600 >= critical + margin: low again
  EXPECT_EQ(windowTick(f, c, s, 3300), 0u);
  EXPECT_EQ(windowTick(f, c, s, 3300), 0u);
  EXPECT_EQ(windowTick(f, c, s, 3300), 900u);
}

TEST(Entry, AnUnknownLevelNeverSleeps) {
  State s;
  Feed g;
  g.on();
  Controller d;
  clear(s);
  for (int i = 0; i < 10; i++) EXPECT_EQ(windowTick(g, d, s, 0), 0u);   // a board that cannot measure
  EXPECT_EQ(eff(d), "blocked:level-unknown");
  for (int i = 0; i < 10; i++) EXPECT_EQ(windowTick(g, d, s, 1500), 0u);   // out of range: a sensor fault
  EXPECT_EQ(eff(d), "blocked:level-unknown");
}

TEST(Entry, ExternalPowerNeverSleeps) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  for (int i = 0; i < 6; i++) EXPECT_EQ(windowTick(f, c, s, 3000, nullptr, true), 0u);
  EXPECT_EQ(eff(c), "blocked:external-power");
}

TEST(Entry, AnOpenJobATransferOrATrialHoldsTheSleep) {
  for (int which = 0; which < 4; which++) {
    Feed f;
    f.on();
    Controller c;
    State s;
    clear(s);
    Busy busy;
    bool* flag[4] = {&busy.in.job_open, &busy.in.update_transfer, &busy.in.update_trial, &busy.in.flash_write};
    *flag[which] = true;
    for (int i = 0; i < 6; i++) EXPECT_EQ(windowTick(f, c, s, 3300, &busy), 0u) << which;
    EXPECT_EQ(eff(c).rfind("blocked:", 0), 0u) << eff(c);
    EXPECT_NE(s.phase, PH_ASLEEP);
    *flag[which] = false;
    EXPECT_EQ(settled(f, c, s, &busy), 900u) << "sleeps once it is clear";
  }
}

TEST(Entry, TheEmergencyLevelSleepsAtOnceThroughAJobButNotThroughASave) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  Busy busy;
  busy.in.job_open = busy.in.update_trial = busy.in.flash_write = true;
  EXPECT_EQ(windowTick(f, c, s, 3200, &busy), 0u);   // 3200 < 3400 - 150
  EXPECT_EQ(eff(c), "blocked:flash-write");
  busy.in.flash_write = false;
  EXPECT_EQ(settled(f, c, s, &busy), 900u);
  EXPECT_EQ(s.entry, E_EMERGENCY);
  // 3260 is not 150 under: it waits for the confirmation and for the job
  Feed g;
  g.on();
  Controller d;
  clear(s);
  busy.in.update_trial = false;
  for (int i = 0; i < 5; i++) EXPECT_EQ(windowTick(g, d, s, 3260, &busy), 0u);
  EXPECT_EQ(eff(d), "blocked:job-open");
}

TEST(Entry, ATimerThatCannotBeArmedLeavesTheDeviceUpAndTriesAtTheNextWindow) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  for (int i = 0; i < 2; i++) windowTick(f, c, s, 3300);
  ASSERT_EQ(windowTick(f, c, s, 3300), 900u);
  c.sleepFailed(s);
  EXPECT_TRUE(valid(s));
  EXPECT_NE(s.phase, PH_ASLEEP);
  EXPECT_EQ(s.sleeps, 0);
  EXPECT_EQ(s.recovery, V_TIMER_FAILED);
  EXPECT_EQ(eff(c), "blocked:timer-failed");
  EXPECT_EQ(c.tick(f.now + 10, s, f.b, false, 0, nullptr, nullptr), 0u) << "not again in the same window";
  EXPECT_EQ(windowTick(f, c, s, 3300), 900u) << "the next window tries again";
}

TEST(Entry, TurningTheGuardOffClearsTheBlock) {
  Feed f;
  f.on();
  Controller c;
  State s;
  clear(s);
  windowTick(f, c, s, 3700);
  ASSERT_NE(c.state(), nullptr);
  EXPECT_EQ(c.state()->critical_mv, 3400);
  f.b.settings().critical_mv = 3300;
  c.tick(f.now, s, f.b, false, 0, nullptr, nullptr);
  EXPECT_EQ(s.critical_mv, 3300) << "the block follows the settings";
  f.b.settings().enabled = 0;
  c.tick(f.now, s, f.b, false, 0, nullptr, nullptr);
  EXPECT_EQ(s.magic, 0u);
  EXPECT_EQ(c.state(), nullptr);
}

// ---------------------------------------------------------------- the wake

TEST(Wake, BelowTheRecoveryVoltageItSleepsAgainAfterOneBurst) {
  State s = asleepBlock();
  Board b;
  b.bursts({3640});   // under 3650
  const Boot r = boot(s, W_TIMER, false, b.io());
  EXPECT_EQ(r.outcome, O_SLEPT);
  EXPECT_EQ(b.reads, (size_t)BURST_READINGS);
  ASSERT_EQ(b.sleeps.size(), 1u);
  EXPECT_EQ(b.sleeps[0], 900u);
  EXPECT_EQ(s.phase, PH_ASLEEP);
  EXPECT_EQ(s.sleeps, 2);
  EXPECT_TRUE(valid(s));
}

TEST(Wake, OneBurstAboveIsNotARecoveryAllThreeMustBe) {
  State s = asleepBlock();
  Board b;
  b.bursts({3700, 3700, 3600});   // a rebound, then the cell sags again
  EXPECT_EQ(boot(s, W_TIMER, false, b.io()).outcome, O_SLEPT);
  EXPECT_EQ(s.phase, PH_ASLEEP);
  s = asleepBlock();
  Board c;
  c.bursts({3700, 3600});
  EXPECT_EQ(boot(s, W_TIMER, false, c.io()).outcome, O_SLEPT);
  s = asleepBlock();
  Board d;
  d.bursts({3700, 3700, 3650});
  EXPECT_EQ(boot(s, W_TIMER, false, d.io()).outcome, O_CONTINUE);
  EXPECT_EQ(d.reads, 3u * BURST_READINGS);
  EXPECT_EQ(std::count(d.waits.begin(), d.waits.end(), BURST_GAP_MS), 2);
  EXPECT_TRUE(d.sleeps.empty());
  EXPECT_EQ(s.phase, PH_PROBATION);
  EXPECT_EQ(s.recovery, V_VOLTAGE);
}

TEST(Wake, ABurstDropsItsExtremesAndNeedsTwelveValidReadings) {
  Board b;
  b.script = {3000, 3000, 3000, 3000, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 4500, 4500, 4500, 4500};
  EXPECT_EQ(burst(b.io()), 3700);
  Board c;   // 5 invalid of 16: no measurement
  c.script = {0, 0, 0, 0, 0, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 3700, 3700};
  EXPECT_EQ(burst(c.io()), 0);
  Board d;   // 4 invalid: 12 valid, 3 dropped at each end
  d.script = {0, 9000, 1000, 0, 3600, 3600, 3600, 3700, 3700, 3700, 3700, 3700, 3700, 3800, 3800, 3800};
  EXPECT_EQ(burst(d.io()), 3700);
}

TEST(Wake, AWakeThatCannotMeasureBootsAndNeverSleeps) {
  State s = asleepBlock();
  Board b;
  b.script = {0};
  EXPECT_EQ(boot(s, W_TIMER, false, b.io()).outcome, O_CONTINUE);
  EXPECT_TRUE(b.sleeps.empty());
  EXPECT_EQ(s.recovery, V_SENSOR_INVALID);
  EXPECT_EQ(s.phase, PH_NONE);
}

TEST(Wake, ATimerThatCannotBeArmedOnAWakeBoots) {
  State s = asleepBlock();
  Board b;
  b.bursts({3500});
  b.arm_ok = false;
  EXPECT_EQ(boot(s, W_TIMER, false, b.io()).outcome, O_CONTINUE);
  EXPECT_EQ(s.recovery, V_TIMER_FAILED);
  EXPECT_EQ(s.phase, PH_NONE);
  EXPECT_TRUE(valid(s));
}

TEST(Wake, TheButtonGivesALeaseAndAResetBootsNormally) {
  State s = asleepBlock();
  Board b;
  b.bursts({3000});
  Boot r = boot(s, W_BUTTON, false, b.io());
  EXPECT_EQ(r.outcome, O_CONTINUE);
  EXPECT_EQ(r.note, V_BUTTON);
  EXPECT_EQ(b.reads, 0u);
  EXPECT_EQ(s.recovery, V_BUTTON);
  // in the loop: no sleep for the lease, then the guard is back
  Feed f;
  f.on();
  Controller c;
  uint32_t asked = 0;
  for (int i = 0; i < 9; i++) {
    f.window(3300);
    asked = settled(f, c, s, nullptr, false, r.note);
    EXPECT_EQ(asked, 0u) << i;
  }
  EXPECT_EQ(eff(c), "blocked:lease");
  for (int i = 0; i < 4 && !asked; i++) asked = windowTick(f, c, s, 3300);
  EXPECT_EQ(asked, 900u);
  s = asleepBlock();
  r = boot(s, W_OTHER, false, b.io());
  EXPECT_EQ(r.outcome, O_CONTINUE);
  EXPECT_EQ(r.note, V_NONE);
  EXPECT_EQ(s.recovery, V_RESET);
  EXPECT_EQ(s.phase, PH_NONE);
}

TEST(Wake, ADamagedBlockIsClearedAndTheBootMeasuresAfresh) {
  State s = asleepBlock();
  s.interval_s = 60;   // not resealed
  Board b;
  b.bursts({3000});
  const Boot r = boot(s, W_TIMER, false, b.io());
  EXPECT_EQ(r.outcome, O_CONTINUE);
  EXPECT_EQ(r.note, V_STATE_LOST);
  EXPECT_EQ(b.reads, 0u);
  EXPECT_EQ(s.magic, 0u);
}

TEST(Wake, ABrownoutResetBelowTheRecoveryVoltageSleeps) {
  State s = armedBlock();
  Board b;
  b.bursts({3600});
  EXPECT_EQ(boot(s, W_BROWNOUT, false, b.io()).outcome, O_SLEPT);
  EXPECT_EQ(s.entry, E_BROWNOUT);
  s = armedBlock();
  Board c;
  c.bursts({3700});
  EXPECT_EQ(boot(s, W_BROWNOUT, false, c.io()).outcome, O_CONTINUE);
  s = armedBlock();
  Board d;   // on trial: a sleep would roll the update back, so only the emergency level
  d.bursts({3500});
  EXPECT_EQ(boot(s, W_BROWNOUT, true, d.io()).outcome, O_CONTINUE);
  Board e;
  e.bursts({3200});
  EXPECT_EQ(boot(s, W_BROWNOUT, true, e.io()).outcome, O_SLEPT);
  s = armedBlock();
  Board g;
  g.bursts({3000});
  EXPECT_EQ(boot(s, W_OTHER, false, g.io()).outcome, O_CONTINUE) << "a plain reset leaves it to the loop";
  EXPECT_EQ(g.reads, 0u);
}

// ---------------------------------------------------------------- probation and back-off

TEST(Probation, ARelapseDoublesTheIntervalAndRaisesTheBarAWholeProbationResetsThem) {
  State s = asleepBlock();
  s.phase = PH_PROBATION;
  seal(s);
  markAsleep(s, E_CRITICAL);
  EXPECT_EQ(s.failed, 1);
  EXPECT_EQ(s.interval_s, 1800);
  EXPECT_EQ(recoverMv(s), 3700);
  s.phase = PH_PROBATION;
  markAsleep(s, E_CRITICAL);
  EXPECT_EQ(s.failed, 2);
  EXPECT_EQ(s.interval_s, 3600);
  EXPECT_EQ(recoverMv(s), 3750);
  s.phase = PH_PROBATION;
  markAsleep(s, E_CRITICAL);
  EXPECT_EQ(s.interval_s, 3600) << "capped at the maximum";
  // through the controller: a whole probation in the loop resets the back-off
  s.phase = PH_PROBATION;
  seal(s);
  Feed f;
  f.on();
  Controller c;
  f.window(3700);
  c.tick(f.now, s, f.b, false, 0, nullptr, nullptr);
  EXPECT_EQ(eff(c), "probation");
  while (f.now < PROBATION_MS + 60000) f.window(3700);
  c.tick(f.now, s, f.b, false, 0, nullptr, nullptr);
  EXPECT_EQ(s.phase, PH_NONE);
  EXPECT_EQ(s.failed, 0);
  EXPECT_EQ(s.interval_s, 900);
  EXPECT_EQ(recoverMv(s), 3650);
  EXPECT_EQ(eff(c), "armed");
}

TEST(Probation, ARelapseInTheLoopIsCountedAsAFailedRecovery) {
  State s = asleepBlock();
  s.phase = PH_PROBATION;
  seal(s);
  Feed f;
  f.on();
  Controller c;
  uint32_t asked = 0;
  for (int i = 0; i < 4 && !asked; i++) asked = windowTick(f, c, s, 3300);
  EXPECT_EQ(asked, 1800u);
  EXPECT_EQ(s.failed, 1);
}

TEST(Probation, AfterTwentyFailedRecoveriesTheIntervalStaysAtItsMaximum) {
  State s = asleepBlock();
  for (int i = 0; i < 25; i++) {
    s.phase = PH_PROBATION;
    markAsleep(s, E_CRITICAL);
  }
  EXPECT_EQ(s.failed, FAIL_CAP);
  EXPECT_EQ(s.interval_s, INTERVAL_MAX_S);
  EXPECT_EQ(recoverMv(s), 3850);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
