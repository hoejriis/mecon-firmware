#include <gtest/gtest.h>
#include <vector>
#include "helpers/iface/PacketTransmit.h"

using namespace Iface;

// A native packet as Packet::writeTo writes it, so the check is held to MeshCore's own encoding.
static std::vector<uint8_t> native(uint8_t route, uint8_t type, const std::vector<uint8_t>& path, size_t payload_len,
                                   uint8_t hash_size = 1) {
  mesh::Packet p;
  memset(&p, 0, sizeof(p));
  p.header = (uint8_t)(route | (type << PH_TYPE_SHIFT));
  p.transport_codes[0] = 0x1234; p.transport_codes[1] = 0xABCD;
  p.setPathHashSizeAndCount(hash_size, (uint8_t)(path.size() / hash_size));
  memcpy(p.path, path.data(), path.size());
  p.payload_len = (uint16_t)payload_len;
  for (size_t i = 0; i < payload_len; i++) p.payload[i] = (uint8_t)(i * 7 + 1);
  uint8_t out[MAX_TRANS_UNIT + 1];
  const uint8_t n = p.writeTo(out);
  return std::vector<uint8_t>(out, out + n);
}

TEST(PacketTransmit, ANativePacketOfEachAllowedTypeIsTakenAndRebuiltByteForByte) {
  for (uint8_t type : {PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_GRP_TXT,
                       PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_ANON_REQ, PAYLOAD_TYPE_PATH}) {
    for (uint8_t route : {ROUTE_TYPE_FLOOD, ROUTE_TYPE_DIRECT, ROUTE_TYPE_TRANSPORT_FLOOD, ROUTE_TYPE_TRANSPORT_DIRECT}) {
      const auto b = native(route, type, {0x11, 0x22, 0x33}, 40);
      RawPacketInfo info;
      mesh::Packet p;
      ASSERT_EQ(nullptr, checkRawPacket(b.data(), b.size(), &info, &p)) << "type " << (int)type << " route " << (int)route;
      EXPECT_EQ(type, info.type);
      EXPECT_EQ(route, info.route);
      EXPECT_EQ(40u, info.payload_len);
      uint8_t again[MAX_TRANS_UNIT + 1];
      const uint8_t n = p.writeTo(again);
      ASSERT_EQ(b.size(), (size_t)n) << "sent as supplied: no hop added, none removed";
      EXPECT_EQ(0, memcmp(b.data(), again, n));
    }
  }
}

TEST(PacketTransmit, TheLargestPacketFitsAndOneBytePastThePayloadBoundDoesNot) {
  std::vector<uint8_t> path(MAX_PATH_SIZE, 0x5A);
  const auto biggest = native(ROUTE_TYPE_TRANSPORT_DIRECT, PAYLOAD_TYPE_TXT_MSG, path, MAX_PACKET_PAYLOAD, 2);   // 32 two-byte hashes: a count is 6 bits
  EXPECT_EQ(254u, biggest.size());
  EXPECT_EQ(nullptr, checkRawPacket(biggest.data(), biggest.size(), nullptr));
  auto over = native(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, {}, MAX_PACKET_PAYLOAD);
  over.push_back(0);
  EXPECT_STREQ("malformed", checkRawPacket(over.data(), over.size(), nullptr));
}

TEST(PacketTransmit, ShortOrInconsistentBytesAreMalformedAndNothingIsReadPastThem) {
  const auto ok = native(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, {1, 2, 3, 4}, 10);
  // every truncation, including inside the transport codes and the path, which readFrom would read past
  for (size_t n = 0; n < ok.size() - 10; n++)
    EXPECT_STREQ("malformed", checkRawPacket(ok.data(), n, nullptr)) << "truncated to " << n;
  EXPECT_STREQ("malformed", checkRawPacket(nullptr, 5, nullptr));
  auto v2 = native(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, {}, 10);
  v2[0] |= (1 << PH_VER_SHIFT);
  EXPECT_STREQ("malformed", checkRawPacket(v2.data(), v2.size(), nullptr)) << "a payload version this firmware cannot read";
  auto bad_path = native(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, {}, 10);
  bad_path[1] = 0xC1;   // hash size 4: no valid path
  EXPECT_STREQ("malformed", checkRawPacket(bad_path.data(), bad_path.size(), nullptr));
  const auto no_payload = native(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, {9}, 0);
  EXPECT_STREQ("malformed", checkRawPacket(no_payload.data(), no_payload.size(), nullptr));
}

TEST(PacketTransmit, AdvertsTracesAndOtherTypesAreRefused) {
  for (int type : {PAYLOAD_TYPE_ADVERT, PAYLOAD_TYPE_TRACE, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
                       PAYLOAD_TYPE_RAW_CUSTOM, 0x0C}) {
    const auto b = native(ROUTE_TYPE_FLOOD, (uint8_t)type, {}, 20);
    EXPECT_STREQ("unsupported-type", checkRawPacket(b.data(), b.size(), nullptr)) << "type " << (int)type;
  }
}

TEST(PacketTransmit, TwoByteHashPathsAreTakenWhole) {
  const auto b = native(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_TXT_MSG, {1, 2, 3, 4, 5, 6}, 12, 2);
  mesh::Packet p;
  ASSERT_EQ(nullptr, checkRawPacket(b.data(), b.size(), nullptr, &p));
  EXPECT_EQ(3, p.getPathHashCount());
  EXPECT_EQ(2, p.getPathHashSize());
  EXPECT_EQ(12, p.payload_len);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
