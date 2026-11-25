// The LoRa dispatch split and the gateway's budget (docs/contract/LORA_UPDATES.md sections 6 and 9): which datagrams are bulk
// frames and which are requests, what a group datagram may carry, and what bulk may spend of the duty window.
#include <gtest/gtest.h>
#include <string.h>
#include "helpers/iface/LoraDispatch.h"

using namespace Iface;

TEST(LoraDispatch, ABulkMarkerPointToPointIsTheTransferAndARequestMarkerIsARequest) {
  uint8_t bulk[40] = {BULK_MARKER, 1, 2, 3};
  uint8_t req[40] = {LORA_MARKER, 1, 0, 1};
  uint8_t ans[40] = {ANSWER_MARKER};
  uint8_t other[40] = {0x01};
  EXPECT_EQ(ROUTE_BULK, loraRoute(bulk, sizeof(bulk), false));
  EXPECT_EQ(ROUTE_REQUEST, loraRoute(req, sizeof(req), false));
  EXPECT_EQ(ROUTE_OTHER, loraRoute(ans, sizeof(ans), false)) << "an answer is not a frame to the target";
  EXPECT_EQ(ROUTE_OTHER, loraRoute(other, sizeof(other), false)) << "any other marker keeps today's path";
  EXPECT_EQ(ROUTE_OTHER, loraRoute(bulk, 0, false));
  EXPECT_EQ(ROUTE_OTHER, loraRoute(nullptr, 4, false));
}

TEST(LoraDispatch, ABulkFrameOnTheManagementChannelIsDroppedNeverTheTransfers) {
  uint8_t bulk[40] = {BULK_MARKER, 1, 2, 3};
  uint8_t req[40] = {LORA_MARKER, 1, 0, 1};
  EXPECT_EQ(ROUTE_DROP_GROUP_BULK, loraRoute(bulk, sizeof(bulk), true)) << "point to point only";
  EXPECT_EQ(ROUTE_REQUEST, loraRoute(req, sizeof(req), true)) << "a request on the channel is as before";
}

TEST(LoraDispatch, TheCountOfGroupBulkFramesIsKeptAndReported) {
  LoraLink l;
  EXPECT_EQ(0u, l.counts().dropped_group_bulk);
  l.noteGroupBulk();
  l.noteGroupBulk();
  EXPECT_EQ(2u, l.counts().dropped_group_bulk);
}

TEST(LoraDispatch, PaddingIsOnlyTheZerosToTheBlockAndOnlyOnABulkFrame) {
  const uint32_t size = 115 * 3 + 40;
  BulkKeys k;
  const uint8_t token[16] = {9};
  bulkKeys(token, sizeof(token), &k);
  uint8_t data[BULK_DATA] = {1, 2, 3};
  uint8_t f[BULK_ROOM + 16];
  const size_t n = bulkFrameEncode(k, 7, size, 0, data, BULK_DATA, f, sizeof(f));
  ASSERT_EQ(BULK_ROOM, n);
  const size_t padded = (4 + n + 15) / 16 * 16 - 4;   // 140
  EXPECT_EQ(140u, padded);
  memset(f + n, 0, padded - n);
  EXPECT_EQ(n, bulkTrimPadding(size, f, padded));
  EXPECT_EQ(n, bulkTrimPadding(size, f, n)) << "exact stays exact";
  EXPECT_EQ(padded - 1, bulkTrimPadding(size, f, padded - 1)) << "a length that is not the padded one stays";
  f[padded - 1] = 1;
  EXPECT_EQ(padded, bulkTrimPadding(size, f, padded)) << "non-zero padding stays";
  f[padded - 1] = 0;
  f[0] = ANSWER_MARKER;
  EXPECT_EQ(padded, bulkTrimPadding(size, f, padded)) << "not a bulk frame";
  // the short last frame: 40 bytes of image: 9 + 40 + 8 = 57, heard as 64 - 4 = 60
  f[0] = BULK_MARKER;
  uint8_t last[BULK_ROOM + 16];
  const size_t ln = bulkFrameEncode(k, 7, size, 3, data, 40, last, sizeof(last));
  ASSERT_EQ(57u, ln);
  memset(last + ln, 0, 3);
  EXPECT_EQ(ln, bulkTrimPadding(size, last, 60));
}

TEST(LoraDispatch, ABlockAnswerIsTheTwentyTwoBytesWithOnlyZerosAfter) {
  uint8_t a[32] = {ANSWER_MARKER, 1, 2, 3};
  EXPECT_TRUE(answerHeard(a, 22));
  EXPECT_TRUE(answerHeard(a, 32)) << "the reply datagram pads to 32";
  a[25] = 1;
  EXPECT_FALSE(answerHeard(a, 32));
  a[25] = 0;
  EXPECT_FALSE(answerHeard(a, 21));
  EXPECT_FALSE(answerHeard(a, 38));
  a[0] = LORA_MARKER;
  EXPECT_FALSE(answerHeard(a, 22));
}

TEST(LoraDispatch, AnAnswerIsRelayedOnlyFromTheNodeBulkFramesWentTo) {
  uint8_t to[32], other[32], a[32] = {ANSWER_MARKER, 1, 2};
  memset(to, 5, 32);
  memset(other, 6, 32);
  EXPECT_TRUE(bulkAnswerRelayable(true, to, to, a, 32));
  EXPECT_FALSE(bulkAnswerRelayable(true, to, other, a, 32)) << "a node frames were not sent to";
  EXPECT_FALSE(bulkAnswerRelayable(false, to, to, a, 32)) << "nothing was sent at all";
  a[0] = LORA_MARKER;
  EXPECT_FALSE(bulkAnswerRelayable(true, to, to, a, 32)) << "not an answer";
}

TEST(LoraDispatch, TheBulkCeilingIsEightyPercentOfTheWindowLessEverythingSpentAndQueued) {
  EXPECT_EQ(28800u, bulkCeilingMs(36000)) << "1%";
  EXPECT_EQ(288000u, bulkCeilingMs(360000)) << "10%";
  EXPECT_EQ(28800u, bulkBudgetMs(36000, 0, 0));
  EXPECT_EQ(18800u, bulkBudgetMs(36000, 10000, 0));
  EXPECT_EQ(8800u, bulkBudgetMs(36000, 10000, 10000)) << "queued frames are spoken for";
  EXPECT_EQ(0u, bulkBudgetMs(36000, 28800, 0)) << "at the ceiling";
  EXPECT_EQ(0u, bulkBudgetMs(36000, 30000, 0)) << "the reserve (7,200 ms here) is never bulk's, even when control traffic used none of it";
  EXPECT_EQ(0u, bulkBudgetMs(36000, 0, 40000));
  EXPECT_EQ(0u, bulkBudgetMs(360000, 360000, 0)) << "a seeded hour: nothing until it ages";
}

TEST(LoraDispatch, FramesFitTheBudgetOrNothingIsQueued) {
  EXPECT_TRUE(bulkFits(1600, 8, 200));
  EXPECT_FALSE(bulkFits(1599, 8, 200));
  EXPECT_FALSE(bulkFits(0, 1, 1));
  EXPECT_TRUE(bulkFits(0, 0, 200));
  EXPECT_EQ(2 + 1 + 32 + 2 + 144u, bulkPacketBytes(BULK_ROOM)) << "a full frame is 181 bytes on the air";
}

TEST(LoraDispatch, AnRfBulkFrameIsAtMostOneDatagramAndMarkedBulk) {
  uint8_t f[BULK_ROOM + 1];
  memset(f, 0, sizeof(f));
  f[0] = BULK_MARKER;
  EXPECT_TRUE(bulkFrameShape(f, BULK_ROOM));
  EXPECT_FALSE(bulkFrameShape(f, BULK_ROOM + 1)) << "too long";
  EXPECT_FALSE(bulkFrameShape(f, BULK_HDR + BULK_TAG)) << "no byte of image";
  EXPECT_TRUE(bulkFrameShape(f, BULK_HDR + 1 + BULK_TAG));
  f[0] = LORA_MARKER;
  EXPECT_FALSE(bulkFrameShape(f, BULK_ROOM)) << "a request frame is not a bulk frame";
  f[0] = ANSWER_MARKER;
  EXPECT_FALSE(bulkFrameShape(f, BULK_ROOM));
}

TEST(LoraDispatch, ThePeerKeepsWhereTheFramesCameFromAndWipes) {
  BulkPeer p = {};
  EXPECT_FALSE(p.valid);
  uint8_t key[32], secret[32], path[3] = {7, 8, 9};
  memset(key, 1, 32);
  memset(secret, 2, 32);
  p.set(key, secret, path, 3);
  EXPECT_TRUE(p.valid);
  EXPECT_EQ(3, p.path_len);
  EXPECT_EQ(0, memcmp(p.path, path, 3));
  p.set(key, secret, nullptr, LoraLink::PATH_UNKNOWN);
  EXPECT_EQ((uint8_t)LoraLink::PATH_UNKNOWN, p.path_len);
  p.clear();
  EXPECT_FALSE(p.valid);
  EXPECT_EQ(0, p.key[0]);
  EXPECT_EQ(0, p.secret[0]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
