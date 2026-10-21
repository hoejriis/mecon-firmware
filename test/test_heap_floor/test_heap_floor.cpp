// The enforced heap floor (src/helpers/HeapFloor.h): when an episode starts and ends, that one episode is one audit
// entry, that the largest block (not the total) is the figure, the check period, the reply of `get mecon.mem`, and the
// wording of the home status line.
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/HeapFloor.h"
#include "helpers/cli/MeconCli.h"
#include "helpers/connmode/ConnStatus.h"

namespace {
MemStats mem(uint32_t free_bytes, uint32_t largest, bool known = true) {
  MemStats m;
  m.free = free_bytes; m.largest = largest; m.known = known;
  return m;
}
const uint32_t FLOOR = 20480, HYST = 2048, PERIOD = 60000;
}  // namespace

class HeapFloorTest : public ::testing::Test {
protected:
  Posture::AuditRing audit;
  HeapFloor h;
  void SetUp() override { h.begin(FLOOR, HYST, PERIOD); }
  int episodes() const {
    for (int i = 0; i < audit.n; i++)
      if (audit.e[i].event == Posture::EV_LOW_MEM) return audit.e[i].count;
    return 0;
  }
};

TEST_F(HeapFloorTest, AtOrAboveTheFloorIsNotLow) {
  EXPECT_FALSE(h.check(0, 100, mem(100000, FLOOR), audit));       // exactly the floor is fine
  EXPECT_FALSE(h.low());
  EXPECT_EQ(audit.n, 0);
}

TEST_F(HeapFloorTest, OneByteBelowTheFloorIsLowAndAuditedOnce) {
  EXPECT_TRUE(h.check(0, 100, mem(100000, FLOOR - 1), audit));
  EXPECT_TRUE(h.low());
  ASSERT_EQ(audit.n, 1);
  EXPECT_EQ(audit.e[0].event, Posture::EV_LOW_MEM);
  EXPECT_EQ(audit.e[0].caller, Posture::CALLER_SYSTEM);
  EXPECT_EQ(audit.e[0].what, Posture::W_HEAP_FLOOR);
  EXPECT_EQ(audit.e[0].ts, 100u);
}

TEST_F(HeapFloorTest, AnEpisodeIsOneEntryHoweverLongItLasts) {
  h.check(0, 100, mem(100000, 10000), audit);
  for (int i = 1; i <= 50; i++) EXPECT_FALSE(h.check(i * PERIOD, 100 + i, mem(100000, 5000 + i), audit));   // deeper, still low
  EXPECT_TRUE(h.low());
  EXPECT_EQ(episodes(), 1);
}

TEST_F(HeapFloorTest, TheEpisodeEndsOnlyAboveTheFloorPlusHysteresis) {
  h.check(0, 100, mem(100000, 1000), audit);
  EXPECT_FALSE(h.check(PERIOD, 101, mem(100000, FLOOR), audit));                // back at the floor is not enough
  EXPECT_TRUE(h.low());
  EXPECT_FALSE(h.check(2 * PERIOD, 102, mem(100000, FLOOR + HYST - 1), audit));
  EXPECT_TRUE(h.low());
  EXPECT_TRUE(h.check(3 * PERIOD, 103, mem(100000, FLOOR + HYST), audit));      // floor + hysteresis ends it
  EXPECT_FALSE(h.low());
}

TEST_F(HeapFloorTest, ABlockHoveringAtTheFloorMakesOneEntryNotOnePerLook) {
  h.check(0, 100, mem(100000, FLOOR - 1), audit);
  for (int i = 1; i < 20; i++) {                   // wobbles between floor - 1 and floor + hysteresis - 1: never recovers
    h.check(i * PERIOD, 100 + i, mem(100000, i % 2 ? FLOOR + HYST - 1 : FLOOR - 1), audit);
  }
  EXPECT_EQ(episodes(), 1);
}

TEST_F(HeapFloorTest, EachNewEpisodeAddsOneToTheSameEntry) {
  for (int ep = 1; ep <= 3; ep++) {
    h.check(ep * 10 * PERIOD, 100 + ep, mem(100000, 1000), audit);
    EXPECT_TRUE(h.low());
    EXPECT_EQ(episodes(), ep);
    h.check(ep * 10 * PERIOD + PERIOD, 200 + ep, mem(100000, 100000), audit);
    EXPECT_FALSE(h.low());
  }
  EXPECT_EQ(audit.n, 1);                           // folded: one kind of event, a count of 3
}

TEST_F(HeapFloorTest, TheLargestBlockIsTheFigureNotTheTotal) {
  EXPECT_TRUE(h.check(0, 100, mem(300000, 1000), audit));    // plenty free, no usable block: fragmented
  EXPECT_TRUE(h.low());
  h.begin(FLOOR, HYST, PERIOD);
  Posture::AuditRing a2;
  EXPECT_FALSE(h.check(0, 100, mem(1000, 90000), a2));       // little free in total (an impossible figure) but a big block: not low
  EXPECT_FALSE(h.low());
}

TEST_F(HeapFloorTest, DisabledOrUnmeasurableNeverFires) {
  HeapFloor off;
  off.begin(0);
  EXPECT_FALSE(off.enabled());
  EXPECT_FALSE(off.due(1000000));
  EXPECT_FALSE(off.check(0, 100, mem(0, 0), audit));
  EXPECT_FALSE(off.low());
  EXPECT_FALSE(h.check(0, 100, mem(0, 0, false), audit));    // known = false: the numbers are not measurements
  EXPECT_FALSE(h.low());
  EXPECT_EQ(audit.n, 0);
}

TEST_F(HeapFloorTest, TheFirstLookIsImmediateThenOnePerPeriod) {
  EXPECT_TRUE(h.due(5));                                      // after setup
  h.check(5, 100, mem(100000, 100000), audit);
  EXPECT_FALSE(h.due(5 + PERIOD - 1));
  EXPECT_TRUE(h.due(5 + PERIOD));
}

TEST_F(HeapFloorTest, ThePeriodSurvivesTheMillisecondCounterWrapping) {
  const uint32_t t0 = 0xFFFFFFFFu - 1000;
  h.check(t0, 100, mem(100000, 100000), audit);
  EXPECT_FALSE(h.due(t0 + PERIOD - 1));
  EXPECT_TRUE(h.due(t0 + PERIOD));                            // wrapped past zero
}

TEST_F(HeapFloorTest, TheAuditEntryIsReadableInGetAudit) {
  h.check(0, 1760000000u, mem(100000, 1000), audit);
  char r[160];
  audit.formatPage(0, r, sizeof(r));
  EXPECT_STREQ(r, "> 1 entries\n1760000000 low-mem system heap-floor x1");
}

TEST_F(HeapFloorTest, TheFileImageKeepsTheEntry) {
  h.check(0, 100, mem(100000, 1000), audit);
  uint8_t img[Posture::AuditRing::FILE_SIZE];
  audit.serialize(img);
  Posture::AuditRing back;
  ASSERT_TRUE(back.deserialize(img, sizeof(img)));
  EXPECT_EQ(back.e[0].event, Posture::EV_LOW_MEM);
  EXPECT_EQ(back.e[0].what, Posture::W_HEAP_FLOOR);
}

TEST(HeapFloorReply, MemReportsTheFlagAndTheFloor) {
  char r[200];
  HeapFloor h;
  h.begin(FLOOR, HYST, PERIOD);
  Cli::formatMem(mem(105600, 94196), h, r, sizeof(r));
  EXPECT_STREQ(r, "> heap free=105600 largest=94196 min_free=0 heap_low=0 floor=20480");
  Posture::AuditRing a;
  h.check(0, 1, mem(105600, 100), a);
  Cli::formatMem(mem(105600, 100), h, r, sizeof(r));
  EXPECT_STREQ(r, "> heap free=105600 largest=100 min_free=0 heap_low=1 floor=20480");
  HeapFloor off;
  off.begin(0);
  Cli::formatMem(mem(105600, 94196), off, r, sizeof(r));
  EXPECT_STREQ(r, "> heap free=105600 largest=94196 min_free=0 heap_low=0 floor=0");
  Cli::formatMem(mem(0, 0, false), h, r, sizeof(r));
  EXPECT_STREQ(r, "> heap: not measurable on this platform");
}

// On a board with PSRAM (Heltec V4) the figures are internal RAM and the PSRAM is reported beside them, so a large
// PSRAM block can never hide internal RAM running out.
TEST(HeapFloorReply, PsramIsReportedApartAndDoesNotMaskLowInternalRam) {
  char r[200];
  HeapFloor h;
  h.begin(FLOOR, HYST, PERIOD);
  MemStats m = mem(40000, 100);
  m.psram = 2031604;
  Posture::AuditRing a;
  h.check(0, 1, m, a);
  EXPECT_TRUE(h.low());
  Cli::formatMem(m, h, r, sizeof(r));
  EXPECT_STREQ(r, "> heap free=40000 largest=100 min_free=0 heap_low=1 floor=20480 psram_free=2031604");
}

TEST(HeapFloorScreen, TheHomeLineCarriesLowMemWhenItFitsAndAloneWhenItDoesNot) {
  ConnMode::ConnStatus s;
  char out[64];
  s.mode = ConnMode::BLE;
  ConnMode::formatHome(s, out, sizeof(out));
  EXPECT_STREQ(out, "BLE");
  s.heap_low = true;
  ConnMode::formatHome(s, out, sizeof(out));
  EXPECT_STREQ(out, "BLE LOW MEM");
  s.ble_pin_default = true;                       // "BLE (default PIN!)" + marker is 26 characters
  ConnMode::formatHome(s, out, sizeof(out));
  EXPECT_STREQ(out, "LOW MEM");
  for (size_t cap : {size_t(8), size_t(9), size_t(12)}) {   // a small buffer is never overrun
    char small[16];
    memset(small, 'x', sizeof(small));
    ConnMode::formatHome(s, small, cap);
    EXPECT_LT(strlen(small), cap);
  }
  s.heap_low = false;
  ConnMode::formatHome(s, out, sizeof(out));
  EXPECT_STREQ(out, "BLE (default PIN!)");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
