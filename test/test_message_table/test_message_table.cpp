// What became of a message.send (src/helpers/iface/MessageOps.h, docs/contract/OPERATIONS.md section 6): the outcome
// table's states, delayed and duplicate ACKs, the eviction order, and the bound on an entry the queue lost.
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <helpers/iface/MessageOps.h>

using namespace Iface;

namespace {

MessageEntry* add(MessageTable& t, const char* id, uint8_t tag, int channel = -1, uint32_t ack = 0, uint32_t now = 0) {
  MessageEntry* e = t.claim(id);
  if (!e) return nullptr;
  memset(e->hash, tag, sizeof(e->hash));
  e->channel = (int8_t)channel;
  e->ack = ack;
  e->est_ms = 1000;
  e->since_ms = now;
  memset(e->to, tag, sizeof(e->to));
  e->state = MSG_QUEUED;
  return e;
}

void hashOf(uint8_t tag, uint8_t out[8]) { memset(out, tag, 8); }

}  // namespace

TEST(MessageTable, ADirectMessageGoesQueuedTransmittedAcked) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8], to[6];
  hashOf(1, h);
  EXPECT_FALSE(messageFinal(*e));
  t.onTx(h, 500);
  EXPECT_EQ(e->state, MSG_TRANSMITTED);
  EXPECT_FALSE(messageFinal(*e)) << "a direct message still waits for its ACK";
  EXPECT_TRUE(t.onAck(77, to));
  EXPECT_EQ(e->state, MSG_ACKED);
  EXPECT_EQ(to[0], 1);
  EXPECT_TRUE(messageFinal(*e));
}

TEST(MessageTable, AChannelMessageIsFinalOnceTransmittedAndHasNoAck) {
  MessageTable t;
  MessageEntry* e = add(t, "c", 2, 1);
  uint8_t h[8], to[6];
  hashOf(2, h);
  t.onTx(h, 500);
  EXPECT_EQ(e->state, MSG_TRANSMITTED);
  EXPECT_TRUE(messageFinal(*e));
  t.tick(500 + 3600000);
  EXPECT_EQ(e->state, MSG_TRANSMITTED) << "a channel message never times out";
  EXPECT_FALSE(t.onAck(0, to)) << "code 0 is no ACK";
}

TEST(MessageTable, TheEstimateCountsFromTheTransmissionNotTheRequest) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77, 0);
  uint8_t h[8];
  hashOf(1, h);
  t.onHeld(h, 60000);
  t.tick(60000);
  EXPECT_EQ(e->state, MSG_DUTY_HELD);
  t.onTx(h, 60000);
  t.tick(60999);
  EXPECT_EQ(e->state, MSG_TRANSMITTED);
  t.tick(61000);
  EXPECT_EQ(e->state, MSG_TIMED_OUT);
}

TEST(MessageTable, ALateAckMovesATimeoutToAcked) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8], to[6];
  hashOf(1, h);
  t.onTx(h, 0);
  t.tick(1000);
  ASSERT_EQ(e->state, MSG_TIMED_OUT);
  e->tell = false;
  EXPECT_TRUE(t.onAck(77, to));
  EXPECT_EQ(e->state, MSG_ACKED);
  EXPECT_TRUE(e->tell);
}

TEST(MessageTable, ADuplicateAckChangesNothingButStillStopsTheAck) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8], to[6];
  hashOf(1, h);
  t.onTx(h, 0);
  ASSERT_TRUE(t.onAck(77, to));
  e->tell = false;
  EXPECT_TRUE(t.onAck(77, to)) << "the caller still marks a repeat as this node's, so it is not forwarded";
  EXPECT_FALSE(e->tell);
}

TEST(MessageTable, AnAckBeforeATransmissionIsNotAcked) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t to[6];
  EXPECT_FALSE(t.onAck(77, to));
  EXPECT_EQ(e->state, MSG_QUEUED);
}

TEST(MessageTable, ADroppedPacketFails) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8], other[8];
  hashOf(9, other);
  t.onTxFail(other);
  EXPECT_EQ(e->state, MSG_QUEUED) << "another packet's failure";
  hashOf(1, h);
  t.onTxFail(h);
  EXPECT_EQ(e->state, MSG_FAILED);
  EXPECT_TRUE(messageFinal(*e));
}

TEST(MessageTable, ATransmittedEntryIsNotMovedByAnotherReport) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8];
  hashOf(1, h);
  t.onTx(h, 0);
  t.onTxFail(h);
  t.onHeld(h, 10);
  EXPECT_EQ(e->state, MSG_TRANSMITTED);
}

TEST(MessageTable, AQueuedEntryTheReportOfWentMissingIsUnknownNotFailed) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77, 0);
  t.tick(MessageTable::QUEUE_LIMIT_MS - 1);
  EXPECT_EQ(e->state, MSG_QUEUED);
  t.tick(MessageTable::QUEUE_LIMIT_MS);
  EXPECT_EQ(e->state, MSG_UNKNOWN);
  EXPECT_TRUE(messageFinal(*e));
}

TEST(MessageTable, EvictionTakesCollectedThenOldestFinalAndNeverAnOpenEntry) {
  MessageTable t;
  char id[8];
  MessageEntry* e[MessageTable::SLOTS];
  for (int i = 0; i < MessageTable::SLOTS; i++) {
    snprintf(id, sizeof(id), "m%d", i);
    e[i] = add(t, id, (uint8_t)(i + 1), -1, (uint32_t)(i + 1));
  }
  EXPECT_EQ(t.claim("x"), nullptr) << "all open: nothing may be evicted";
  e[2]->state = MSG_ACKED;
  e[5]->state = MSG_ACKED;
  e[5]->collected = true;
  MessageEntry* c = t.claim("x");
  EXPECT_EQ(c, e[5]) << "a collected entry goes first, though another final entry is older";
  c->state = MSG_QUEUED;
  c = t.claim("y");
  EXPECT_EQ(c, e[2]);
  c->state = MSG_QUEUED;
  EXPECT_EQ(t.claim("z"), nullptr);
  EXPECT_EQ(t.open(), MessageTable::SLOTS);
}

TEST(MessageTable, AnIdThatDoesNotFitIsNotClaimed) {
  MessageTable t;
  char id[MessageEntry::ID_CAP + 2];
  memset(id, 'a', sizeof(id) - 1);
  id[sizeof(id) - 1] = 0;
  EXPECT_EQ(t.claim(id), nullptr);
}

TEST(MessageTable, OpenAndOwedCount) {
  MessageTable t;
  MessageEntry* a = add(t, "a", 1, -1, 7);
  add(t, "b", 2, -1, 8);
  EXPECT_EQ(t.open(), 2);
  EXPECT_EQ(t.owed(), 0);
  a->state = MSG_ACKED;
  EXPECT_EQ(t.open(), 1);
  EXPECT_EQ(t.owed(), 1);
  a->collected = true;
  EXPECT_EQ(t.owed(), 0);
}

TEST(MessageTable, EachChangeIsToldOnceInItsLatestState) {
  MessageTable t;
  MessageEntry* e = add(t, "a", 1, -1, 77);
  uint8_t h[8];
  hashOf(1, h);
  e->tell = true;
  t.onTx(h, 0);
  MessageEntry* n = t.nextToTell();
  ASSERT_EQ(n, e);
  EXPECT_EQ(n->state, MSG_TRANSMITTED);
  EXPECT_EQ(t.nextToTell(), nullptr);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
