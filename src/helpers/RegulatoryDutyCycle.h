#pragma once

#include <stdint.h>

/**
 * Regulatory duty-cycle floor for the EU 863-870 MHz SRD band and the 433 MHz band
 * (ERC/REC 70-03 annex 1, EN 300 220).  Dependency-free: no Arduino, no Dispatcher.
 *
 * The occupied channel is centre +/- bandwidth/2.  When it lies inside one sub-band that band's
 * limit applies; when it touches several, the strictest limit among them applies.  Anything in
 * 863-870 MHz that no sub-band lists (868.6-868.7, 869.2-869.4, 869.65-869.7) is limited to 0.1 %.
 * Frequencies outside the tabulated ranges carry no floor (e.g. 902-928 MHz, which has dwell-time
 * rules instead).
 *
 * Limits are in basis points of an hour (100 = 1.00 %, 1000 = 10.00 %).  Band edges are compared with
 * a 1 kHz tolerance, so a 250 kHz channel centred on 869.525 MHz (float rounding included) still counts
 * as inside 869.400-869.650, and a channel that only touches a neighbouring band's edge is not in it.
 */
namespace regulatory {

#define REG_DUTY_NONE_BP 10000    // 100 %: no regulatory floor applies

struct DutyFloor {
  uint16_t limit_bp;     // allowed share of any hour on the air, in 0.01 %; REG_DUTY_NONE_BP when no floor
  uint32_t band_lo_khz;  // edges of the strictest band the channel touches; 0/0 when no floor
  uint32_t band_hi_khz;
};

/** Floor for a channel centred on freq_mhz with width bw_khz. Never fails; an unknown frequency is "none". */
DutyFloor dutyFloor(float freq_mhz, float bw_khz);

}
