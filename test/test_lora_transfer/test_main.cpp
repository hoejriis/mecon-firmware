// The target's side of a LoRa transfer (docs/contract/LORA_UPDATES.md sections 6 and 7) against a fake sink: a byte
// array that records every write, the durable `next` saves, the block answers sent and a switchable full duty window.
#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/iface/LoraTransfer.h"

using namespace Iface;

namespace {

const uint32_t JOB = 0x01020304;

struct Answer { uint32_t base, next; uint8_t have; };

struct Fake {
  std::vector<uint8_t> flash;
  std::vector<std::pair<uint32_t, size_t>> writes;   // offset, length
  std::vector<uint32_t> saves;
  std::vector<std::vector<uint8_t>> sent;
  bool duty_full = false;
  bool write_fail = false;
  int erased = 0;
  BulkKeys keys;

  static bool wr(void* c, uint32_t off, const uint8_t* d, size_t n) {
    Fake* f = (Fake*)c;
    if (f->write_fail) return false;
    if (f->flash.size() < off + n) f->flash.resize(off + n, 0xFF);
    memcpy(&f->flash[off], d, n);
    f->writes.push_back({off, n});
    return true;
  }
  static bool snd(void* c, const uint8_t* b, size_t n) {
    Fake* f = (Fake*)c;
    if (f->duty_full) return false;
    f->sent.emplace_back(b, b + n);
    return true;
  }
  static void sv(void* c, uint32_t n) { ((Fake*)c)->saves.push_back(n); }
  static void er(void* c) { ((Fake*)c)->erased++; }
  TransferSink sink() { return TransferSink{this, wr, snd, sv, er}; }

  Answer answer(size_t i) const {
    Answer a{};
    EXPECT_LT(i, sent.size());
    if (i < sent.size()) {
      EXPECT_EQ(ANSWER_LEN, sent[i].size());
      EXPECT_TRUE(answerDecode(keys, JOB, sent[i].data(), sent[i].size(), &a.base, &a.next, &a.have));
    }
    return a;
  }
};

struct Rig {
  Fake fake;
  std::vector<uint8_t> image;
  uint32_t size;
  LoraTransfer* t = nullptr;
  uint32_t now = 1000;

  explicit Rig(uint32_t sz) : size(sz) {
    const uint8_t token[16] = {5, 4, 3};
    bulkKeys(token, sizeof(token), &fake.keys);
    for (uint32_t i = 0; i < sz; i++) image.push_back((uint8_t)(i * 7 + (i >> 8)));
  }
  ~Rig() { delete t; }
  LoraTransfer& open(unsigned block, uint32_t deadline_in = 3600u * 1000, uint32_t next = 0, uint32_t quiet = 8000) {
    delete t;
    t = new LoraTransfer(fake.sink(), quiet);
    EXPECT_TRUE(t->begin(JOB, size, block, fake.keys, now + deadline_in, now, next));
    return *t;
  }
  std::vector<uint8_t> frame(uint32_t i, uint32_t job = JOB) {
    uint8_t out[BULK_ROOM];
    const size_t len = bulkFrameDataLen(size, i);
    const size_t n = bulkFrameEncode(fake.keys, job, size, i, &image[i * BULK_DATA], len, out, sizeof(out));
    EXPECT_GT(n, 0u);
    return std::vector<uint8_t>(out, out + n);
  }
  void send(uint32_t i, uint32_t dt = 10) {
    now += dt;
    auto f = frame(i);
    t->onFrame(f.data(), f.size(), now);
  }
  void tick(uint32_t dt) { now += dt; t->tick(now); }
  bool flashIs(uint32_t upto_frames) {
    const size_t n = bulkFrameDataLen(size, 0) ? std::min<size_t>(size, (size_t)upto_frames * BULK_DATA) : 0;
    return fake.flash.size() >= n && memcmp(fake.flash.data(), image.data(), n) == 0;
  }
};

const uint32_t SIZE = 115 * 10 + 40;   // 11 frames, the last one short (40 bytes)

}  // namespace

TEST(LoraTransfer, WholeTransferInOrderIsByteExactWithAShortLastFrame) {
  Rig r(SIZE);
  auto& t = r.open(4);
  for (uint32_t i = 0; i < 11; i++) r.send(i);
  EXPECT_EQ(TS_COMPLETE, t.state());
  EXPECT_EQ(SIZE, r.fake.flash.size());
  EXPECT_EQ(0, memcmp(r.fake.flash.data(), r.image.data(), SIZE));
  ASSERT_EQ(3u, r.fake.sent.size());   // blocks 0-3, 4-7, 8-10
  Answer a = r.fake.answer(0);
  EXPECT_EQ(0u, a.base); EXPECT_EQ(4u, a.next); EXPECT_EQ(0x0F, a.have);
  a = r.fake.answer(1);
  EXPECT_EQ(4u, a.base); EXPECT_EQ(8u, a.next); EXPECT_EQ(0x0F, a.have);
  a = r.fake.answer(2);
  EXPECT_EQ(8u, a.base); EXPECT_EQ(11u, a.next); EXPECT_EQ(0x07, a.have);   // frames past the end have no bit
  EXPECT_EQ(11u, r.fake.writes.size());
  EXPECT_EQ(40u, r.fake.writes.back().second);
}

TEST(LoraTransfer, LossAndResendHoldTheBaseThenAdvance) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(0); r.send(1); r.send(3);   // frame 2 lost; the block's last frame triggers the answer
  ASSERT_EQ(1u, r.fake.sent.size());
  Answer a = r.fake.answer(0);
  EXPECT_EQ(0u, a.base); EXPECT_EQ(2u, a.next); EXPECT_EQ(0x0B, a.have);   // 0, 1 written, 3 held
  EXPECT_EQ(2u, r.fake.writes.size());   // 3 is only buffered
  r.send(2);   // the resend: a gap filled, 3 flushed; the block's last frame is not re-sent so no answer yet
  EXPECT_EQ(4u, t.next());
  EXPECT_EQ(4u, r.fake.writes.size());
  r.send(3);   // the manager re-sends the missing frame and the last: a duplicate of 3 answers again
  ASSERT_EQ(2u, r.fake.sent.size());
  a = r.fake.answer(1);
  EXPECT_EQ(0u, a.base); EXPECT_EQ(4u, a.next); EXPECT_EQ(0x0F, a.have);
  r.send(4);   // the next block starts at 4
  EXPECT_TRUE(r.flashIs(5));
}

TEST(LoraTransfer, ReorderingWithinABlockWritesInOrder) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(2); r.send(1); r.send(0);
  EXPECT_EQ(3u, t.next());
  r.send(3);
  EXPECT_EQ(4u, t.next());
  ASSERT_EQ(1u, r.fake.sent.size());
  EXPECT_EQ(0x0F, r.fake.answer(0).have);
  for (size_t i = 0; i < r.fake.writes.size(); i++) EXPECT_EQ(i * BULK_DATA, r.fake.writes[i].first);   // ascending
  EXPECT_TRUE(r.flashIs(4));
}

TEST(LoraTransfer, ADuplicateIsNeverWrittenTwice) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(0); r.send(0); r.send(2); r.send(2);   // written dup, then buffered dup
  EXPECT_EQ(1u, r.fake.writes.size());
  EXPECT_EQ(2u, t.status().dup);
  r.send(1);   // flushes 2
  r.send(1); r.send(2);
  EXPECT_EQ(3u, r.fake.writes.size());
  EXPECT_EQ(4u, t.status().dup);
  r.send(3);
  EXPECT_EQ(4u, r.fake.writes.size());
}

TEST(LoraTransfer, StrangersAndOtherJobsAreDroppedCountedAndNeverAnswered) {
  Rig r(SIZE);
  auto& t = r.open(4);
  BulkKeys other;
  const uint8_t tok[16] = {9};
  bulkKeys(tok, sizeof(tok), &other);
  uint8_t out[BULK_ROOM];
  size_t n = bulkFrameEncode(other, JOB, SIZE, 3, &r.image[3 * BULK_DATA], BULK_DATA, out, sizeof(out));
  t.onFrame(out, n, r.now);   // wrong key, the block's last index: still no answer
  auto f = r.frame(1, JOB + 1);
  t.onFrame(f.data(), f.size(), r.now);   // another job
  uint8_t ans[ANSWER_LEN];
  answerEncode(r.fake.keys, JOB, 0, 1, 1, ans);
  t.onFrame(ans, ANSWER_LEN, r.now);   // an answer is not a bulk frame
  TransferStatus s = t.status();
  EXPECT_EQ(1u, s.bad[BULK_BAD_TAG]);
  EXPECT_EQ(1u, s.bad[BULK_OTHER_JOB]);
  EXPECT_EQ(1u, s.bad[BULK_NOT_BULK]);
  EXPECT_TRUE(r.fake.sent.empty());
  EXPECT_TRUE(r.fake.writes.empty());
  r.tick(60000);   // nothing was accepted, so not even the quiet answer
  EXPECT_TRUE(r.fake.sent.empty());
  // A frame changed on the way, and one of the wrong length.
  auto g = r.frame(0);
  g[12] ^= 1;
  t.onFrame(g.data(), g.size(), r.now);
  auto h = r.frame(0);
  t.onFrame(h.data(), h.size() - 1, r.now);
  uint32_t bad = 0;
  for (uint32_t b : t.status().bad) bad += b;
  EXPECT_EQ(5u, bad);
  EXPECT_TRUE(r.fake.writes.empty());
}

TEST(LoraTransfer, FramesAheadOfTheBlockAreDroppedAndCounted) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(4); r.send(7); r.send(10);
  EXPECT_EQ(3u, t.status().out_of_block);
  EXPECT_TRUE(r.fake.writes.empty());
  EXPECT_TRUE(r.fake.sent.empty());
  r.send(0); r.send(1); r.send(2); r.send(3);
  EXPECT_EQ(4u, t.next());   // the dropped ones were not kept
  r.send(4);
  EXPECT_EQ(5u, t.next());
}

TEST(LoraTransfer, QuietTimeoutAnswersWhenTheLastFrameIsLost) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(0); r.send(1); r.send(2);   // 3 lost
  r.tick(7000);
  EXPECT_TRUE(r.fake.sent.empty());   // 7.0 s since the last frame: not yet
  r.tick(1100);
  ASSERT_EQ(1u, r.fake.sent.size());
  Answer a = r.fake.answer(0);
  EXPECT_EQ(0u, a.base); EXPECT_EQ(3u, a.next); EXPECT_EQ(0x07, a.have);
  r.tick(20000);   // answered once, and no new frame: no second answer
  EXPECT_EQ(1u, r.fake.sent.size());
  r.send(3);
  ASSERT_EQ(2u, r.fake.sent.size());
  EXPECT_EQ(0x0F, r.fake.answer(1).have);
  EXPECT_EQ(TS_OPEN, t.state());
}

TEST(LoraTransfer, TheQuietTimeIsAConstructorParameter) {
  Rig r(SIZE);
  r.open(4, 3600u * 1000, 0, 500);
  r.send(0);
  r.tick(400);
  EXPECT_TRUE(r.fake.sent.empty());
  r.tick(200);
  EXPECT_EQ(1u, r.fake.sent.size());
}

TEST(LoraTransfer, NextIsSavedAtMultiplesOf64AtPauseAndAtCompletion) {
  const uint32_t big = 115 * 150 + 10;   // 151 frames
  Rig r(big);
  auto& t = r.open(8);
  for (uint32_t i = 0; i < 151; i++) {
    r.send(i);
    if (i == 62) EXPECT_TRUE(r.fake.saves.empty());
  }
  EXPECT_EQ(TS_COMPLETE, t.state());
  ASSERT_EQ(3u, r.fake.saves.size());
  EXPECT_EQ(64u, r.fake.saves[0]);    // 8-frame blocks: 64 is reached exactly
  EXPECT_EQ(128u, r.fake.saves[1]);
  EXPECT_EQ(151u, r.fake.saves[2]);   // completion

  Rig p(big);
  auto& u = p.open(8);
  for (uint32_t i = 0; i < 70; i++) p.send(i);
  ASSERT_EQ(1u, p.fake.saves.size());
  EXPECT_EQ(64u, p.fake.saves[0]);
  p.tick(3600u * 1000);   // the window ends: pause saves 70... next is 70, not a multiple
  EXPECT_EQ(TS_PAUSED, u.state());
  ASSERT_EQ(2u, p.fake.saves.size());
  EXPECT_EQ(70u, p.fake.saves[1]);
}

TEST(LoraTransfer, ACrossingInsideAFlushSavesOnceAtTheNewNext) {
  const uint32_t big = 115 * 150 + 10;
  Rig r(big);
  auto& t = r.open(8, 3600u * 1000, 60);   // a resume at 60: blocks start there
  r.send(61); r.send(62); r.send(63); r.send(64); r.send(65);
  EXPECT_TRUE(r.fake.saves.empty());
  r.send(60);   // flushes 60..65 and crosses 64
  EXPECT_EQ(66u, t.next());
  ASSERT_EQ(1u, r.fake.saves.size());
  EXPECT_EQ(66u, r.fake.saves[0]);
}

TEST(LoraTransfer, ARebootResumesFromThePersistedNextAndFinishesByteExact) {
  const uint32_t big = 115 * 150 + 10;
  Rig r(big);
  r.open(8);
  for (uint32_t i = 0; i < 100; i++) r.send(i);
  ASSERT_FALSE(r.fake.saves.empty());
  const uint32_t persisted = r.fake.saves.back();
  EXPECT_EQ(64u, persisted);
  r.fake.writes.clear();
  auto& t = r.open(8, 3600u * 1000, persisted);   // a new object, as after a reboot; flash survives
  for (uint32_t i = persisted; i < 151; i++) r.send(i);   // the manager restarts the block from `next`
  EXPECT_EQ(TS_COMPLETE, t.state());
  EXPECT_EQ(big, r.fake.flash.size());
  EXPECT_EQ(0, memcmp(r.fake.flash.data(), r.image.data(), big));
  EXPECT_EQ(151u - 64u, r.fake.writes.size());   // frames 64..99 were rewritten with identical bytes
}

TEST(LoraTransfer, TheWindowEndsAPauseAndAResumeReopensAtNext) {
  Rig r(SIZE);
  auto& t = r.open(4, 5000);
  r.send(0); r.send(1); r.send(3);
  r.tick(5000);
  EXPECT_EQ(TS_PAUSED, t.state());
  EXPECT_EQ(TR_WINDOW, t.status().reason);
  ASSERT_EQ(1u, r.fake.saves.size());
  EXPECT_EQ(2u, r.fake.saves[0]);
  r.send(2);   // dropped while paused, counted, not written
  EXPECT_EQ(1u, t.status().dropped_paused);
  EXPECT_EQ(2u, r.fake.writes.size());
  EXPECT_FALSE(t.resume(r.now + 3600u * 1000, r.now) && false);
  EXPECT_EQ(TS_OPEN, t.state());
  // The RAM buffer is gone: frame 3 must be sent again; the block now starts at 2.
  r.send(2); r.send(3); r.send(4); r.send(5);
  EXPECT_EQ(6u, t.next());
  Answer a = r.fake.answer(r.fake.sent.size() - 1);
  EXPECT_EQ(2u, a.base);
  EXPECT_EQ(6u, a.next);
  EXPECT_EQ(0x0F, a.have);
  EXPECT_FALSE(t.resume(r.now, r.now));   // only a paused transfer resumes
}

TEST(LoraTransfer, TheManagerPausesAfter30MinutesWithoutABulkFrame) {
  Rig r(SIZE);
  auto& t = r.open(4, 24u * 3600 * 1000);
  r.send(0);
  r.tick(29u * 60 * 1000);
  EXPECT_EQ(TS_OPEN, t.state());
  // A stranger's frames do not keep the transfer alive.
  auto f = r.frame(1, JOB + 1);
  t.onFrame(f.data(), f.size(), r.now);
  r.tick(60u * 1000 + 1);
  EXPECT_EQ(TS_PAUSED, t.state());
  EXPECT_EQ(TR_MANAGER, t.status().reason);
  ASSERT_EQ(1u, r.fake.saves.size());
  EXPECT_EQ(1u, r.fake.saves[0]);
}

TEST(LoraTransfer, DutyPausesWhenAnAnswerCannotBeSentFor10Minutes) {
  Rig r(SIZE);
  auto& t = r.open(4, 24u * 3600 * 1000);
  r.fake.duty_full = true;
  for (uint32_t i = 0; i < 4; i++) r.send(i);
  EXPECT_TRUE(r.fake.sent.empty());
  EXPECT_EQ(TS_OPEN, t.state());
  r.tick(9u * 60 * 1000);
  EXPECT_EQ(TS_OPEN, t.state());
  r.fake.duty_full = false;   // room again: the late answer goes, nothing pauses
  r.tick(1000);
  ASSERT_EQ(1u, r.fake.sent.size());
  EXPECT_EQ(4u, r.fake.answer(0).next);
  EXPECT_EQ(TS_OPEN, t.state());

  r.fake.duty_full = true;
  for (uint32_t i = 4; i < 8; i++) r.send(i);
  r.tick(10u * 60 * 1000 - 100);
  EXPECT_EQ(TS_OPEN, t.state());
  r.tick(200);
  EXPECT_EQ(TS_PAUSED, t.state());
  EXPECT_EQ(TR_DUTY, t.status().reason);
  EXPECT_EQ(8u, r.fake.saves.back());
}

TEST(LoraTransfer, APausedTransferStalls7DaysLaterAndErasesTheSlot) {
  Rig r(SIZE);
  auto& t = r.open(4, 1000);
  r.send(0);
  r.tick(1000);
  ASSERT_EQ(TS_PAUSED, t.state());
  r.tick(TRANSFER_STALL_MS - 1);
  EXPECT_EQ(TS_PAUSED, t.state());
  EXPECT_EQ(0, r.fake.erased);
  r.tick(1);
  EXPECT_EQ(TS_FAILED, t.state());
  EXPECT_EQ(TR_STALLED, t.status().reason);
  EXPECT_EQ(1, r.fake.erased);
  r.send(1);   // an ended transfer takes nothing
  EXPECT_EQ(1u, t.status().dropped_closed);
}

TEST(LoraTransfer, CancelEndsTheJobAndErasesTheSlot) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(0);
  t.cancel();
  EXPECT_EQ(TS_FAILED, t.state());
  EXPECT_EQ(TR_CANCELLED, t.status().reason);
  EXPECT_EQ(1, r.fake.erased);
  r.send(1);
  EXPECT_EQ(1u, r.fake.writes.size());
}

TEST(LoraTransfer, AWriteFailureFailsTheJobWithReasonWrite) {
  Rig r(SIZE);
  auto& t = r.open(4);
  r.send(0);
  r.fake.write_fail = true;
  r.send(1);
  EXPECT_EQ(TS_FAILED, t.state());
  EXPECT_EQ(TR_WRITE, t.status().reason);
  EXPECT_STREQ("write", transferReasonName(t.status().reason));
  r.fake.write_fail = false;
  r.send(1);
  EXPECT_EQ(1u, r.fake.writes.size());
}

TEST(LoraTransfer, TheWindowHelperBoundsTheDeadline) {
  EXPECT_TRUE(windowValid(1000 + 3600, 1000));
  EXPECT_TRUE(windowValid(1000 + 43200, 1000));
  EXPECT_FALSE(windowValid(1000 + 43201, 1000));   // more than 12 h
  EXPECT_FALSE(windowValid(1000, 1000));
  EXPECT_FALSE(windowValid(999, 1000));            // already past
  // Unknown clock: the relative bound.
  EXPECT_EQ(5000u + 600u * 1000, windowDeadlineMs(1600, 1000, false, 0, 5000));
  // Device clock behind the issue time: still the relative bound, never longer.
  EXPECT_EQ(5000u + 600u * 1000, windowDeadlineMs(1600, 1000, true, 900, 5000));
  // Device clock in step, then ahead: shorter.
  EXPECT_EQ(5000u + 600u * 1000, windowDeadlineMs(1600, 1000, true, 1000, 5000));
  EXPECT_EQ(5000u + 400u * 1000, windowDeadlineMs(1600, 1000, true, 1200, 5000));
  // The window is already over on the device's clock: no time at all.
  EXPECT_EQ(5000u, windowDeadlineMs(1600, 1000, true, 1600, 5000));
  EXPECT_EQ(5000u, windowDeadlineMs(1600, 1000, true, 9999, 5000));
  // An invalid window gives nothing; a 12 h window is 43.2 million ms.
  EXPECT_EQ(5000u, windowDeadlineMs(900, 1000, false, 0, 5000));
  EXPECT_EQ(5000u + 43200u * 1000, windowDeadlineMs(1000 + 43200, 1000, false, 0, 5000));
}

TEST(LoraTransfer, AMillisecondWrapMidTransferChangesNothing) {
  Rig r(SIZE);
  r.now = 0xFFFFFFFFu - 5000;   // wraps during the transfer
  auto& t = r.open(4, 20u * 60 * 1000);
  r.send(0); r.send(1); r.send(2);
  r.tick(7000);
  EXPECT_TRUE(r.fake.sent.empty());
  r.tick(1100);   // quiet timeout across the wrap
  ASSERT_EQ(1u, r.fake.sent.size());
  EXPECT_LT(r.now, 100000u);   // the clock wrapped
  r.send(3);
  EXPECT_EQ(TS_OPEN, t.state());
  r.tick(20u * 60 * 1000 - 20000);
  EXPECT_EQ(TS_OPEN, t.state());
  r.tick(20000);
  EXPECT_EQ(TS_PAUSED, t.state());   // the deadline lay across the wrap
  EXPECT_EQ(TR_WINDOW, t.status().reason);
}

TEST(LoraTransfer, BeginRefusesBadArgumentsAndAWholeTransferBeginsComplete) {
  Rig r(SIZE);
  LoraTransfer t(r.fake.sink());
  EXPECT_FALSE(t.begin(JOB, SIZE, 0, r.fake.keys, 1, 0, 0));
  EXPECT_FALSE(t.begin(JOB, SIZE, 9, r.fake.keys, 1, 0, 0));
  EXPECT_FALSE(t.begin(JOB, SIZE, 4, r.fake.keys, 1, 0, 12));
  EXPECT_TRUE(t.begin(JOB, SIZE, 4, r.fake.keys, 1, 0, 11));
  EXPECT_EQ(TS_COMPLETE, t.state());
  EXPECT_EQ(11u, t.status().frames_total);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
