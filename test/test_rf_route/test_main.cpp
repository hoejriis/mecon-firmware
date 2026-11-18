#include <gtest/gtest.h>
#include "helpers/iface/RfRoute.h"

using Iface::RfRoute;

static const uint8_t A[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t B[6] = {9, 9, 9, 9, 9, 9};

// One request: ask, send as told, as rfSend does. Returns whether it flooded.
static bool request(RfRoute& r, const uint8_t* key, uint32_t now) {
  const bool f = r.flood(key, now);
  r.sent(key, f, now);
  return f;
}

TEST(RfRoute, AnUnknownTargetIsTriedZeroHopFirst) {
  RfRoute r;
  EXPECT_FALSE(request(r, A, 1000));
}

TEST(RfRoute, AZeroHopAnswerKeepsTheTargetZeroHop) {
  RfRoute r;
  request(r, A, 1000);
  r.heard(A, 3000);
  EXPECT_EQ(r.mode(A), RfRoute::ZERO_HOP);
  EXPECT_FALSE(request(r, A, 100000));   // long after: still zero-hop, nothing rebroadcast
}

TEST(RfRoute, AnUnansweredZeroHopAttemptSendsTheNextRequestByFlood) {
  RfRoute r;
  request(r, A, 1000);
  EXPECT_FALSE(request(r, A, 1000 + RfRoute::WAIT_MS - 1));   // a retry inside the wait: still zero-hop
  EXPECT_TRUE(request(r, A, 1000 + RfRoute::WAIT_MS));        // past it with nothing heard: flood
}

TEST(RfRoute, AFloodAnswerKeepsTheTargetFloodUntilItsTimeRunsOut) {
  RfRoute r;
  request(r, A, 0);
  EXPECT_TRUE(request(r, A, RfRoute::WAIT_MS));
  r.heard(A, RfRoute::WAIT_MS + 5000);
  EXPECT_EQ(r.mode(A), RfRoute::FLOOD);
  const uint32_t t = RfRoute::WAIT_MS + 5000;
  EXPECT_TRUE(request(r, A, t + 60000));                     // a multi-hop target is not tried zero-hop each time
  r.heard(A, t + 62000);
  EXPECT_FALSE(request(r, A, t + 62000 + RfRoute::FLOOD_TTL_MS));   // until the time runs out, then zero-hop again
}

TEST(RfRoute, AZeroHopTargetThatStopsAnsweringFallsBackToFlood) {
  RfRoute r;
  request(r, A, 0);
  r.heard(A, 1000);
  request(r, A, 50000);                                      // it has moved out of range: no answer
  EXPECT_TRUE(request(r, A, 50000 + RfRoute::WAIT_MS));
}

TEST(RfRoute, TargetsAreKeptApart) {
  RfRoute r;
  request(r, A, 0);
  r.heard(A, 1000);
  EXPECT_FALSE(request(r, B, 2000));
  EXPECT_TRUE(request(r, B, 2000 + RfRoute::WAIT_MS));       // B unanswered floods; A stays zero-hop
  EXPECT_FALSE(request(r, A, 2000 + RfRoute::WAIT_MS));
}

TEST(RfRoute, TheClockWrapping) {
  RfRoute r;
  request(r, A, 0xFFFFF000u);
  EXPECT_TRUE(request(r, A, 0xFFFFF000u + RfRoute::WAIT_MS));
}

// #578: the request's frames waited for airtime, so the attempt's wait runs from its last frame on the air.
TEST(RfRoute, AnAttemptHeldForAirtimeIsNotCountedUnansweredFromWhenItWasQueued) {
  RfRoute r;
  request(r, A, 1000);
  r.onAir(15000);   // the last frame left 14 s after it was queued
  // Past the wait from queueing (21 s), but not from the air: still zero-hop.
  EXPECT_FALSE(request(r, A, 15000 + RfRoute::WAIT_MS - 1));
  EXPECT_TRUE(request(r, A, 15000 + RfRoute::WAIT_MS));    // past the wait from the air with nothing heard: flood
}

TEST(RfRoute, OnAirLeavesAFloodAttemptAndAnAnsweredTargetAlone) {
  RfRoute r;
  request(r, A, 0);
  EXPECT_TRUE(request(r, A, RfRoute::WAIT_MS));   // A floods
  r.onAir(RfRoute::WAIT_MS + 1000);
  EXPECT_EQ(r.mode(A), RfRoute::FLOOD);
  request(r, B, 0);
  r.heard(B, 2000);
  r.onAir(50000);                                 // nothing pending for B: no effect
  EXPECT_EQ(r.mode(B), RfRoute::ZERO_HOP);
  EXPECT_FALSE(request(r, B, 50000 + RfRoute::WAIT_MS));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
