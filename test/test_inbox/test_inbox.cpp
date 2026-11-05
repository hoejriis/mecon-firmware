// The manager's copy of received messages (src/helpers/iface/InboxOps.h, docs/contract/OPERATIONS.md section 6.4): the
// eviction order, what counts as lost, and that an acknowledged text is wiped.
#include <gtest/gtest.h>
#include <string.h>
#include <helpers/iface/InboxOps.h>

using namespace Iface;

namespace {

void add(Inbox& b, int channel, const char* text = "x") {
  InboxEntry* e = b.add();
  ASSERT_NE(e, nullptr);
  e->channel = (int8_t)channel;
  strcpy(e->text, text);
  e->len = (uint8_t)strlen(text);
}

}  // namespace

TEST(Inbox, WithNoSlotsAReceptionIsCountedAndLost) {
  Inbox b;
  EXPECT_EQ(b.add(), nullptr);
  EXPECT_EQ(b.received(), 1u);
  EXPECT_EQ(b.lost(), 1u);
  EXPECT_EQ(b.unread(), 0);
  ASSERT_TRUE(b.alloc());
  add(b, -1);
  EXPECT_EQ(b.received(), 2u);
  EXPECT_EQ(b.lost(), 1u);
  EXPECT_EQ(b.nextAfter(0)->seq, 2u) << "the seq counts the reception that had no slot, so the gap shows";
}

TEST(Inbox, FullPushesOutTheOldestChannelMessageFirst) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  add(b, -1);    // 1
  add(b, 0);     // 2
  add(b, -1);    // 3
  add(b, 1);     // 4
  for (int i = 0; i < 4; i++) add(b, -1);   // 5..8
  EXPECT_EQ(b.lost(), 0u);
  add(b, -1);    // 9 pushes out 2
  EXPECT_EQ(b.lost(), 1u);
  EXPECT_EQ(b.nextAfter(1)->seq, 3u);
  add(b, -1);    // 10 pushes out 4
  EXPECT_EQ(b.nextAfter(3)->seq, 5u);
  EXPECT_EQ(b.nextAfter(0)->seq, 1u) << "a direct message outlives a channel one, however old";
  add(b, -1);    // 11: only direct ones left, so the oldest goes
  EXPECT_EQ(b.nextAfter(0)->seq, 3u);
  EXPECT_EQ(b.lost(), 3u);
  EXPECT_EQ(b.unread(), 8);
}

TEST(Inbox, ReadInOrderWhateverSlotEachLandedIn) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  for (int i = 0; i < 8; i++) add(b, -1);
  b.ackUpTo(3);
  for (int i = 0; i < 2; i++) add(b, -1);
  uint32_t at = 3;
  uint32_t want = 4;
  while (const InboxEntry* e = b.nextAfter(at)) {
    EXPECT_EQ(e->seq, want++);
    at = e->seq;
  }
  EXPECT_EQ(want, 11u);
}

TEST(Inbox, AnAckWipesTheTextAndFreesTheSlotWithoutCountingItLost) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  add(b, -1, "secret one");
  const InboxEntry* e = b.nextAfter(0);
  b.ackUpTo(1);
  EXPECT_EQ(b.acked(), 1u);
  EXPECT_EQ(b.unread(), 0);
  EXPECT_EQ(b.nextAfter(0), nullptr);
  EXPECT_EQ(strstr(e->text, "secret"), nullptr) << "no copy stays once the manager has it";
  for (int i = 0; i < 8; i++) add(b, -1);
  EXPECT_EQ(b.lost(), 0u);
}

TEST(Inbox, AnOlderAckDoesNotMoveTheMarkBack) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  add(b, -1);
  add(b, -1);
  b.ackUpTo(2);
  b.ackUpTo(1);
  EXPECT_EQ(b.acked(), 2u);
}

TEST(Inbox, PushingOutAnEntryWipesItsText) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  add(b, 0, "a long old channel text");
  for (int i = 0; i < 7; i++) add(b, -1);
  InboxEntry* e = b.add();
  ASSERT_NE(e, nullptr);
  EXPECT_EQ(e->text[0], 0);
  EXPECT_EQ(e->len, 0);
  EXPECT_EQ(e->seq, 9u);
}

TEST(Inbox, AnEventNamesCountsOnly) {
  Inbox b;
  ASSERT_TRUE(b.alloc());
  add(b, -1, "not in the event");
  char out[64];
  ASSERT_TRUE(writeInboxEventData(out, sizeof(out), b));
  EXPECT_STREQ(out, "{\"received\":1,\"unread\":1,\"lost\":0}");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
