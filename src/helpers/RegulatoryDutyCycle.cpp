#include "RegulatoryDutyCycle.h"

namespace regulatory {

namespace {

struct Band {
  uint32_t lo_khz, hi_khz;
  uint16_t limit_bp;
};

// Sub-bands of 863-870 MHz in order, with the unlisted gaps (0.1 %) written out so that the whole range
// is covered, plus the 433 MHz band.
const Band BANDS[] = {
  {863000, 865000, 10},     // 863.0-865.0     0.1 %
  {865000, 868000, 100},    // 865.0-868.0     1 %
  {868000, 868600, 100},    // 868.0-868.6     1 %
  {868600, 868700, 10},     // gap             0.1 %
  {868700, 869200, 10},     // 868.7-869.2     0.1 %
  {869200, 869400, 10},     // gap             0.1 %
  {869400, 869650, 1000},   // 869.4-869.65    10 %
  {869650, 869700, 10},     // gap             0.1 %
  {869700, 870000, 100},    // 869.7-870.0     1 %
  {433050, 434790, 1000},   // 433.05-434.79   10 %
};

const double EDGE_TOLERANCE_KHZ = 1.0;
const double MIN_WIDTH_KHZ = 3.0;     // a missing or absurd bandwidth still occupies some spectrum

}

DutyFloor dutyFloor(float freq_mhz, float bw_khz) {
  DutyFloor out = {REG_DUTY_NONE_BP, 0, 0};
  if (!(freq_mhz > 0.0f)) return out;      // also rejects NaN

  double bw = (bw_khz >= MIN_WIDTH_KHZ) ? (double)bw_khz : MIN_WIDTH_KHZ;
  double lo = (double)freq_mhz * 1000.0 - bw / 2.0;
  double hi = (double)freq_mhz * 1000.0 + bw / 2.0;

  for (unsigned i = 0; i < sizeof(BANDS) / sizeof(BANDS[0]); i++) {
    const Band& b = BANDS[i];
    double ov_lo = lo > (double)b.lo_khz ? lo : (double)b.lo_khz;
    double ov_hi = hi < (double)b.hi_khz ? hi : (double)b.hi_khz;
    if (ov_hi - ov_lo > EDGE_TOLERANCE_KHZ && b.limit_bp < out.limit_bp) {    // touched, and stricter
      out.limit_bp = b.limit_bp;
      out.band_lo_khz = b.lo_khz;
      out.band_hi_khz = b.hi_khz;
    }
  }
  return out;
}

}
