// LPP read back into readings (src/helpers/iface/Lpp.h), with bytes as the CayenneLPP library writes them.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/Lpp.h"

using namespace Iface;

static std::string decode(const std::vector<uint8_t>& b, int* n) {
  char out[1024];
  JsonWriter w(out, sizeof(out));
  w.beginArray();
  *n = decodeLpp(b.data(), b.size(), w);
  w.endArray();
  return std::string(out, w.size());
}

TEST(Lpp, TemperatureHumidityPressure) {
  // ch1 temperature 21.5 C (215 = 0x00D7), ch2 humidity 40% (80 halves), ch3 pressure 1013.2 hPa (10132 = 0x2794)
  int n;
  std::string j = decode({1, 103, 0x00, 0xD7, 2, 104, 80, 3, 115, 0x27, 0x94}, &n);
  EXPECT_EQ(3, n);
  EXPECT_EQ("[{\"channel\":1,\"quantity\":\"temperature\",\"value\":21.500,\"unit\":\"C\"},"
            "{\"channel\":2,\"quantity\":\"humidity\",\"value\":40.000,\"unit\":\"%\"},"
            "{\"channel\":3,\"quantity\":\"pressure\",\"value\":1013.200,\"unit\":\"hPa\"}]", j);
}

TEST(Lpp, SignedValues) {
  int n;
  // -5.0 C = -50 = 0xFFCE; altitude -12 m = 0xFFF4
  std::string j = decode({1, 103, 0xFF, 0xCE, 1, 121, 0xFF, 0xF4}, &n);
  EXPECT_EQ(2, n);
  EXPECT_NE(std::string::npos, j.find("\"value\":-5.000")) << j;
  EXPECT_NE(std::string::npos, j.find("\"value\":-12,")) << j;
}

TEST(Lpp, UnsignedValuesStayPositive) {
  int n;
  // voltage 3.70 V = 370 = 0x0172; a large distance 40000.000 m = 0x02625A00 (top byte below 0x80)
  std::string j = decode({1, 116, 0x01, 0x72, 4, 130, 0x02, 0x62, 0x5A, 0x00}, &n);
  EXPECT_EQ(2, n);
  EXPECT_NE(std::string::npos, j.find("\"value\":3.700,\"unit\":\"V\"")) << j;
  EXPECT_NE(std::string::npos, j.find("\"value\":40000.000,\"unit\":\"m\"")) << j;
  // a pressure with its top bit set must not turn negative: 0x8000 = 3276.8 hPa
  j = decode({1, 115, 0x80, 0x00}, &n);
  EXPECT_NE(std::string::npos, j.find("\"value\":3276.800")) << j;
}

TEST(Lpp, GpsIsSkipped) {
  int n;
  std::string j = decode({1, 136, 1, 2, 3, 4, 5, 6, 7, 8, 9, 2, 103, 0x00, 0x64}, &n);
  EXPECT_EQ(1, n);
  EXPECT_EQ(std::string::npos, j.find("gps"));
  EXPECT_NE(std::string::npos, j.find("\"channel\":2")) << j;
}

TEST(Lpp, MalformedStops) {
  int n;
  decode({1, 103, 0x00}, &n);            // short entry
  EXPECT_EQ(-1, n);
  decode({1, 250, 0x00, 0x01}, &n);      // unknown type
  EXPECT_EQ(-1, n);
  decode({}, &n);
  EXPECT_EQ(0, n);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
