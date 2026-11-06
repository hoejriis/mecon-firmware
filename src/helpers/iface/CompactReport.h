#pragma once
// The compact report (docs/contract/TELEMETRY.md section "Compact reports over LoRa"): a small binary subset of the telemetry
// producers, signed like every event. Little-endian. Pure logic, for the native tests.
//
//   0x1E | version | fields | boot u32 | seq u32 | time u32 (0: unknown) | the fields, in bit order | signature (64)
//
// A field the board cannot produce is absent from `fields`: never a made-up zero.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Iface {

const uint8_t REPORT_MARKER = 0x1E;
const uint8_t REPORT_VERSION = 1;
const size_t REPORT_HEADER = 15;
const size_t REPORT_SIG = 64;
const size_t REPORT_MAX = 132;   // what an anonymous datagram takes after its tag

enum ReportField : uint8_t { RF_HEALTH = 1, RF_RADIO = 2, RF_LOCATION = 4 };
const uint8_t RF_ALL = RF_HEALTH | RF_RADIO | RF_LOCATION;
const uint8_t RF_PRIVATE = RF_LOCATION;   // what a caller must be allowed to read, and what a channel never carries

const uint16_t REPORT_NONE16 = 0xFFFF;
const int8_t REPORT_NONE8 = 0x7F;

struct ReportData {
  uint32_t uptime_s = 0;
  uint16_t heap_free_kb = REPORT_NONE16, battery_mv = REPORT_NONE16;
  uint32_t rx_total = 0, tx_total = 0;
  uint8_t err_flags = 0;
  int8_t noise_dbm = REPORT_NONE8, last_rssi = REPORT_NONE8, last_snr_q = REPORT_NONE8;   // SNR in quarters of a dB
  bool fix = false;
  int32_t lat_e6 = 0, lon_e6 = 0;
  int16_t alt_m = 0;
  uint8_t sats = 0;
  uint16_t age_s = REPORT_NONE16;
};

inline uint8_t* put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
inline uint8_t* put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); return p + 4; }

// The report without its signature; its length, or 0 when it does not fit with the signature. `fields`: what this board has.
inline size_t encodeReport(uint8_t* out, size_t cap, uint8_t fields, uint32_t boot, uint32_t seq, uint32_t time_s, const ReportData& d) {
  if (cap > REPORT_MAX) cap = REPORT_MAX;
  const size_t need = REPORT_HEADER + ((fields & RF_HEALTH) ? 8 : 0) + ((fields & RF_RADIO) ? 12 : 0) + ((fields & RF_LOCATION) ? 14 : 0) + REPORT_SIG;
  if (need > cap || (fields & ~RF_ALL)) return 0;
  uint8_t* p = out;
  *p++ = REPORT_MARKER; *p++ = REPORT_VERSION; *p++ = fields;
  p = put32(p, boot); p = put32(p, seq); p = put32(p, time_s);
  if (fields & RF_HEALTH) { p = put32(p, d.uptime_s); p = put16(p, d.heap_free_kb); p = put16(p, d.battery_mv); }
  if (fields & RF_RADIO) {
    p = put32(p, d.rx_total); p = put32(p, d.tx_total);
    *p++ = d.err_flags; *p++ = (uint8_t)d.noise_dbm; *p++ = (uint8_t)d.last_rssi; *p++ = (uint8_t)d.last_snr_q;
  }
  if (fields & RF_LOCATION) {
    *p++ = d.fix ? 1 : 0; p = put32(p, (uint32_t)d.lat_e6); p = put32(p, (uint32_t)d.lon_e6);
    p = put16(p, (uint16_t)d.alt_m); *p++ = d.sats; p = put16(p, d.age_s);
  }
  return (size_t)(p - out);
}

}  // namespace Iface
