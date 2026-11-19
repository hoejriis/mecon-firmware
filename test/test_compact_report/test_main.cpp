#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/iface/CompactReport.h"

using namespace Iface;

static std::vector<uint8_t> encode(uint8_t fields, const ReportData& d) {
  uint8_t out[REPORT_MAX];
  const size_t n = encodeReport(out, sizeof(out), fields, 0x01020304, 0x05060708, 0x090A0B0C, d);
  return std::vector<uint8_t>(out, out + n);
}

static ReportData sample() {
  ReportData d;
  d.uptime_s = 0x11223344; d.heap_free_kb = 0x5566; d.battery_mv = 0x7788;
  d.rx_total = 0x01010101; d.tx_total = 0x02020202; d.err_flags = 3; d.noise_dbm = -100; d.last_rssi = -90; d.last_snr_q = 20;
  d.rx_dup = 0x0A01; d.rx_dropped = 0x0B02; d.rx_unparsable = 0x0C03; d.tx_failed = 0x0D04; d.queue_full = 0x0E05;
  d.cad_retries = 0x0F06; d.duty_holds = 0x1007; d.queue_depth = 9; d.queue_free = 23;
  return d;
}

TEST(CompactReport, HealthAndRadioAreTheBytesTheyAlwaysWere) {
  // A report that does not ask for the counters is the layout of version 1 as first shipped: 15 + 8 + 12 bytes.
  const std::vector<uint8_t> b = encode(RF_HEALTH | RF_RADIO, sample());
  const uint8_t want[] = {0x1E, 1, 3, 4, 3, 2, 1, 8, 7, 6, 5, 0x0C, 0x0B, 0x0A, 9,
                          0x44, 0x33, 0x22, 0x11, 0x66, 0x55, 0x88, 0x77,
                          1, 1, 1, 1, 2, 2, 2, 2, 3, 0x9C, 0xA6, 20};
  ASSERT_EQ(b.size(), sizeof(want));
  EXPECT_EQ(0, memcmp(b.data(), want, sizeof(want)));
}

TEST(CompactReport, TheCountersComeLastInBitOrderAsLittleEndianValues) {
  const std::vector<uint8_t> b = encode(RF_COUNTERS, sample());
  ASSERT_EQ(b.size(), REPORT_HEADER + 16);
  EXPECT_EQ(b[2], RF_COUNTERS);
  const uint8_t want[] = {0x01, 0x0A, 0x02, 0x0B, 0x03, 0x0C, 0x04, 0x0D, 0x05, 0x0E, 0x06, 0x0F, 0x07, 0x10, 9, 23};
  EXPECT_EQ(0, memcmp(b.data() + REPORT_HEADER, want, sizeof(want)));
}

TEST(CompactReport, EveryFieldTogetherFitsWithItsSignatureInWhatADatagramTakes) {
  const std::vector<uint8_t> b = encode(RF_ALL, sample());
  EXPECT_EQ(b.size(), REPORT_HEADER + 8 + 12 + 14 + 16);
  EXPECT_LE(b.size() + REPORT_SIG, REPORT_MAX);
  // the counters are the last 16 bytes: after the location, which is after the radio
  EXPECT_EQ(b[b.size() - 2], 9);
  EXPECT_EQ(b[b.size() - 1], 23);
}

TEST(CompactReport, TheLengthOfTheFieldsFollowsTheBits) {
  EXPECT_EQ(reportFieldsLen(0), 0u);
  EXPECT_EQ(reportFieldsLen(RF_HEALTH), 8u);
  EXPECT_EQ(reportFieldsLen(RF_RADIO), 12u);
  EXPECT_EQ(reportFieldsLen(RF_LOCATION), 14u);
  EXPECT_EQ(reportFieldsLen(RF_COUNTERS), 16u);
  EXPECT_EQ(reportFieldsLen(RF_ALL), 50u);
}

TEST(CompactReport, ABitThatIsNotAFieldIsRefused) {
  uint8_t out[REPORT_MAX];
  EXPECT_EQ(0u, encodeReport(out, sizeof(out), 0x10, 1, 2, 3, ReportData()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
