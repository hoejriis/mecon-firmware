// LoRa bulk frames and block answers (docs/contract/LORA_UPDATES.md section 6) against the vectors that
// tools/mecon_lora_bulk.py writes to test/fixtures/lora_bulk_v1.json: the firmware and the manager's reference agree
// on every byte, and neither kind of frame can stand in for the other.
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/iface/LoraBulk.h"

using namespace Iface;

static std::string fixture() {
  FILE* f = fopen("test/fixtures/lora_bulk_v1.json", "rb");
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

static std::vector<uint8_t> hex(const std::string& s) {
  std::vector<uint8_t> out;
  auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back((uint8_t)(nib(s[i]) << 4 | nib(s[i + 1])));
  return out;
}

// The first string value of "key" at or after `from`.
static std::string str(const std::string& j, const char* key, size_t from = 0) {
  const std::string k = std::string("\"") + key + "\": \"";
  size_t a = j.find(k, from);
  if (a == std::string::npos) return "";
  a += k.size();
  return j.substr(a, j.find('"', a) - a);
}
static long num(const std::string& j, const char* key) {
  const std::string k = std::string("\"") + key + "\": ";
  size_t a = j.find(k);
  return a == std::string::npos ? -1 : atol(j.c_str() + a + k.size());
}
static size_t at(const std::string& j, const char* name) { return j.find(std::string("\"") + name + "\": {"); }

class LoraBulkVectors : public ::testing::Test {
protected:
  void SetUp() override {
    j = fixture();
    ASSERT_FALSE(j.empty()) << "test/fixtures/lora_bulk_v1.json";
    auto token = hex(str(j, "token_hex"));
    bulkKeys(token.data(), token.size(), &k);
    job = (uint32_t)num(j, "job");
    size = (uint32_t)num(j, "transfer_size");
  }
  std::string j;
  BulkKeys k;
  uint32_t job = 0, size = 0;
};

TEST_F(LoraBulkVectors, TheKeysAreTheReferences) {
  EXPECT_EQ(hex(str(j, "key_bulk_hex")), std::vector<uint8_t>(k.bulk, k.bulk + 32));
  EXPECT_EQ(hex(str(j, "key_answer_hex")), std::vector<uint8_t>(k.answer, k.answer + 32));
}

TEST_F(LoraBulkVectors, FramesAreByteForByteTheReferences) {
  ASSERT_EQ(3u, bulkFramesTotal(size));
  EXPECT_EQ(BULK_DATA, bulkFrameDataLen(size, 0));
  EXPECT_EQ(5u, bulkFrameDataLen(size, 2));   // 235 = 115 + 115 + 5
  EXPECT_EQ(0u, bulkFrameDataLen(size, 3));
  std::vector<uint8_t> image(size);
  for (uint32_t i = 0; i < size; i++) image[i] = (uint8_t)(i * 7 + 3);
  size_t pos = j.find('[', j.find("\"frames_hex\""));
  for (uint32_t idx = 0; idx < 3; idx++) {
    const size_t a = j.find('"', pos), b = j.find('"', a + 1);   // the next string in the array
    const auto want = hex(j.substr(a + 1, b - a - 1));
    pos = b + 1;
    uint8_t out[BULK_ROOM];
    const size_t n = bulkFrameEncode(k, job, size, idx, image.data() + idx * BULK_DATA, bulkFrameDataLen(size, idx), out,
                                     sizeof(out));
    EXPECT_EQ(want, std::vector<uint8_t>(out, out + n)) << "frame " << idx;
  }
}

TEST_F(LoraBulkVectors, EveryFrameCaseGetsItsVerdict) {
  for (const char* name : {"frame_0", "frame_last", "answer_as_frame", "answer_marker_changed_as_frame",
                           "frame_short_before_last", "frame_long_last", "frame_other_job", "frame_past_end"}) {
    const size_t p = at(j, name);
    ASSERT_NE(std::string::npos, p) << name;
    const auto f = hex(str(j, "hex", p));
    uint32_t idx = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
    const BulkCheck c = bulkFrameDecode(k, job, size, f.data(), f.size(), &idx, &data, &len);
    EXPECT_STREQ(str(j, "verdict", p).c_str(), bulkCheckName(c)) << name;
  }
}

TEST_F(LoraBulkVectors, TheAnswerIsTheReferenceAndOnlyAnAnswerIsAccepted) {
  uint8_t a[ANSWER_LEN];
  answerEncode(k, job, 2, 2, 3, a);
  EXPECT_EQ(hex(str(j, "hex", at(j, "answer"))), std::vector<uint8_t>(a, a + ANSWER_LEN));
  for (const char* name : {"frame_last_as_answer", "frame_last_marker_changed_as_answer"}) {
    const auto f = hex(str(j, "hex", at(j, name)));
    uint32_t base, next;
    uint8_t have;
    EXPECT_FALSE(answerDecode(k, job, f.data(), f.size(), &base, &next, &have)) << name;
  }
  uint32_t base = 0, next = 0;
  uint8_t have = 0;
  ASSERT_TRUE(answerDecode(k, job, a, sizeof(a), &base, &next, &have));
  EXPECT_EQ(2u, base);
  EXPECT_EQ(2u, next);
  EXPECT_EQ(3, have);
}

TEST(LoraBulk, EncodeRefusesAWrongLengthAndAnotherKeyFailsTheTag) {
  const uint8_t token[16] = {9};
  BulkKeys k, other;
  bulkKeys(token, sizeof(token), &k);
  const uint8_t token2[16] = {8};
  bulkKeys(token2, sizeof(token2), &other);
  uint8_t data[BULK_DATA] = {1}, out[BULK_ROOM];
  EXPECT_EQ(0u, bulkFrameEncode(k, 7, 300, 0, data, BULK_DATA - 1, out, sizeof(out)));   // short before the last
  EXPECT_EQ(0u, bulkFrameEncode(k, 7, 300, 3, data, 1, out, sizeof(out)));               // past the end
  const size_t n = bulkFrameEncode(k, 7, 300, 0, data, BULK_DATA, out, sizeof(out));
  ASSERT_EQ(BULK_ROOM, n);
  uint32_t idx;
  const uint8_t* d;
  size_t len;
  EXPECT_EQ(BULK_OK, bulkFrameDecode(k, 7, 300, out, n, &idx, &d, &len));
  EXPECT_EQ(BULK_BAD_TAG, bulkFrameDecode(other, 7, 300, out, n, &idx, &d, &len));
  out[20] ^= 1;   // a byte changed on the way
  EXPECT_EQ(BULK_BAD_TAG, bulkFrameDecode(k, 7, 300, out, n, &idx, &d, &len));
  bulkKeysWipe(&k);
  for (uint8_t b : k.bulk) EXPECT_EQ(0, b);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
