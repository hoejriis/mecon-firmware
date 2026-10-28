// The public Deployment authority vectors (tools/mecon_auth_vectors.py) verified with the firmware's own Ed25519, so
// the generator and the device agree on the signed bytes, not only on the prose.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "ed_25519.h"
#include "vectors.h"

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> out;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    out.push_back((uint8_t)(nib(s[i]) << 4 | nib(s[i + 1])));
  }
  return out;
}

static std::vector<uint8_t> unb64(const std::string& s) {
  static const std::string A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<uint8_t> out;
  uint32_t acc = 0;
  int bits = 0;
  for (char c : s) {
    if (c == '=') break;
    acc = acc << 6 | (uint32_t)A.find(c);
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(acc >> bits)); }
  }
  return out;
}

// The value of "key":"..." in a flat JSON text (the vectors never escape quotes inside these values).
static std::string field(const std::string& json, const char* key) {
  std::string k = std::string("\"") + key + "\":\"";
  size_t a = json.find(k);
  if (a == std::string::npos) return "";
  a += k.size();
  return json.substr(a, json.find('"', a) - a);
}

static bool verify(const char* sig_b64, const char* signed_hex, const char* pub_hex) {
  auto sig = unb64(sig_b64), msg = hex(signed_hex), pub = hex(pub_hex);
  if (sig.size() != 64 || pub.size() != 32) return false;
  return ed25519_verify(sig.data(), msg.data(), msg.size(), pub.data()) == 1;
}

TEST(DeploymentVectors, SeedsDeriveThePublishedPublicKeys) {
  const char* seeds[] = {SEED_DAK_EPOCH_1, SEED_DAK_EPOCH_2, SEED_INSTANCE_A, SEED_INSTANCE_B, SEED_DEVICE_NODE};
  const char* pubs[] = {PUB_DAK_EPOCH_1, PUB_DAK_EPOCH_2, PUB_INSTANCE_A, PUB_INSTANCE_B, PUB_DEVICE_NODE};
  for (int i = 0; i < 5; i++) {
    uint8_t pub[32], prv[64];
    ed25519_create_keypair(pub, prv, hex(seeds[i]).data());
    EXPECT_EQ(hex(pubs[i]), std::vector<uint8_t>(pub, pub + 32)) << i;
  }
}

TEST(DeploymentVectors, AuthoritySetsAndRotationVerifyAgainstTheirDak) {
  EXPECT_TRUE(verify(AUTHORITY_SET_REV1_SIG, AUTHORITY_SET_REV1_SIGNED_HEX, PUB_DAK_EPOCH_1));
  EXPECT_TRUE(verify(ROTATION_1_TO_2_SIG, ROTATION_1_TO_2_SIGNED_HEX, PUB_DAK_EPOCH_1));
  EXPECT_TRUE(verify(AUTHORITY_SET_REV2_SIG, AUTHORITY_SET_REV2_SIGNED_HEX, PUB_DAK_EPOCH_2));
  // Revision 2 is signed by the rotated key, so the old one must not verify it.
  EXPECT_FALSE(verify(AUTHORITY_SET_REV2_SIG, AUTHORITY_SET_REV2_SIGNED_HEX, PUB_DAK_EPOCH_1));
}

TEST(DeploymentVectors, SignedBytesCarryTheDomainPrefix) {
  auto b = hex(AUTHORITY_SET_REV1_SIGNED_HEX);
  const char d[] = "MECON-AUTHSET-v1";
  ASSERT_GT(b.size(), sizeof(d));
  EXPECT_EQ(0, memcmp(b.data(), d, sizeof(d) - 1));
  EXPECT_EQ(0, b[sizeof(d) - 1]);  // the 0x00 separator
}

TEST(DeploymentVectors, IfaceRequestWrapperVerifiesAndBindsItsSuffix) {
  std::string wire = IFACE_REQUEST_WIRE;
  std::string sig = field(wire, "sig");
  EXPECT_TRUE(verify(sig.c_str(), IFACE_REQUEST_SIGNED_HEX, PUB_INSTANCE_B));
  EXPECT_FALSE(verify(sig.c_str(), IFACE_REQUEST_SIGNED_HEX, PUB_INSTANCE_A));
  // The same body under another suffix is a different message: the signature must not carry over.
  std::string other = IFACE_REQUEST_SIGNED_HEX;
  std::string suffix_hex = "6966616365";  // "iface"
  size_t at = other.find(suffix_hex);
  ASSERT_NE(std::string::npos, at);
  other.replace(at, suffix_hex.size(), "6966616366");  // "ifacf"
  EXPECT_FALSE(verify(sig.c_str(), other.c_str(), PUB_INSTANCE_B));
}

TEST(DeploymentVectors, UplinkSignatureRecoversFromTheWire) {
  std::string wire = UPLINK_HEALTH_WIRE;
  size_t at = wire.rfind(",\"usig\":\"");
  ASSERT_NE(std::string::npos, at);
  std::string sig = wire.substr(at + 9, 88);
  std::string payload = wire.substr(0, at) + "}";
  std::string expect = std::string("MECON-UPLINK-v1") + '\0' + "health" + '\0' + payload;
  auto signed_bytes = hex(UPLINK_HEALTH_SIGNED_HEX);
  EXPECT_EQ(std::vector<uint8_t>(expect.begin(), expect.end()), signed_bytes);
  EXPECT_TRUE(verify(sig.c_str(), UPLINK_HEALTH_SIGNED_HEX, PUB_DEVICE_NODE));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
