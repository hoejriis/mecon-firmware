#include <gtest/gtest.h>
#include <math.h>
#include "helpers/RegulatoryDutyCycle.h"

using namespace regulatory;

// A channel as centre and width, given by its two edges in kHz.
static DutyFloor edges(double lo_khz, double hi_khz) {
  return dutyFloor((float)((lo_khz + hi_khz) / 2000.0), (float)(hi_khz - lo_khz));
}

struct BandCase { uint32_t lo, hi; uint16_t bp; };
static const BandCase LISTED[] = {
  {863000, 865000, 10}, {865000, 868000, 100}, {868000, 868600, 100}, {868700, 869200, 10},
  {869400, 869650, 1000}, {869700, 870000, 100}, {433050, 434790, 1000},
};
static const BandCase GAPS[] = {{868600, 868700, 10}, {869200, 869400, 10}, {869650, 869700, 10}};

TEST(RegulatoryDutyCycle, NarrowChannelInsideEachListedBand) {
  for (const BandCase& b : LISTED) {
    DutyFloor f = dutyFloor((b.lo + b.hi) / 2000.0f, 10.0f);
    EXPECT_EQ(f.limit_bp, b.bp) << b.lo;
    EXPECT_EQ(f.band_lo_khz, b.lo);
    EXPECT_EQ(f.band_hi_khz, b.hi);
  }
}

TEST(RegulatoryDutyCycle, UnlistedPartsOfTheBandAreOneTenthPercent) {
  for (const BandCase& b : GAPS) {
    DutyFloor f = dutyFloor((b.lo + b.hi) / 2000.0f, 10.0f);
    EXPECT_EQ(f.limit_bp, 10) << b.lo;
  }
}

TEST(RegulatoryDutyCycle, ChannelFillingABandExactlyIsInsideIt) {
  for (const BandCase& b : LISTED) {
    DutyFloor f = edges(b.lo, b.hi);
    EXPECT_EQ(f.limit_bp, b.bp) << b.lo;
    EXPECT_EQ(f.band_lo_khz, b.lo);
  }
  // the 250 kHz channel of the 10 % band: 869.525 MHz, BW 250
  EXPECT_EQ(dutyFloor(869.525f, 250.0f).limit_bp, 1000);
}

TEST(RegulatoryDutyCycle, ChannelTouchingOnlyAnEdgeIsNotInTheNeighbour) {
  // upper edge exactly at the lower edge of the 10 % band: the channel is in the gap below (0.1 %) only
  EXPECT_EQ(edges(869200, 869400).limit_bp, 10);
  // lower edge exactly at the upper edge of the 10 % band: gap above
  EXPECT_EQ(edges(869650, 869700).limit_bp, 10);
  // 865.0-868.0 next to 863.0-865.0: touching at 865.000 only
  EXPECT_EQ(edges(865000, 868000).limit_bp, 100);
  EXPECT_EQ(edges(863000, 865000).limit_bp, 10);
  // 1 kHz tolerance: 0.5 kHz past the edge still counts as inside
  EXPECT_EQ(edges(869400, 869650.5).limit_bp, 1000);
}

TEST(RegulatoryDutyCycle, StraddlingChannelTakesTheStrictestBand) {
  // 869.55 +/- 125: 869.425-869.675, the 10 % band and the 0.1 % gap above it
  EXPECT_EQ(dutyFloor(869.55f, 250.0f).limit_bp, 10);
  // 5 kHz beyond the 10 % band on either side
  EXPECT_EQ(edges(869395, 869650).limit_bp, 10);
  EXPECT_EQ(edges(869400, 869655).limit_bp, 10);
  // 868.0-868.6 (1 %) and 868.6-868.7 gap (0.1 %)
  EXPECT_EQ(edges(868500, 868650).limit_bp, 10);
  // 865-868 (1 %) and 868-868.6 (1 %): both 1 %
  EXPECT_EQ(edges(867900, 868100).limit_bp, 100);
  // 864.9-865.1: 863-865 (0.1 %) and 865-868 (1 %)
  EXPECT_EQ(edges(864900, 865100).limit_bp, 10);
  // wide channel across several: 869.0-869.8 touches 0.1 %, 10 %, 0.1 %, 1 %
  EXPECT_EQ(edges(869000, 869800).limit_bp, 10);
  // 868.1 MHz, BW 125: 868.0375-868.1625 inside 868.0-868.6
  EXPECT_EQ(dutyFloor(868.1f, 125.0f).limit_bp, 100);
  // the same centre at BW 250 reaches down to 867.975: still 1 % on both sides
  EXPECT_EQ(dutyFloor(868.1f, 250.0f).limit_bp, 100);
}

TEST(RegulatoryDutyCycle, ChannelLeavingTheTabulatedRangeKeepsTheInsidePart) {
  // 869.825-870.075: only 869.7-870.0 is tabulated
  EXPECT_EQ(dutyFloor(869.95f, 250.0f).limit_bp, 100);
  // 862.9-863.1 reaches into 863.0-865.0
  EXPECT_EQ(edges(862900, 863100).limit_bp, 10);
  // 433.0-433.1 reaches into 433.05-434.79
  EXPECT_EQ(edges(433000, 433100).limit_bp, 1000);
  EXPECT_EQ(dutyFloor(434.7f, 250.0f).limit_bp, 1000);
}

TEST(RegulatoryDutyCycle, OutsideTheRangesThereIsNoFloor) {
  const float freqs[] = {150.0f, 432.0f, 435.0f, 862.0f, 870.5f, 902.0f, 915.0f, 928.0f, 2400.0f};
  for (float f : freqs) {
    DutyFloor d = dutyFloor(f, 125.0f);
    EXPECT_EQ(d.limit_bp, REG_DUTY_NONE_BP) << f;
    EXPECT_EQ(d.band_lo_khz, 0u);
    EXPECT_EQ(d.band_hi_khz, 0u);
  }
  // 433.0 with a narrow channel ends at 433.0125: just short of 433.05
  EXPECT_EQ(dutyFloor(433.0f, 25.0f).limit_bp, REG_DUTY_NONE_BP);
  // channel entirely below / above the 863-870 range
  EXPECT_EQ(edges(860000, 862990).limit_bp, REG_DUTY_NONE_BP);
  EXPECT_EQ(edges(870000, 871000).limit_bp, REG_DUTY_NONE_BP);
}

TEST(RegulatoryDutyCycle, BadInputIsNoFloorAndNeverCrashes) {
  EXPECT_EQ(dutyFloor(0.0f, 125.0f).limit_bp, REG_DUTY_NONE_BP);
  EXPECT_EQ(dutyFloor(-868.1f, 125.0f).limit_bp, REG_DUTY_NONE_BP);
  EXPECT_EQ(dutyFloor(NAN, 125.0f).limit_bp, REG_DUTY_NONE_BP);
  // a missing bandwidth still occupies a few kHz
  EXPECT_EQ(dutyFloor(869.525f, 0.0f).limit_bp, 1000);
  EXPECT_EQ(dutyFloor(868.1f, -5.0f).limit_bp, 100);
  EXPECT_EQ(dutyFloor(868.1f, NAN).limit_bp, 100);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
