// The kept reports' ring (src/helpers/iface/KeptReports.h, docs/contract/TELEMETRY.md "Reports kept for later"): what a
// report costs in flash writes, the overflow count, the release mark, what a reboot keeps, and a write a power cut stopped.
#include <gtest/gtest.h>
#include <map>
#include <string.h>
#include <vector>
#include "helpers/iface/KeptReports.h"

using namespace Iface;

namespace {

struct RamKept : KeptStore {
  std::map<int, std::vector<uint8_t>> f;
  bool fail_append = false;
  uint32_t free_bytes = 0xFFFFFFFFu;
  size_t size(int slot) override { return f.count(slot) ? f[slot].size() : 0; }
  bool read(int slot, size_t off, uint8_t* buf, size_t n) override {
    if (!f.count(slot) || off + n > f[slot].size()) return false;
    memcpy(buf, f[slot].data() + off, n);
    return true;
  }
  bool append(int slot, const uint8_t* buf, size_t n) override {
    if (fail_append) return false;
    f[slot].insert(f[slot].end(), buf, buf + n);
    return true;
  }
  bool replace(int slot, const uint8_t* buf, size_t n) override { f[slot].assign(buf, buf + n); return true; }
  void remove(int slot) override { f.erase(slot); }
  uint32_t freeBytes() override { return free_bytes; }
  size_t total() { size_t t = 0; for (auto& kv : f) t += kv.second.size(); return t; }
};

// A report as the firmware builds one: its header carries boot and seq, and a signature follows the fields.
std::vector<uint8_t> report(uint32_t boot, uint32_t seq, uint8_t fields = RF_HEALTH | RF_RADIO) {
  uint8_t b[REPORT_MAX];
  ReportData d;
  const size_t n = encodeReport(b, sizeof(b), fields, boot, seq, 1000 + seq, d);
  std::vector<uint8_t> v(b, b + n);
  for (int i = 0; i < 64; i++) v.push_back((uint8_t)(seq * 7 + i));   // stands for the signature
  return v;
}

struct Seen {
  std::vector<KeptEntry> e;
};
bool collect(void* user, const KeptEntry& e) { static_cast<Seen*>(user)->e.push_back(e); return true; }

Seen all(KeptReports& k, uint32_t after = 0) {
  Seen s;
  k.each(after, collect, &s);
  return s;
}

void fill(KeptReports& k, uint32_t n, uint32_t first_seq = 1) {
  for (uint32_t i = 0; i < n; i++) ASSERT_TRUE(k.add(report(1, first_seq + i).data(), report(1, first_seq + i).size(), 7));
}

}  // namespace

TEST(Kept, ReportsComeBackOldestFirstWithTheirExactBytesAndCursorsFromOne) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  EXPECT_EQ(k.last(), 0u);
  fill(k, 3);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 3u);
  for (uint32_t i = 0; i < 3; i++) {
    EXPECT_EQ(got.e[i].cursor, i + 1);
    const std::vector<uint8_t> want = report(1, i + 1);
    ASSERT_EQ(got.e[i].len, want.size());
    EXPECT_EQ(memcmp(got.e[i].bytes, want.data(), want.size()), 0) << "the bytes as they were sent, signature included";
    EXPECT_EQ(got.e[i].owner, 7u);
  }
  EXPECT_EQ(k.dropped(), 0u);
  EXPECT_EQ(all(k, 2).e.size(), 1u) << "after a cursor";
}

TEST(Kept, ARingOf64KeepsTheLast64AndCountsWhatItPushedOut) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 64);
  EXPECT_EQ(k.dropped(), 0u) << "64 fit";
  fill(k, 1, 65);
  EXPECT_EQ(k.dropped(), 1u) << "the 65th pushes the first out";
  fill(k, 5, 66);
  EXPECT_EQ(k.dropped(), 6u);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 64u) << "a listing shows the last 64, whatever the segments still hold";
  EXPECT_EQ(got.e.front().cursor, 7u);
  EXPECT_EQ(got.e.back().cursor, 70u);
  EXPECT_EQ(k.held(), 64u);
}

TEST(Kept, FlashIsBoundedAndNothingIsEverRewrittenToMakeRoom) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 400);
  EXPECT_EQ(k.appends(), 400u) << "one write per report";
  EXPECT_EQ(k.replaces(), 0u) << "no file is rewritten to make room";
  EXPECT_LE(s.f.size(), (size_t)KeptReports::NSEG) << "at most five segment files";
  EXPECT_LE(s.total(), (size_t)(KeptReports::NSEG * KeptReports::SEG * (KeptReports::RECORD_OVERHEAD + REPORT_MAX)));
  EXPECT_EQ(k.removes(), 400u / KeptReports::SEG - KeptReports::NSEG) << "one file removed per segment started once the slots are full";
  EXPECT_EQ(k.dropped(), 400u - 64u);
}

TEST(Kept, AReleaseFreesWhatItNamesAndRestartsTheDroppedCount) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 70);
  ASSERT_EQ(k.dropped(), 6u);
  uint32_t rel = 0;
  ASSERT_TRUE(k.release(10, &rel));
  EXPECT_EQ(rel, 4u) << "cursors 7..10 were the held ones up to it; 1..6 had gone already";
  EXPECT_EQ(k.dropped(), 0u) << "since the last release";
  ASSERT_FALSE(all(k).e.empty());
  EXPECT_EQ(all(k).e.front().cursor, 11u);
  EXPECT_EQ(k.held(), 60u);
  fill(k, 10, 71);   // 80 reports: the ring now holds 17..80
  EXPECT_EQ(k.dropped(), 6u) << "11..16 are pushed out since the release";
  EXPECT_FALSE(k.release(81, &rel)) << "a cursor not yet given";
  EXPECT_FALSE(k.release(0, &rel));
  ASSERT_TRUE(k.release(10, &rel));   // the pushed-out ones are now behind this release's mark
  const uint32_t writes = k.replaces();
  ASSERT_TRUE(k.release(10, &rel));
  EXPECT_EQ(rel, 0u);
  EXPECT_EQ(k.replaces(), writes) << "a repeat writes nothing";
}

TEST(Kept, ReleasingEverythingRemovesTheFilesAndCursorsAreNeverReused) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 48);
  uint32_t rel = 0;
  ASSERT_TRUE(k.release(48, &rel));
  EXPECT_EQ(rel, 48u);
  EXPECT_EQ(s.f.count(0) + s.f.count(1) + s.f.count(2), 0u) << "the segments are gone";
  EXPECT_EQ(k.held(), 0u);
  KeptReports again;   // a reboot
  again.begin(&s);
  EXPECT_EQ(again.last(), 48u);
  fill(again, 1, 49);
  const Seen next = all(again);
  ASSERT_EQ(next.e.size(), 1u) << "only the new one: the released ones stay released";
  EXPECT_EQ(next.e.front().cursor, 49u) << "the next report takes the next cursor";
}

TEST(Kept, ARebootKeepsTheReportsTheMarkAndTheCursor) {
  RamKept s;
  {
    KeptReports k;
    k.begin(&s);
    fill(k, 30);
    uint32_t rel;
    ASSERT_TRUE(k.release(5, &rel));
  }
  KeptReports k;
  k.begin(&s);
  EXPECT_EQ(k.last(), 30u);
  EXPECT_EQ(k.mark(), 5u);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 25u);
  EXPECT_EQ(got.e.front().cursor, 6u) << "the released ones stay released, in a segment still partly held";
  EXPECT_EQ(memcmp(got.e.back().bytes, report(1, 30).data(), report(1, 30).size()), 0);
}

TEST(Kept, AWriteACutPowerStoppedLeavesTheValidReportsAndTheNextOneTakesItsPlace) {
  RamKept s;
  {
    KeptReports k;
    k.begin(&s);
    fill(k, 5);
  }
  const std::vector<uint8_t> r = report(1, 6);
  s.f[0].insert(s.f[0].end(), r.begin(), r.begin() + 40);   // half of the sixth record's bytes
  KeptReports k;
  k.begin(&s);
  EXPECT_EQ(k.last(), 5u) << "the half record is not a report";
  EXPECT_EQ(all(k).e.size(), 5u);
  fill(k, 1, 6);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 6u);
  EXPECT_EQ(got.e.back().cursor, 6u);
  EXPECT_EQ(memcmp(got.e.back().bytes, r.data(), r.size()), 0);
  KeptReports third;   // and the file reads whole after it
  third.begin(&s);
  EXPECT_EQ(all(third).e.size(), 6u);
}

TEST(Kept, ARecordWhoseBytesChangedInFlashIsNotAReport) {
  RamKept s;
  {
    KeptReports k;
    k.begin(&s);
    fill(k, 4);
  }
  s.f[0][s.f[0].size() - 40] ^= 0x10;   // a flipped bit inside the fourth record: its length is intact, its CRC is not
  KeptReports k;
  k.begin(&s);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 3u) << "the first three are whole, and nothing after the bad one is trusted";
  EXPECT_EQ(k.last(), 3u);
}

TEST(Kept, AFailedWriteIsCountedAsDroppedAndTheNextReportRetriesTheCursor) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 2);
  s.fail_append = true;
  EXPECT_FALSE(k.add(report(1, 3).data(), report(1, 3).size(), 7));
  EXPECT_EQ(k.dropped(), 1u);
  s.fail_append = false;
  fill(k, 1, 4);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 3u);
  EXPECT_EQ(got.e.back().cursor, 3u) << "the failed one took no cursor";
  EXPECT_EQ(k.dropped(), 1u);
}

TEST(Kept, ANewSegmentIsNotStartedWhereTheFlashIsNearlyFull) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 16);
  s.free_bytes = KeptReports::RESERVE - 1;
  EXPECT_FALSE(k.add(report(1, 17).data(), report(1, 17).size(), 7)) << "the 17th starts a segment";
  EXPECT_EQ(k.dropped(), 1u);
  s.free_bytes = KeptReports::RESERVE;
  fill(k, 1, 17);
  EXPECT_EQ(k.last(), 17u);
}

TEST(Kept, ThingsThatAreNotCompactReportsAreRefused) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  uint8_t junk[100] = {0};
  EXPECT_FALSE(k.add(junk, sizeof(junk), 1));
  std::vector<uint8_t> r = report(1, 1);
  EXPECT_FALSE(k.add(r.data(), 20, 1));
  EXPECT_EQ(k.last(), 0u);
}

TEST(Kept, ClearGoesOnFromTheSameCursorAndKeepsNothing) {
  RamKept s;
  KeptReports k;
  k.begin(&s);
  fill(k, 20);
  k.clear();
  EXPECT_EQ(all(k).e.size(), 0u);
  fill(k, 1, 21);
  const Seen after = all(k);
  ASSERT_EQ(after.e.size(), 1u);
  EXPECT_EQ(after.e.front().cursor, 21u);
}

// The nRF52 setting (StandingOps.cpp): eight reports to one write, because its file system wears out a directory page fast.
TEST(KeptBatch, SixtyFourReportsCostEightAppends) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 64);
  EXPECT_EQ(k.appends(), 8u) << "one write per eight reports";
  EXPECT_EQ(k.pending(), 0u);
  EXPECT_EQ(all(k).e.size(), 64u);
  KeptReports plain;
  RamKept s2;
  plain.begin(&s2, 1);
  fill(plain, 64);
  EXPECT_EQ(plain.appends(), 64u) << "unbatched, as on ESP32";
}

TEST(KeptBatch, ABatchAcrossASegmentEdgeIsWrittenInTwoPartsAndReadsBackInOrder) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 4);
  k.flush();   // the next eight start at cursor 5 and run past 16
  fill(k, 24, 5);
  const Seen got = all(k);
  ASSERT_EQ(got.e.size(), 28u);
  for (size_t i = 0; i < got.e.size(); i++) EXPECT_EQ(got.e[i].cursor, i + 1);
  KeptReports again;
  again.begin(&s, 8);
  EXPECT_EQ(all(again).e.size(), 28u) << "and from flash";
}

TEST(KeptBatch, AListFlushesFirstSoItShowsEverything) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 3);
  EXPECT_EQ(k.pending(), 3u);
  EXPECT_EQ(k.appends(), 0u) << "waiting in RAM";
  EXPECT_EQ(all(k).e.size(), 3u);
  EXPECT_EQ(k.pending(), 0u);
  EXPECT_EQ(k.appends(), 1u);
}

TEST(KeptBatch, AnAckFlushesFirstAndKnowsTheCursorsOfWhatWaited) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 3);
  uint32_t rel = 0;
  ASSERT_TRUE(k.release(3, &rel)) << "cursor 3 is given, though it has not been written yet";
  EXPECT_EQ(rel, 3u);
}

TEST(KeptBatch, WhatHasWaitedAnHourIsWrittenAndNotBefore) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  const std::vector<uint8_t> r = report(1, 1);
  ASSERT_TRUE(k.add(r.data(), r.size(), 7, 1000));
  k.tick(1000 + KeptReports::HOLD_MS - 1);
  EXPECT_EQ(k.pending(), 1u);
  k.tick(1000 + KeptReports::HOLD_MS);
  EXPECT_EQ(k.pending(), 0u);
  EXPECT_EQ(s.total() > 0, true);
}

TEST(KeptBatch, TheShutdownPathWritesWhatWaits) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 5);
  EXPECT_EQ(s.total(), 0u);
  KeptReports::flushAll();
  EXPECT_EQ(k.pending(), 0u);
  KeptReports after;   // a reboot
  after.begin(&s, 8);
  EXPECT_EQ(all(after).e.size(), 5u);
}

TEST(KeptBatch, APowerCutLosesAtMostWhatWaited) {
  RamKept s;
  {
    KeptReports k;
    k.begin(&s, 8);
    fill(k, 11);   // eight written, three waiting
  }
  KeptReports after;
  after.begin(&s, 8);
  EXPECT_EQ(all(after).e.size(), 8u);
  EXPECT_EQ(after.last(), 8u) << "the cursors of the lost ones were never given out, and are taken again";
}

TEST(KeptBatch, AFailedWriteGivesTheCursorsBackAndCountsTheReports) {
  RamKept s;
  KeptReports k;
  k.begin(&s, 8);
  fill(k, 5);
  s.fail_append = true;
  all(k);   // the flush fails
  EXPECT_EQ(k.dropped(), 5u);
  s.fail_append = false;
  fill(k, 1, 6);
  EXPECT_EQ(all(k).e.front().cursor, 1u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
