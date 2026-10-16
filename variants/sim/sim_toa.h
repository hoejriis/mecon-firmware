// LoRa time-on-air per the Semtech SX127x/SX126x datasheet formula
// (explicit header, CRC on). Returns microseconds.
#pragma once
#include <stdint.h>
#include <math.h>

// sf 5..12, bw in kHz, cr as 5..8 (denominator of 4/x), preamble in symbols.
inline double sim_toa_us(int sf, double bw_khz, int cr, int preamble_syms, int payload_bytes) {
  const double tsym_ms = (double)(1u << sf) / bw_khz;
  const int de = tsym_ms > 16.0 ? 1 : 0;            // low data-rate optimisation
  const int crc = 1, ih = 0;
  const double pre_const = (sf == 5 || sf == 6) ? 6.25 : 4.25;
  const double t_pre = (preamble_syms + pre_const) * tsym_ms;
  double num = 8.0 * payload_bytes - 4.0 * sf + 28.0 + 16.0 * crc - 20.0 * ih;
  double blocks = ceil(num / (4.0 * (sf - 2 * de)));
  if (blocks < 0) blocks = 0;
  const double payload_syms = 8.0 + blocks * cr;     // cr here is 5..8 == (CR index + 4)
  return (t_pre + payload_syms * tsym_ms) * 1000.0;
}
