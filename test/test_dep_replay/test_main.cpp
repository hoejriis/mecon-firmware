// The signed-request replay window: new, duplicate, too old, out-of-order arrival inside the window, a reboot
// (the persisted state) and the absence of any sequence-reset adoption.
#include <gtest/gtest.h>
#include "helpers/deployment/DepReplay.h"

using namespace Dep;

static Watermark accept(Watermark w, uint32_t id) {
  EXPECT_EQ(RV_NEW, replayCheck(w, id)) << id;
  markAccepted(&w, id);
  return w;
}

TEST(DepReplay, FreshStateTakesAnyRealId) {
  Watermark w = { 0, 0 };
  EXPECT_EQ(RV_NEW, replayCheck(w, 1));
  EXPECT_EQ(RV_NEW, replayCheck(w, 100000));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 0));
}

TEST(DepReplay, ExactRepeatIsADuplicate) {
  Watermark w = accept({ 0, 0 }, 5);
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 5));
}

TEST(DepReplay, OutOfOrderInsideTheWindowRunsOnce) {
  Watermark w = { 0, 0 };
  w = accept(w, 10);
  w = accept(w, 12);       // 11 still in flight
  w = accept(w, 11);       // arrives late: new
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 11));
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 10));
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 12));
}

TEST(DepReplay, WindowEdge) {
  Watermark w = accept({ 0, 0 }, 100);
  EXPECT_EQ(RV_NEW, replayCheck(w, 100 - (REPLAY_WINDOW - 1)));   // the oldest id the window still tracks
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 100 - REPLAY_WINDOW));
}

TEST(DepReplay, ABigJumpForgetsTheOldWindow) {
  Watermark w = accept(accept({ 0, 0 }, 1), 2);
  w = accept(w, 2 + REPLAY_WINDOW + 5);
  EXPECT_EQ(1u, w.mask);
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 2));
}

// The persisted pair is the whole state: a guard rebuilt from it after a reboot refuses every id it accepted.
TEST(DepReplay, RebootKeepsEveryAcceptedIdRefused) {
  Watermark w = { 0, 0 };
  for (uint32_t id : { 3u, 5u, 6u, 9u, 20u, 21u }) w = accept(w, id);
  Watermark persisted = w;   // what the record stores
  for (uint32_t id : { 3u, 5u, 6u, 9u, 20u, 21u }) EXPECT_NE(RV_NEW, replayCheck(persisted, id)) << id;
  for (uint32_t id : { 4u, 7u, 8u, 10u, 19u }) EXPECT_EQ(RV_NEW, replayCheck(persisted, id)) << id;
}

// A stored mask that lost bit 0 must still refuse the mark itself.
TEST(DepReplay, MarkIsAlwaysAccepted) {
  Watermark w = { 50, 0 };
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 50));
}

// Old signed requests replayed in increasing order, all below the window: each is refused, and no run of them
// ever moves the window (no adoption).
TEST(DepReplay, NoAdoptionFromARunOfOldIds) {
  Watermark w = accept({ 0, 0 }, 1000);
  Watermark before = w;
  for (uint32_t id = 900; id < 960; id++) {
    EXPECT_EQ(RV_TOO_OLD, replayCheck(w, id)) << id;
  }
  EXPECT_EQ(before.mark, w.mark);
  EXPECT_EQ(before.mask, w.mask);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
