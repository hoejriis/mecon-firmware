#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/LoraLink.h"

using namespace Iface;

static const uint8_t KEY_A[32] = {1}, KEY_B[32] = {2}, KEY_C[32] = {3}, SECRET[32] = {9};
static const uint8_t NO_PATH[1] = {0};

static std::vector<uint8_t> frame(uint8_t xid, uint8_t idx, uint8_t cnt, size_t len, uint8_t fill = 'x') {
  std::vector<uint8_t> f = {LORA_MARKER, xid, idx, cnt};
  f.insert(f.end(), len, fill);
  return f;
}

static LoraLink::Feed feed(LoraLink& l, const uint8_t* key, const std::vector<uint8_t>& f, uint32_t now = 1000, uint8_t path_len = 0) {
  return l.feed(key, SECRET, NO_PATH, path_len, f.data(), f.size(), LORA_REQ_PAYLOAD, now);
}

struct Sent { std::vector<std::vector<uint8_t>> frames; std::vector<bool> flood; bool refuse = false; };
static bool sendTo(void* u, const LoraLink::Peer&, const uint8_t* f, size_t n, bool flood_ok) {
  Sent* s = static_cast<Sent*>(u);
  if (s->refuse) return false;
  s->frames.emplace_back(f, f + n);
  s->flood.push_back(flood_ok);
  return true;
}

TEST(LoraLink, AMessageInOrderOrNotCompletesOnce) {
  LoraLink l;
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(7, 1, 2, 5, 'b')));
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(7, 1, 2, 5, 'b'))) << "heard twice costs nothing";
  EXPECT_EQ(nullptr, l.nextComplete());
  EXPECT_EQ(LoraLink::F_COMPLETE, feed(l, KEY_A, frame(7, 0, 2, LORA_REQ_PAYLOAD, 'a')));
  LoraLink::Peer* p = l.nextComplete();
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(LORA_REQ_PAYLOAD + 5, LoraLink::messageLen(*p));
  EXPECT_EQ('a', LoraLink::message(*p)[0]);
  EXPECT_EQ('b', LoraLink::message(*p)[LORA_REQ_PAYLOAD]);
  EXPECT_EQ(0, LoraLink::message(*p)[LORA_REQ_PAYLOAD + 5]);
  l.release(*p);
  EXPECT_EQ(nullptr, l.nextComplete());
}

TEST(LoraLink, TheBlockPaddingOfTheDatagramIsNotMessage) {
  LoraLink l;
  std::vector<uint8_t> first = frame(7, 0, 2, LORA_REQ_PAYLOAD, 'a');
  first.insert(first.end(), 8, 0);   // a full frame, padded up to a block
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, first));
  std::vector<uint8_t> last = frame(7, 1, 2, 5, 'b');
  last.insert(last.end(), 11, 0);
  EXPECT_EQ(LoraLink::F_COMPLETE, feed(l, KEY_A, last));
  LoraLink::Peer* p = l.nextComplete();
  ASSERT_NE(nullptr, p);
  EXPECT_EQ(LORA_REQ_PAYLOAD + 5, LoraLink::messageLen(*p));
  std::vector<uint8_t> empty = frame(8, 0, 1, 0);
  empty.insert(empty.end(), 16, 0);
  LoraLink m;
  EXPECT_EQ(LoraLink::F_IGNORED, feed(m, KEY_B, empty)) << "only padding";
}

TEST(LoraLink, AReplyUpToTheBoundCompletesThoughItsFullFramesWouldPassIt) {
  // 7 reply frames are 1,050 bytes when all are full, past LORA_MSG_MAX; a message of 968 in 7 is within it (#457)
  const size_t last = 968 - 6 * LORA_REPLY_PAYLOAD;
  auto rfeed = [](LoraLink& l, const std::vector<uint8_t>& f) {
    return l.feed(KEY_A, SECRET, NO_PATH, 0, f.data(), f.size(), LORA_REPLY_PAYLOAD, 1000);
  };
  LoraLink l;
  for (uint8_t i = 0; i < 6; i++) EXPECT_EQ(LoraLink::F_PARTIAL, rfeed(l, frame(4, i, 7, LORA_REPLY_PAYLOAD))) << (int)i;
  EXPECT_EQ(LoraLink::F_COMPLETE, rfeed(l, frame(4, 6, 7, last)));
  EXPECT_EQ(968u, LoraLink::messageLen(*l.nextComplete()));
  LoraLink m;   // the last frame first: the others follow it in
  EXPECT_EQ(LoraLink::F_PARTIAL, rfeed(m, frame(5, 6, 7, last)));
  for (uint8_t i = 0; i < 5; i++) EXPECT_EQ(LoraLink::F_PARTIAL, rfeed(m, frame(5, i, 7, LORA_REPLY_PAYLOAD))) << (int)i;
  EXPECT_EQ(LoraLink::F_COMPLETE, rfeed(m, frame(5, 5, 7, LORA_REPLY_PAYLOAD)));
  LoraLink o;   // one byte past the bound, known at the last frame
  EXPECT_EQ(LoraLink::F_IGNORED, rfeed(o, frame(6, 6, 7, LORA_MSG_MAX - 6 * LORA_REPLY_PAYLOAD + 1)));
  EXPECT_EQ(LoraLink::F_PARTIAL, rfeed(o, frame(6, 6, 7, LORA_MSG_MAX - 6 * LORA_REPLY_PAYLOAD)));
  LoraLink q;   // 8 frames cannot be within it, whatever the last holds
  EXPECT_EQ(LoraLink::F_IGNORED, rfeed(q, frame(7, 0, 8, LORA_REPLY_PAYLOAD)));
}

TEST(LoraLink, BadFramesAreDroppedBeforeAnythingIsReserved) {
  LoraLink l;
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, frame(1, 0, 0, 5))) << "no frames";
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, frame(1, 0, 9, LORA_REQ_PAYLOAD))) << "over 8 frames";
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, frame(1, 2, 2, 5))) << "index at the count";
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, frame(1, 0, 2, 5))) << "a short frame that is not the last";
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, frame(1, 0, 1, LORA_REQ_PAYLOAD + 1))) << "over one frame's size";
  std::vector<uint8_t> over = frame(1, 7, 8, LORA_REPLY_PAYLOAD);
  EXPECT_EQ(LoraLink::F_IGNORED, l.feed(KEY_A, SECRET, NO_PATH, 0, over.data(), over.size(), LORA_REPLY_PAYLOAD, 1000)) << "over the message bound";
  std::vector<uint8_t> other = frame(1, 0, 1, 5);
  other[0] = 0x01;
  EXPECT_EQ(LoraLink::F_IGNORED, feed(l, KEY_A, other)) << "a native typed request is not a frame";
  EXPECT_EQ(6u, l.counts().dropped_bad);
  EXPECT_EQ(0u, l.counts().frames);
}

TEST(LoraLink, TwoMessagesAreNeverCombined) {
  LoraLink l;
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(1, 0, 2, LORA_REQ_PAYLOAD, 'a')));
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(2, 1, 2, 5, 'b'))) << "another xid discards the first";
  EXPECT_EQ(nullptr, l.nextComplete());
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(2, 1, 3, LORA_REQ_PAYLOAD, 'c'))) << "another count too";
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(2, 0, 3, LORA_REQ_PAYLOAD, 'd')));
  EXPECT_EQ(LoraLink::F_COMPLETE, feed(l, KEY_A, frame(2, 2, 3, 1, 'e')));
  LoraLink::Peer* p = l.nextComplete();
  ASSERT_NE(nullptr, p);
  EXPECT_EQ('d', LoraLink::message(*p)[0]);
  EXPECT_EQ('c', LoraLink::message(*p)[LORA_REQ_PAYLOAD]);
}

TEST(LoraLink, AThirdPeerEvictsTheOldest) {
  LoraLink l;
  feed(l, KEY_A, frame(1, 0, 2, LORA_REQ_PAYLOAD), 100);
  feed(l, KEY_B, frame(1, 0, 2, LORA_REQ_PAYLOAD), 200);
  feed(l, KEY_C, frame(1, 0, 2, LORA_REQ_PAYLOAD), 300);
  EXPECT_EQ(1u, l.counts().evicted);
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(1, 1, 2, 5), 400)) << "A lost its first frame: it does not complete";
  EXPECT_EQ(LoraLink::F_COMPLETE, feed(l, KEY_C, frame(1, 1, 2, 5), 500));
}

TEST(LoraLink, AReplyIsCutIntoFramesOnePerGap) {
  LoraLink l;
  feed(l, KEY_A, frame(5, 0, 1, 10));
  LoraLink::Peer* p = l.nextComplete();
  std::string msg(LORA_REPLY_PAYLOAD + 10, 'r');
  ASSERT_TRUE(l.queueReply(*p, msg.data(), msg.size(), true, 1000));
  EXPECT_EQ(nullptr, l.nextComplete()) << "not again while its answer is going out";
  Sent s;
  l.tick(1000, sendTo, &s);
  ASSERT_EQ(1u, s.frames.size());
  EXPECT_EQ(0, memcmp(s.frames[0].data(), LORA_REPLY_MAGIC, 4));
  EXPECT_EQ(5, s.frames[0][4]);
  EXPECT_EQ(0, s.frames[0][5]);
  EXPECT_EQ(2, s.frames[0][6]);
  l.tick(1000 + LORA_FRAME_GAP_MS - 1, sendTo, &s);
  EXPECT_EQ(1u, s.frames.size()) << "the gap holds";
  l.tick(1000 + LORA_FRAME_GAP_MS, sendTo, &s);
  ASSERT_EQ(2u, s.frames.size());
  EXPECT_EQ(LORA_REPLY_HDR + 10, s.frames[1].size());
  EXPECT_TRUE(s.flood[1]);
}

TEST(LoraLink, UnverifiedAnswersNeedAKnownPathAndShareABudget) {
  LoraLink l;
  feed(l, KEY_A, frame(1, 0, 1, 5), 1000, LoraLink::PATH_UNKNOWN);
  LoraLink::Peer* p = l.nextComplete();
  EXPECT_FALSE(l.queueReply(*p, "x", 1, false, 1000)) << "no way back: never a flood";
  l.release(*p);
  int ok = 0;
  for (int i = 0; i < 10; i++) {
    feed(l, i % 2 ? KEY_A : KEY_B, frame((uint8_t)(i + 2), 0, 1, 5), 2000);
    p = l.nextComplete();
    ASSERT_NE(nullptr, p);
    if (l.queueReply(*p, "x", 1, false, 2000)) ok++;
    Sent s;
    l.tick(2000 + (uint32_t)i, sendTo, &s);
    l.release(*p);
  }
  EXPECT_EQ((int)LORA_UNVERIFIED_PER_MIN, ok) << "shared across peers, whatever the key";
  feed(l, KEY_A, frame(99, 0, 1, 5), 2000 + 15000);
  p = l.nextComplete();
  EXPECT_TRUE(l.queueReply(*p, "x", 1, false, 2000 + 15000)) << "one more after a quarter of a minute";
}

TEST(LoraLink, EveryStateEndsByItsDeadline) {
  LoraLink l;
  Sent s;
  feed(l, KEY_A, frame(3, 0, 2, LORA_REQ_PAYLOAD), 1000, 0);
  l.tick(1000 + LORA_REASSEMBLY_MS - 1, sendTo, &s);
  EXPECT_TRUE(s.frames.empty());
  l.tick(1000 + LORA_REASSEMBLY_MS, sendTo, &s);
  ASSERT_EQ(1u, s.frames.size()) << "parts-missing, with the xid and what arrived";
  std::string body(s.frames[0].begin() + LORA_REPLY_HDR, s.frames[0].end());
  EXPECT_NE(std::string::npos, body.find("\"error\":\"parts-missing\",\"xid\":3,\"have\":1"));
  EXPECT_EQ(3, s.frames[0][4]);
  EXPECT_EQ(LoraLink::F_PARTIAL, feed(l, KEY_A, frame(3, 1, 2, 5), 1000 + LORA_REASSEMBLY_MS + 1, 0)) << "starts again";

  LoraLink held;
  feed(held, KEY_A, frame(4, 0, 1, 5), 1000);
  LoraLink::Peer* p = held.nextComplete();
  std::string msg(10, 'r');
  ASSERT_TRUE(held.queueReply(*p, msg.data(), msg.size(), true, 1000));
  Sent no;
  no.refuse = true;
  held.tick(1000, sendTo, &no);
  EXPECT_EQ(0u, held.counts().dropped_deadline) << "held, not lost";
  held.tick(1000 + LORA_REPLY_MS, sendTo, &no);
  EXPECT_EQ(1u, held.counts().dropped_deadline) << "dropped at its deadline, and counted";
}

TEST(LoraLink, OnTheChannelOnlyAVerifiedResultIsAnswered) {
  LoraLink l;
  const uint8_t prefix[LORA_CH_PREFIX] = {1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<uint8_t> f = frame(5, 0, 1, 10);
  ASSERT_EQ(LoraLink::F_COMPLETE, l.feedChannel(prefix, f.data(), f.size(), 1000));
  LoraLink::Peer* p = l.nextComplete();
  ASSERT_NE(nullptr, p);
  EXPECT_TRUE(p->channel);
  EXPECT_FALSE(l.queueReply(*p, "x", 1, false, 1000)) << "never an unverified answer, whatever the budget";
  std::string msg(LORA_CH_REPLY_PAYLOAD + 1, 'r');
  ASSERT_TRUE(l.queueReply(*p, msg.data(), msg.size(), true, 1000));
  l.release(*p);
  Sent s;
  l.tick(1000, sendTo, &s);
  ASSERT_EQ(1u, s.frames.size());
  EXPECT_EQ(LORA_REPLY_HDR + LORA_CH_REPLY_PAYLOAD, s.frames[0].size());
  EXPECT_EQ(2, s.frames[0][6]);

  LoraLink gap;
  std::vector<uint8_t> part = frame(6, 0, 2, LORA_REQ_PAYLOAD);
  gap.feedChannel(prefix, part.data(), part.size(), 1000);
  Sent none;
  gap.tick(1000 + LORA_REASSEMBLY_MS, sendTo, &none);
  EXPECT_TRUE(none.frames.empty()) << "no parts-missing on a channel";
  EXPECT_FALSE(loraChannelOpAllowed("status.get")) << "a read reveals state to every member";
  EXPECT_TRUE(loraChannelOpAllowed("config.apply"));
}

TEST(LoraLink, AReplyPathIsTheFloodReversed) {
  const uint8_t path[] = {1, 2, 3, 4};
  uint8_t out[8];
  EXPECT_EQ(0, loraReplyPath(true, 1, 0, path, out, sizeof(out))) << "a neighbour";
  EXPECT_EQ(0, loraReplyPath(false, 1, 0, path, out, sizeof(out)));
  EXPECT_EQ((uint8_t)LoraLink::PATH_UNKNOWN, loraReplyPath(false, 1, 2, path, out, sizeof(out))) << "direct: its path is used up";
  EXPECT_EQ(2, loraReplyPath(true, 1, 2, path, out, sizeof(out)));
  EXPECT_EQ(2, out[0]);
  EXPECT_EQ(1, out[1]);
  EXPECT_EQ((1 << 6) | 2, loraReplyPath(true, 2, 2, path, out, sizeof(out)));
  EXPECT_EQ(3, out[0]);
  EXPECT_EQ(4, out[1]);
  EXPECT_EQ(1, out[2]);
  EXPECT_EQ((uint8_t)LoraLink::PATH_UNKNOWN, loraReplyPath(true, 2, 2, path, out, 3)) << "does not fit";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
