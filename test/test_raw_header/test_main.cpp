#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include "helpers/iface/RawHeader.h"

using namespace Iface;

// A header byte as MeshCore lays it out: version in bits 6-7, type in bits 2-5, route in bits 0-1.
static uint8_t hdr(uint8_t route, uint8_t type, uint8_t ver = 0) { return (uint8_t)(route | (type << 2) | (ver << 6)); }

TEST(RawHeader, EachRouteAndTypeIsReadFromTheHeaderByte) {
  const char* names[4] = {"tflood", "flood", "direct", "tdirect"};
  for (uint8_t route = 0; route < 4; route++) {
    for (uint8_t type = 0; type < 16; type++) {
      std::vector<uint8_t> f = {hdr(route, type), 0, 0, 0, 0, 0, 0};
      RawHeader h = parseRawHeader(f.data(), (int)f.size());
      EXPECT_EQ(type, h.type);
      EXPECT_STREQ(names[route], h.route);
    }
  }
}

TEST(RawHeader, ThePathLengthSitsAfterTheTransportCodesOnlyForATransportRoute) {
  const uint8_t flood[] = {hdr(1, 4), 3, 0xAA, 0xBB, 0xCC, 0xDD};
  RawHeader h = parseRawHeader(flood, sizeof(flood));
  EXPECT_TRUE(h.path_known);
  EXPECT_EQ(3, h.path_len);
  const uint8_t tflood[] = {hdr(0, 4), 0x11, 0x22, 0x33, 0x44, 5, 1, 2};   // 4 transport bytes, then the path length 5
  h = parseRawHeader(tflood, sizeof(tflood));
  EXPECT_TRUE(h.path_known);
  EXPECT_EQ(5, h.path_len);
  const uint8_t tdirect[] = {hdr(3, 2), 0x11, 0x22, 0x33, 0x44, 2, 9, 9};
  EXPECT_EQ(2, parseRawHeader(tdirect, sizeof(tdirect)).path_len);
}

TEST(RawHeader, TheHashSizeBitsAreNotPartOfTheHopCount) {
  const uint8_t f[] = {hdr(1, 5), (uint8_t)((1 << 6) | 7), 0};   // two-byte hashes, 7 hops
  RawHeader h = parseRawHeader(f, sizeof(f));
  EXPECT_TRUE(h.path_known);
  EXPECT_EQ(7, h.path_len);
}

TEST(RawHeader, AFrameThatEndsBeforeItsPathLengthOrUsesTheReservedModeHasNoPathLength) {
  const uint8_t only[] = {hdr(1, 4)};
  EXPECT_FALSE(parseRawHeader(only, 1).path_known);
  const uint8_t trans[] = {hdr(0, 4), 1, 2, 3, 4};   // ends where the path length byte would be
  EXPECT_FALSE(parseRawHeader(trans, sizeof(trans)).path_known);
  const uint8_t reserved[] = {hdr(1, 4), (uint8_t)(3 << 6) | 2, 0};
  RawHeader h = parseRawHeader(reserved, sizeof(reserved));
  EXPECT_FALSE(h.path_known);
  EXPECT_EQ(0, h.path_len);
  EXPECT_FALSE(parseRawHeader(nullptr, 0).path_known);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
