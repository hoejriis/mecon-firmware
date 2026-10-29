// The job table (src/helpers/iface/Jobs.h, docs/contract/OPERATIONS.md section 3): one open job, owed results and
// their eviction, the never-twice ring, the confirm deadline and boot rules, and the persisted image.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/Jobs.h"

using namespace Iface;

static Job* openTerminal(JobTable& t, const std::string& id, uint8_t state = JOB_APPLIED) {
  Job* j = t.open(id.c_str(), "config.apply");
  EXPECT_NE(nullptr, j) << id;
  if (j) EXPECT_TRUE(t.setState(j, state));
  return j;
}

TEST(Jobs, OneStateChangingJobIsOpenAtATime) {
  JobTable t;
  Job* a = t.open("a", "config.apply");
  ASSERT_NE(nullptr, a);
  EXPECT_EQ(JOB_ACCEPTED, a->state);
  EXPECT_EQ(nullptr, t.open("b", "config.apply")) << "a second job while one is open is busy";
  EXPECT_EQ(a, t.openJob());
  ASSERT_TRUE(t.setState(a, JOB_PROGRESS));
  EXPECT_EQ(nullptr, t.open("b", "config.apply"));
  ASSERT_TRUE(t.setState(a, JOB_APPLIED));
  EXPECT_EQ(nullptr, t.openJob());
  EXPECT_NE(nullptr, t.open("b", "config.apply")) << "a terminal job leaves the way free";
}

TEST(Jobs, AKnownIdIsNeverOpenedTwice) {
  JobTable t;
  openTerminal(t, "same");
  EXPECT_EQ(nullptr, t.open("same", "config.apply"));
  Job* j = nullptr;
  EXPECT_EQ(JobTable::FOUND, t.find("same", &j));
  EXPECT_STREQ("same", j->id);
}

TEST(Jobs, ATerminalJobNeverMovesAgain) {
  JobTable t;
  Job* j = openTerminal(t, "t", JOB_FAILED);
  for (uint8_t s : {JOB_ACCEPTED, JOB_PROGRESS, JOB_APPLIED, JOB_CONFIRMED, JOB_REVERTED, JOB_CANCELLED})
    EXPECT_FALSE(t.setState(j, s)) << (int)s;
  EXPECT_FALSE(t.awaitConfirm(j, 300, true));
  EXPECT_EQ(JOB_FAILED, j->state);
}

TEST(Jobs, AwaitingIsEnteredOnlyWithADeadline) {
  JobTable t;
  Job* j = t.open("c", "config.apply");
  EXPECT_FALSE(t.setState(j, JOB_AWAITING)) << "only awaitConfirm enters it";
  EXPECT_FALSE(t.awaitConfirm(j, 0, true)) << "no deadline, no pending change";
  ASSERT_TRUE(t.awaitConfirm(j, 300, true));
  EXPECT_EQ(JOB_AWAITING, j->state);
  EXPECT_FALSE(jobTerminal(j->state));
}

TEST(Jobs, EvictionTakesTheOldestAcknowledgedThenTheOldestTerminal) {
  JobTable t;
  for (int i = 0; i < JobTable::SLOTS; i++) openTerminal(t, "j" + std::to_string(i));
  EXPECT_EQ(JobTable::SLOTS, t.owedCount());
  ASSERT_TRUE(t.ack("j3"));
  ASSERT_TRUE(t.ack("j5"));
  openTerminal(t, "n1");   // takes j3: acknowledged, and older than j5
  Job* j = nullptr;
  EXPECT_EQ(JobTable::EXPIRED, t.find("j3", &j)) << "an evicted id is remembered, never run again";
  EXPECT_EQ(JobTable::FOUND, t.find("j5", &j));
  openTerminal(t, "n2");   // takes j5, the remaining acknowledged one
  EXPECT_EQ(JobTable::EXPIRED, t.find("j5", &j));
  openTerminal(t, "n3");   // none acknowledged: the oldest terminal, j0
  EXPECT_EQ(JobTable::EXPIRED, t.find("j0", &j));
  EXPECT_EQ(JobTable::FOUND, t.find("j1", &j));
  EXPECT_EQ(JobTable::NONE, t.find("never", &j));
}

TEST(Jobs, AnOpenJobIsNeverEvicted) {
  JobTable t;
  for (int i = 0; i < JobTable::SLOTS - 1; i++) openTerminal(t, "j" + std::to_string(i));
  Job* open = t.open("open", "config.apply");
  ASSERT_NE(nullptr, open);
  ASSERT_TRUE(t.awaitConfirm(open, 300, true));
  EXPECT_EQ(nullptr, t.open("x", "config.apply")) << "busy, and nothing evicted";
  Job* j = nullptr;
  EXPECT_EQ(JobTable::FOUND, t.find("open", &j));
  for (int i = 0; i < JobTable::SLOTS - 1; i++) EXPECT_EQ(JobTable::FOUND, t.find(("j" + std::to_string(i)).c_str(), &j));
}

TEST(Jobs, OnlyATerminalResultCanBeAcknowledged) {
  JobTable t;
  Job* j = t.open("o", "config.apply");
  EXPECT_FALSE(t.ack("o"));
  ASSERT_TRUE(t.setState(j, JOB_CONFIRMED));
  EXPECT_EQ(1, t.owedCount());
  EXPECT_TRUE(t.ack("o"));
  EXPECT_EQ(0, t.owedCount());
  EXPECT_FALSE(t.ack("nobody"));
}

TEST(Jobs, TheConfirmDeadlineCountsRunningTime) {
  JobTable t;
  Job* j = t.open("d", "config.apply");
  ASSERT_TRUE(t.awaitConfirm(j, 10, true));
  int fired = 0;
  auto expired = [&](Job* x) { fired++; EXPECT_EQ(j, x); t.setState(x, JOB_REVERTED); };
  t.tick(4, expired);
  t.tick(5, expired);
  EXPECT_EQ(0, fired);
  EXPECT_EQ(1u, j->confirm_left_s);
  t.tick(1, expired);
  EXPECT_EQ(1, fired);
  EXPECT_EQ(JOB_REVERTED, j->state);
  t.tick(100, expired);
  EXPECT_EQ(1, fired) << "a terminal job is not expired again";
}

TEST(Jobs, ASecondBootRevertsAPendingSetting) {
  JobTable t;
  int reverted = 0;
  auto revert = [&](Job* x) { reverted++; t.setState(x, JOB_REVERTED); };
  Job* live = t.open("live", "config.apply");
  ASSERT_TRUE(t.awaitConfirm(live, 300, true));   // took effect in this boot: the next boot is the second
  t.booted(false, revert);
  EXPECT_EQ(1, reverted);
  Job* rb = t.open("rb", "config.apply");
  ASSERT_TRUE(t.awaitConfirm(rb, 300, false));    // takes effect after its reboot: that one boot is expected
  t.booted(false, revert);
  EXPECT_EQ(1, reverted) << "an operation that needed a reboot gets its one reboot";
  t.booted(false, revert);
  EXPECT_EQ(2, reverted) << "never two";
}

TEST(Jobs, AChangeThatNeedsARebootGetsItsWholeWindowInThatBoot) {
  JobTable t;
  int reverted = 0;
  auto revert = [&](Job* x) { reverted++; t.setState(x, JOB_REVERTED); };
  Job* j = t.open("r", "config.apply");
  ASSERT_TRUE(t.awaitConfirm(j, 100, false));
  t.tick(60, revert);   // a minute before the manager asks for the reboot
  EXPECT_EQ(40u, j->confirm_left_s);
  t.booted(false, revert);
  EXPECT_EQ(100u, j->confirm_left_s) << "the window is for the boot where the change took effect";
  EXPECT_EQ(0, reverted);
  t.tick(100, revert);
  EXPECT_EQ(1, reverted);
  Job* k = t.open("never", "config.apply");
  ASSERT_TRUE(t.awaitConfirm(k, 30, false));
  t.tick(30, revert);
  EXPECT_EQ(2, reverted) << "a change nobody activates still ends";
}

TEST(Jobs, AnUpdateRevertsOnlyOnAnAbnormalReset) {
  JobTable t;
  int reverted = 0;
  auto revert = [&](Job* x) { reverted++; t.setState(x, JOB_REVERTED); };
  Job* u = t.open("u", "update.offer");
  ASSERT_TRUE(t.awaitConfirm(u, 900, false, BOOT_ABNORMAL));
  for (int i = 0; i < 5; i++) t.booted(false, revert);
  EXPECT_EQ(0, reverted) << "a normal reboot or power cycle never vetoes an update";
  t.booted(true, revert);
  EXPECT_EQ(1, reverted);
}

TEST(Jobs, TheImageRoundTripsAndKeepsTheRing) {
  JobTable t;
  for (int i = 0; i < JobTable::SLOTS; i++) openTerminal(t, "j" + std::to_string(i));
  openTerminal(t, "k");                           // evicts j0 into the ring
  Job* a = t.open("await", "config.apply");
  ASSERT_NE(nullptr, a) << "slot freed by evicting another terminal";
  ASSERT_TRUE(t.awaitConfirm(a, 77, false));
  std::vector<uint8_t> img(JobTable::imageSize());
  ASSERT_EQ(img.size(), t.serialize(img.data(), img.size()));
  JobTable u;
  ASSERT_TRUE(u.deserialize(img.data(), img.size()));
  Job* j = nullptr;
  ASSERT_EQ(JobTable::FOUND, u.find("await", &j));
  EXPECT_EQ(JOB_AWAITING, j->state);
  EXPECT_EQ(77u, j->confirm_left_s);
  EXPECT_EQ(JobTable::EXPIRED, u.find("j0", &j)) << "the never-twice ring survives the reboot";
  EXPECT_EQ(nullptr, u.open("x", "config.apply")) << "and so does the open job";
}

TEST(Jobs, ADamagedImageIsRefusedAndForgetsRatherThanInvents) {
  JobTable t;
  openTerminal(t, "a");
  std::vector<uint8_t> img(JobTable::imageSize());
  ASSERT_EQ(img.size(), t.serialize(img.data(), img.size()));
  for (size_t at : {(size_t)0, (size_t)20, img.size() / 2, img.size() - 1}) {
    std::vector<uint8_t> bad = img;
    bad[at] ^= 0x40;
    JobTable u;
    openTerminal(u, "stale");
    EXPECT_FALSE(u.deserialize(bad.data(), bad.size())) << at;
    Job* j = nullptr;
    EXPECT_EQ(JobTable::NONE, u.find("a", &j));
    EXPECT_EQ(JobTable::NONE, u.find("stale", &j)) << "a refused image leaves the table empty";
  }
  JobTable v;
  EXPECT_FALSE(v.deserialize(img.data(), img.size() - 1)) << "another layout's size";
  EXPECT_EQ(0u, t.serialize(img.data(), img.size() - 1)) << "too small a buffer writes nothing";
}

TEST(Jobs, IdsAndOpsThatDoNotFitAreRefused) {
  JobTable t;
  EXPECT_EQ(nullptr, t.open(std::string(33, 'x').c_str(), "config.apply"));
  EXPECT_EQ(nullptr, t.open("ok", std::string(33, 'o').c_str()));
  EXPECT_EQ(nullptr, t.open("", "config.apply"));
  EXPECT_NE(nullptr, t.open(std::string(32, 'x').c_str(), std::string(32, 'o').c_str()));
}

TEST(Jobs, StateNamesAreTheContractsSpelling) {
  EXPECT_STREQ("awaiting-confirm", jobStateName(JOB_AWAITING));
  EXPECT_STREQ("accepted", jobStateName(JOB_ACCEPTED));
  EXPECT_STREQ("cancelled", jobStateName(JOB_CANCELLED));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
