// The LoRa update fixtures (docs/contract/LORA_UPDATES.md section 11) that tools/lora_ota_manager.py writes to
// test/fixtures/lora_updates_v1.json, read through the firmware's own codecs: the bulk frames and block answers, the
// frames a target drops and the answers a manager drops, the manifest parts joined as the target joins them, and the two
// signatures (the member's on the offer, the release key's on the manifest) under the firmware's Ed25519 verify. The
// Python side (tools/tests/test_lora_ota_manager_model.py) reads the same file, so each implementation checks the other.
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/deployment/DepHmac.h"
#include "helpers/deployment/DepVerify.h"
#include "helpers/iface/LoraBulk.h"
#include "helpers/updates/UpdB64.h"
#include "helpers/updates/UpdManifestParts.h"

using namespace Iface;

static std::string fixture() {
  FILE* f = fopen("test/fixtures/lora_updates_v1.json", "rb");
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

// The string value of "key" at or after `from`, with JSON's \" and \\ undone.
static std::string str(const std::string& j, const char* key, size_t from = 0) {
  const std::string k = std::string("\"") + key + "\": \"";
  size_t a = j.find(k, from);
  if (a == std::string::npos) return "";
  a += k.size();
  std::string out;
  for (; a < j.size() && j[a] != '"'; a++) {
    if (j[a] == '\\' && a + 1 < j.size()) a++;
    out += j[a];
  }
  return out;
}
static long num(const std::string& j, const char* key, size_t from = 0) {
  const std::string k = std::string("\"") + key + "\": ";
  size_t a = j.find(k, from);
  return a == std::string::npos ? -1 : atol(j.c_str() + a + k.size());
}
static std::vector<uint8_t> b64(const std::string& s) {
  uint8_t buf[128];
  const long n = Upd::base64Decode(s.data(), s.size(), buf, sizeof(buf));
  return n < 0 ? std::vector<uint8_t>() : std::vector<uint8_t>(buf, buf + n);
}
static size_t at(const std::string& j, const char* name, size_t from = 0) { return j.find(std::string("\"") + name + "\": {", from); }

class Fixtures : public ::testing::Test {
protected:
  void SetUp() override {
    j = fixture();
    ASSERT_FALSE(j.empty()) << "test/fixtures/lora_updates_v1.json";
    bulk = at(j, "bulk");
    auto token = hex(str(j, "token_hex", bulk));
    bulkKeys(token.data(), token.size(), &k);
    job = (uint32_t)num(j, "job", bulk);
    const size_t im = at(j, "image");
    size = (uint32_t)num(j, "size", im);
    total = (uint32_t)num(j, "frames_total", im);
    image.resize(size);
    for (uint32_t i = 0; i < size; i++) image[i] = (uint8_t)(i * 7 + 3);
  }
  std::vector<uint8_t> frameHex(size_t i) {
    size_t pos = j.find('[', j.find("\"frames_hex\"", bulk));
    for (size_t n = 0; n <= i; n++) {
      const size_t a = j.find('"', pos), b = j.find('"', a + 1);
      if (n == i) return hex(j.substr(a + 1, b - a - 1));
      pos = b + 1;
    }
    return {};
  }
  std::string j;
  size_t bulk = 0;
  BulkKeys k;
  uint32_t job = 0, size = 0, total = 0;
  std::vector<uint8_t> image;
};

TEST_F(Fixtures, TheFramesAreByteForByteTheManagersAndAnswerAsTheTargetDoes) {
  EXPECT_EQ(hex(str(j, "key_bulk_hex", bulk)), std::vector<uint8_t>(k.bulk, k.bulk + 32));
  EXPECT_EQ(hex(str(j, "key_answer_hex", bulk)), std::vector<uint8_t>(k.answer, k.answer + 32));
  ASSERT_EQ(total, bulkFramesTotal(size));
  for (uint32_t idx = 0; idx < total; idx++) {
    uint8_t out[BULK_ROOM];
    const size_t n = bulkFrameEncode(k, job, size, idx, image.data() + idx * BULK_DATA, bulkFrameDataLen(size, idx), out, sizeof(out));
    EXPECT_EQ(frameHex(idx), std::vector<uint8_t>(out, out + n)) << "frame " << idx;
    const auto f = frameHex(idx);
    uint32_t got = 99;
    const uint8_t* d = nullptr;
    size_t len = 0;
    EXPECT_EQ(BULK_OK, bulkFrameDecode(k, job, size, f.data(), f.size(), &got, &d, &len)) << idx;
    EXPECT_EQ(idx, got);
  }
  EXPECT_EQ(22u, frameHex(total - 1).size()) << "the last frame is as long as an answer";
}

TEST_F(Fixtures, EveryFrameACaptureOrALengthMakesIsDropped) {
  const size_t cases = at(j, "frame_cases");
  ASSERT_NE(std::string::npos, cases);
  int seen = 0;
  for (const char* name : {"captured_answer_replayed_as_frame", "captured_answer_marker_changed_as_frame", "frame_short_before_last",
                           "frame_long_before_last", "frame_long_last", "frame_short_last"}) {
    const size_t p = at(j, name, cases);
    ASSERT_NE(std::string::npos, p) << name;
    const auto f = hex(str(j, "hex", p));
    uint32_t idx = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
    const BulkCheck c = bulkFrameDecode(k, job, size, f.data(), f.size(), &idx, &data, &len);
    EXPECT_STREQ(str(j, "verdict", p).c_str(), bulkCheckName(c)) << name;
    EXPECT_NE(BULK_OK, c) << name;
    seen++;
  }
  EXPECT_EQ(6, seen);
}

TEST_F(Fixtures, ABulkFrameIsNeverAnAnswerAndAStaleAnswerNamesAnotherBase) {
  uint32_t base, next;
  uint8_t have;
  const size_t cases = at(j, "answer_cases");
  for (const char* name : {"bulk_frame_as_answer", "bulk_frame_marker_changed_as_answer"}) {
    const auto f = hex(str(j, "hex", at(j, name, cases)));
    EXPECT_FALSE(answerDecode(k, job, f.data(), f.size(), &base, &next, &have)) << name;
  }
  const auto good = hex(str(j, "hex", at(j, "answer", cases)));
  ASSERT_TRUE(answerDecode(k, job, good.data(), good.size(), &base, &next, &have));
  const size_t cur = at(j, "answer_current");
  EXPECT_EQ((uint32_t)num(j, "base", cur), base);
  EXPECT_EQ((uint32_t)num(j, "next", cur), next);
  EXPECT_EQ((uint8_t)num(j, "have", cur), have);
  uint8_t again[ANSWER_LEN];
  answerEncode(k, job, base, next, have, again);
  EXPECT_EQ(good, std::vector<uint8_t>(again, again + ANSWER_LEN)) << "the firmware writes the answer the manager reads";
  // the stale one is a good answer, for the block before: only its base tells it from the open block's
  const size_t st = at(j, "stale_answer");
  const auto stale = hex(str(j, "answer_hex", st));
  ASSERT_TRUE(answerDecode(k, job, stale.data(), stale.size(), &base, &next, &have));
  EXPECT_NE((uint32_t)num(j, "open_block_base", st), base);
}

TEST_F(Fixtures, AResumeKeepsTheFirstJobsNumberSoTheResumingRequestsFramesAreAnotherTransfers) {
  const size_t pr = at(j, "pause_resume");
  EXPECT_EQ(job, (uint32_t)num(j, "frames_keep_job_number", pr));
  const auto f = hex(str(j, "frame_for_the_resuming_request_hex", pr));
  uint32_t idx;
  const uint8_t* d;
  size_t len;
  EXPECT_EQ(BULK_OTHER_JOB, bulkFrameDecode(k, job, size, f.data(), f.size(), &idx, &d, &len));
}

TEST_F(Fixtures, TheManifestPartsJoinToTheSignedTextAndBothSignaturesVerify) {
  const size_t mp = at(j, "manifest");
  const std::string text = str(j, "text", mp);
  const auto sig = b64(str(j, "sig_b64", mp));
  ASSERT_EQ(64u, sig.size());
  uint8_t sha[32];
  Dep::sha256(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sha);
  EXPECT_EQ(hex(str(j, "sha256", mp)), std::vector<uint8_t>(sha, sha + 32));

  Upd::ManifestParts parts;
  size_t pos = j.find("\"parts\": [", mp);
  Upd::ManifestParts::Verdict v = Upd::ManifestParts::MP_BAD;
  for (;;) {
    pos = j.find("\"part\": ", pos);
    if (pos == std::string::npos) break;
    const unsigned part = (unsigned)num(j, "part", pos), count = (unsigned)num(j, "parts", pos);
    const std::string data = str(j, "data", pos);
    pos += 8;
    v = parts.add(sha, part, count, data.c_str(), data.size());
    EXPECT_TRUE(v == Upd::ManifestParts::MP_MORE || v == Upd::ManifestParts::MP_COMPLETE);
  }
  ASSERT_EQ(Upd::ManifestParts::MP_COMPLETE, v);
  uint8_t joined[Upd::ManifestParts::TOTAL_MAX];
  const size_t n = parts.join(joined, sizeof(joined));
  ASSERT_EQ(text.size() + 64, n);
  EXPECT_EQ(0, memcmp(joined, text.data(), text.size()));
  EXPECT_EQ(0, memcmp(joined + text.size(), sig.data(), 64));

  // the release signature, over the domain-separated text
  const std::string rel = std::string("MECON-RELEASE-v1") + '\0' + text;
  const auto rpub = hex(str(j, "release_public_key"));
  EXPECT_TRUE(Dep::verifySig(sig.data(), reinterpret_cast<const uint8_t*>(rel.data()), rel.size(), rpub.data()));
}

TEST_F(Fixtures, TheSignedOffersVerifyUnderTheMembersKey) {
  const auto pub = hex(str(j, "issuer_public_key"));
  const std::string prefix = std::string("MECON-DEVMSG-v1") + '\0' + "iface" + '\0';
  const size_t off = at(j, "offer");
  const size_t pr = at(j, "pause_resume");
  for (size_t p : {at(j, "wire", off), at(j, "first_offer_wire", pr), at(j, "resume_wire", pr)}) {
    ASSERT_NE(std::string::npos, p);
    const std::string body = str(j, "body", p);
    const auto sig = b64(str(j, "sig", p));
    ASSERT_EQ(64u, sig.size());
    const std::string msg = prefix + body;
    EXPECT_TRUE(Dep::verifySig(sig.data(), reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), pub.data()));
    std::string bad = msg;
    bad[bad.size() / 2] ^= 1;
    EXPECT_FALSE(Dep::verifySig(sig.data(), reinterpret_cast<const uint8_t*>(bad.data()), bad.size(), pub.data()));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
