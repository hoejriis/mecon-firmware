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
