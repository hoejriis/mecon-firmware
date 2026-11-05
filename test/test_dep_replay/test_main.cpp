// The strict per-member sequence (docs/contract/AUTHORITY.md section 8).
#include <gtest/gtest.h>
#include "helpers/deployment/DepReplay.h"

using namespace Dep;

static Watermark accept(Watermark w, uint32_t id) {
  EXPECT_EQ(RV_NEW, replayCheck(w, id)) << id;
  markAccepted(&w, id);
  return w;
}

TEST(DepReplay, FreshStateTakesAnyRealId) {
  Watermark w = { 0 };
  EXPECT_EQ(RV_NEW, replayCheck(w, 1));
  EXPECT_EQ(RV_NEW, replayCheck(w, 100000));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 0));
}

// Issue #293: one id in MARK_RESERVE writes to flash, and what is on flash is always ahead of what ran.
TEST(DepReplay, OnlyAnIdAboveTheReservationNeedsAWrite) {
  Watermark w = { 0, 0 };
  EXPECT_TRUE(markAccepted(&w, 1));
  EXPECT_EQ(1u + MARK_RESERVE, w.reserved);
  for (uint32_t id = 2; id <= 1 + MARK_RESERVE; id++) EXPECT_FALSE(markAccepted(&w, id)) << id;
  EXPECT_EQ(1u + MARK_RESERVE, w.mark);
  EXPECT_TRUE(markAccepted(&w, 2 + MARK_RESERVE));
  EXPECT_EQ(2u + 2 * MARK_RESERVE, w.reserved);
  EXPECT_GE(w.reserved, w.mark);
}

TEST(DepReplay, TheReservationNeverReachesTheBlockedValue) {
  Watermark w = { 0, 0 };
  EXPECT_TRUE(markAccepted(&w, MARK_BLOCKED - 2));
  EXPECT_EQ(MARK_BLOCKED - 1, w.reserved);
  EXPECT_FALSE(markAccepted(&w, MARK_BLOCKED - 1));
}

TEST(DepReplay, TheMarkIsADuplicate) {
  Watermark w = accept({ 0 }, 5);
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 5));
}

// The held-back request: once a newer id has run, every older one is dead, however recent.
TEST(DepReplay, AnOlderIdNeverRunsAfterANewerOne) {
  Watermark w = accept({ 0 }, 100);
  w = accept(w, 101);
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 100));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 99));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 1));
}

TEST(DepReplay, GapsAreAllowedForward) {
  Watermark w = accept({ 0 }, 3);
  w = accept(w, 50);                       // ids 4..49 were never sent, or were abandoned by the manager
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 20));
  EXPECT_EQ(RV_NEW, replayCheck(w, 51));
}

TEST(DepReplay, AStoredMarkIsTheWholeState) {
  Watermark persisted = accept(accept({ 0 }, 7), 9);   // what a reboot reads back
  EXPECT_EQ(RV_DUPLICATE, replayCheck(persisted, 9));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(persisted, 8));
  EXPECT_EQ(RV_NEW, replayCheck(persisted, 10));
}

TEST(DepReplay, ABlockedMemberRunsNothing) {
  Watermark w = { MARK_BLOCKED };
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 1));
  EXPECT_EQ(RV_TOO_OLD, replayCheck(w, 0xFFFFFFFEu));
  EXPECT_EQ(RV_DUPLICATE, replayCheck(w, 0xFFFFFFFFu));   // the mark itself: answered, never run
}

TEST(DepReplay, MarkAcceptedNeverMovesBack) {
  Watermark w = accept({ 0 }, 40);
  markAccepted(&w, 12);
  EXPECT_EQ(40u, w.mark);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
