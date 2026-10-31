// The security floor's flash page on the nRF52 (src/helpers/updates/UpdFloorPage.h): the slot log, its fail-closed
// reading, and the faults a flash page can show.
#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/updates/UpdFloorPage.h"

using namespace Dep;
using namespace Upd;

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

static uint32_t floorOf(RamPage& p) {
  uint32_t f = 0xDEADBEEF;
  EXPECT_TRUE(floorPageRead(p, &f));
  return f;
}

TEST(FloorPage, AnErasedPageIsAFloorOfZero) {
  RamPage p;
  EXPECT_EQ(0u, floorOf(p));
}

TEST(FloorPage, ARaiseStoresTheFloorAndOnlyARaiseWrites) {
  RamPage p;
  ASSERT_TRUE(floorPageRaise(p, 0x00090100));
  EXPECT_EQ(0x00090100u, floorOf(p));
  ASSERT_TRUE(floorPageRaise(p, 0x00080000)) << "a lower version is already covered";
  ASSERT_TRUE(floorPageRaise(p, 0x00090100));
  EXPECT_EQ(1, p.writes);
  EXPECT_EQ(0x00090100u, floorOf(p)) << "never lowered";
  ASSERT_TRUE(floorPageRaise(p, 0x00090200));
  EXPECT_EQ(0x00090200u, floorOf(p));
  EXPECT_EQ(2, p.writes);
}

TEST(FloorPage, AFullPageIsErasedAndTheFloorKept) {
  RamPage p;
  for (uint32_t v = 1; v <= 600; v++) {
    ASSERT_TRUE(floorPageRaise(p, v)) << v;
    EXPECT_EQ(v, floorOf(p)) << v;
  }
  EXPECT_EQ(2, p.erases);   // 256 slots per page
}

TEST(FloorPage, AnythingThatIsNotOursCannotBeReadAndIsLeftAlone) {
  RamPage p;
  for (size_t i = 0; i < p.b.size(); i++) p.b[i] = (uint8_t)(i * 37 + 11);
  uint32_t f = 1;
  EXPECT_FALSE(floorPageRead(p, &f));
  EXPECT_EQ(0u, f);
  EXPECT_FALSE(floorPageRaise(p, 5)) << "fail closed: no install passes, nothing erased";
  EXPECT_EQ(0, p.erases);
  EXPECT_EQ(0, p.writes);
}

TEST(FloorPage, DataAfterTheValidSlotsCannotBeRead) {
  RamPage p;
  ASSERT_TRUE(floorPageRaise(p, 7));
  p.b[100] = 0x00;   // stray bits in an erased slot
  uint32_t f;
  EXPECT_FALSE(floorPageRead(p, &f));
}

TEST(FloorPage, ATornWriteCannotBeRead) {
  RamPage p;
  ASSERT_TRUE(floorPageRaise(p, 7));
  p.tear_after = 8;
  EXPECT_FALSE(floorPageRaise(p, 9));
  uint32_t f;
  EXPECT_FALSE(floorPageRead(p, &f)) << "half a slot is not ours";
}

TEST(FloorPage, ASlotWithABadComplementOrCrcIsNotValid) {
  uint32_t w[4] = {FLOOR_MAGIC, 5, ~5u, 0};
  w[3] = markerCrc32(reinterpret_cast<const uint8_t*>(w), 12);
  uint32_t f = 0;
  EXPECT_TRUE(floorSlotValid(reinterpret_cast<const uint8_t*>(w), &f));
  EXPECT_EQ(5u, f);
  uint32_t a[4];
  memcpy(a, w, sizeof(a)); a[0] ^= 1;
  EXPECT_FALSE(floorSlotValid(reinterpret_cast<const uint8_t*>(a), &f));
  memcpy(a, w, sizeof(a)); a[2] ^= 1;
  a[3] = markerCrc32(reinterpret_cast<const uint8_t*>(a), 12);   // a bad complement under a good crc
  EXPECT_FALSE(floorSlotValid(reinterpret_cast<const uint8_t*>(a), &f));
  memcpy(a, w, sizeof(a)); a[3] ^= 1;
  EXPECT_FALSE(floorSlotValid(reinterpret_cast<const uint8_t*>(a), &f));
}

TEST(FloorPage, AStuckWriteIsNotARaise) {
  RamPage p;
  p.stuck = true;
  EXPECT_FALSE(floorPageRaise(p, 3)) << "only the read-back proves it";
  EXPECT_EQ(0u, floorOf(p));
}

TEST(FloorPage, AFailedEraseOnAFullPageKeepsTheOldFloor) {
  RamPage p;
  for (uint32_t v = 1; v <= 256; v++) ASSERT_TRUE(floorPageRaise(p, v));
  p.fail_erase = true;
  EXPECT_FALSE(floorPageRaise(p, 300));
  EXPECT_EQ(256u, floorOf(p));
}

TEST(FloorPage, AnUnreadablePageReadsAsUnreadable) {
  RamPage p;
  p.fail_read = true;
  uint32_t f;
  EXPECT_FALSE(floorPageRead(p, &f));
  EXPECT_FALSE(floorPageRaise(p, 1));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
