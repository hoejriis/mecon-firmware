// The provisioned marker's flash page (src/helpers/deployment/DepMarkerPage.h): the slot log, its fail-closed reading,
// and every fault a flash page can show (torn writes, failed erases, another firmware's data over the page).
#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/deployment/DepMarkerPage.h"

using namespace Dep;

// A 4 KB page that behaves like NOR flash: a write can only clear bits, an erase sets every byte to 0xFF.
class RamPage : public FlashPage {
public:
  std::vector<uint8_t> b = std::vector<uint8_t>(4096, 0xFF);
  int erases = 0, writes = 0;
  bool fail_read = false, fail_write = false, fail_erase = false;
  bool stuck = false;      // writes report success and store nothing (worn or write-protected flash)
  size_t tear_after = 0;   // >0: the next write stores only this many bytes, then fails (a power cut)
  size_t size() const override { return b.size(); }
  bool read(uint32_t off, void* buf, size_t n) override {
    if (fail_read || off + n > b.size()) return false;
    memcpy(buf, b.data() + off, n);
    return true;
  }
  bool write(uint32_t off, const void* buf, size_t n) override {
    if (fail_write || off % 4 || n % 4 || off + n > b.size()) return false;
    if (stuck) { writes++; return true; }
    const uint8_t* s = static_cast<const uint8_t*>(buf);
    size_t k = tear_after ? tear_after : n;
    for (size_t i = 0; i < k && i < n; i++) b[off + i] &= s[i];
    writes++;
    if (tear_after) { tear_after = 0; return false; }
    return true;
  }
  bool erase() override {
    if (fail_erase) return false;
    std::fill(b.begin(), b.end(), 0xFF);
    erases++;
    return true;
  }
};

TEST(MarkerPage, AnErasedPageIsAbsentAndSetClearSetFollow) {
  RamPage p;
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
  ASSERT_TRUE(markerPageWrite(p, true));
  EXPECT_EQ(MK_PRESENT, markerPageRead(p));
  ASSERT_TRUE(markerPageWrite(p, false));
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
  ASSERT_TRUE(markerPageWrite(p, true));
  EXPECT_EQ(MK_PRESENT, markerPageRead(p));
  EXPECT_EQ(0, p.erases) << "three changes fit the log without an erase";
}

TEST(MarkerPage, TheSameStateTwiceUsesNoSlot) {
  RamPage p;
  ASSERT_TRUE(markerPageWrite(p, false));   // absent on an erased page: nothing to write
  EXPECT_EQ(0, p.writes);
  ASSERT_TRUE(markerPageWrite(p, true));
  ASSERT_TRUE(markerPageWrite(p, true));
  EXPECT_EQ(1, p.writes);
}

TEST(MarkerPage, AFullPageIsErasedOnceEvery256Changes) {
  RamPage p;
  for (int i = 0; i < 600; i++) {
    ASSERT_TRUE(markerPageWrite(p, i % 2 == 0)) << i;
    EXPECT_EQ(i % 2 == 0 ? MK_PRESENT : MK_ABSENT, markerPageRead(p)) << i;
  }
  EXPECT_EQ(2, p.erases);   // slots 0..255, then 256..511, then the rest
}

TEST(MarkerPage, AnythingThatIsNotOursReadsPresent) {
  RamPage p;
  for (size_t i = 0; i < p.b.size(); i++) p.b[i] = (uint8_t)(i * 37 + 11);   // another firmware's file system data
  EXPECT_EQ(MK_PRESENT, markerPageRead(p)) << "fail closed: never reopen a first provisioning";
  ASSERT_TRUE(markerPageWrite(p, false));   // a clear erases it and starts the log afresh
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
  EXPECT_EQ(1, p.erases);
}

TEST(MarkerPage, ATornSlotReadsPresentAndTheNextWriteRecovers) {
  RamPage p;
  ASSERT_TRUE(markerPageWrite(p, true));
  ASSERT_TRUE(markerPageWrite(p, false));
  p.tear_after = 8;   // the power fails halfway through the next slot
  EXPECT_FALSE(markerPageWrite(p, true));
  EXPECT_EQ(MK_PRESENT, markerPageRead(p)) << "a torn slot is not ours: fail closed";
  ASSERT_TRUE(markerPageWrite(p, false));
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
}

TEST(MarkerPage, ASlotWithABadCrcOrBadInverseReadsPresent) {
  RamPage p;
  ASSERT_TRUE(markerPageWrite(p, true));
  RamPage q = p;
  q.b[12] = 0x00;   // bits of the CRC cleared: it no longer matches
  EXPECT_EQ(MK_PRESENT, markerPageRead(q));
  RamPage r;
  ASSERT_TRUE(markerPageWrite(r, true));
  ASSERT_TRUE(markerPageWrite(r, false));
  r.b[16 + 8] = 0x00;   // the second slot's inverted copy damaged: it must not count as a clear
  EXPECT_EQ(MK_PRESENT, markerPageRead(r));
}

TEST(MarkerPage, DataAfterAnErasedSlotReadsPresent) {
  RamPage p;
  ASSERT_TRUE(markerPageWrite(p, true));
  ASSERT_TRUE(markerPageWrite(p, false));
  p.b[4000] = 0x00;   // something far down the page, after the log's end
  EXPECT_EQ(MK_PRESENT, markerPageRead(p));
}

TEST(MarkerPage, FailuresAreReportedAndFailClosed) {
  RamPage p;
  p.fail_read = true;
  EXPECT_EQ(MK_PRESENT, markerPageRead(p)) << "an unreadable page is present";
  p.fail_read = false;
  p.fail_write = true;
  EXPECT_FALSE(markerPageWrite(p, true));
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
  p.fail_write = false;
  for (size_t i = 0; i < 64; i++) p.b[i] = 0x00;   // not ours: a write must erase, and the erase fails
  p.fail_erase = true;
  EXPECT_FALSE(markerPageWrite(p, false));
  EXPECT_EQ(MK_PRESENT, markerPageRead(p));
}

TEST(MarkerPage, AWriteThatDoesNotStickIsAFailure) {
  RamPage p;
  p.stuck = true;
  EXPECT_FALSE(markerPageWrite(p, true)) << "only the read-back proves the marker is set";
  EXPECT_EQ(MK_ABSENT, markerPageRead(p));
}

TEST(MarkerPage, TheCrcIsTheStandardCrc32) {
  EXPECT_EQ(0xCBF43926u, markerCrc32(reinterpret_cast<const uint8_t*>("123456789"), 9));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
