// SHA-256 (FIPS 180-4) and HMAC-SHA-256 (RFC 4231) for the local unlock challenge (docs/contract/AUTHORITY.md section 10).
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/deployment/DepHmac.h"

using namespace Dep;

static std::string hexOf(const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 15]; }
  return s;
}

static std::string sha(const std::string& m) {
  uint8_t out[32];
  sha256(reinterpret_cast<const uint8_t*>(m.data()), m.size(), out);
  return hexOf(out, 32);
}

static std::string mac(const std::vector<uint8_t>& key, const std::string& m) {
  uint8_t out[32];
  hmacSha256(key.data(), key.size(), reinterpret_cast<const uint8_t*>(m.data()), m.size(), out);
  return hexOf(out, 32);
}

TEST(Sha256, KnownAnswers) {
  EXPECT_EQ(sha(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(sha("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),   // two blocks
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(sha(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, EveryPaddingBoundary) {
  // 55, 56, 63, 64 and 65 bytes straddle the length field; compared with the one-shot value of a message built in pieces.
  for (size_t n : {55u, 56u, 63u, 64u, 65u, 119u, 120u}) {
    std::string m(n, 'x');
    EXPECT_EQ(sha(m).size(), 64u);
    std::string other = m;
    other[n - 1] = 'y';
    EXPECT_NE(sha(m), sha(other)) << n;
  }
}

// RFC 4231 section 4: test cases 1, 2, 3, 4, 6 and 7 (5 is a truncated output).
TEST(HmacSha256, Rfc4231Vectors) {
  EXPECT_EQ(mac(std::vector<uint8_t>(20, 0x0b), "Hi There"),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  EXPECT_EQ(mac({ 'J', 'e', 'f', 'e' }, "what do ya want for nothing?"),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  EXPECT_EQ(mac(std::vector<uint8_t>(20, 0xaa), std::string(50, '\xdd')),
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
  std::vector<uint8_t> k4;
  for (uint8_t i = 1; i <= 25; i++) k4.push_back(i);
  EXPECT_EQ(mac(k4, std::string(50, '\xcd')), "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
  EXPECT_EQ(mac(std::vector<uint8_t>(131, 0xaa), "Test Using Larger Than Block-Size Key - Hash Key First"),
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
  EXPECT_EQ(mac(std::vector<uint8_t>(131, 0xaa),
                "This is a test using a larger than block-size key and a larger than block-size data. The key needs to "
                "be hashed before being used by the HMAC algorithm."),
            "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

// The unlock's own shape: a 32-byte secret keys a 32-byte challenge. Another secret, or another challenge, is another MAC.
TEST(HmacSha256, SecretAndChallengeBothMatter) {
  std::vector<uint8_t> s(32, 0x11), s2 = s;
  s2[31] ^= 1;
  const std::string c(32, '\x22');
  std::string c2 = c;
  c2[0] ^= 1;
  EXPECT_NE(mac(s, c), mac(s2, c));
  EXPECT_NE(mac(s, c), mac(s, c2));
  EXPECT_EQ(mac(s, c), mac(s, c));
}

TEST(ConstTimeEqual, ComparesEveryByte) {
  uint8_t a[32], b[32];
  memset(a, 7, sizeof(a));
  memcpy(b, a, sizeof(a));
  EXPECT_TRUE(constTimeEqual(a, b, 32));
  for (int i = 0; i < 32; i++) {
    b[i] ^= 0x80;
    EXPECT_FALSE(constTimeEqual(a, b, 32)) << i;
    b[i] ^= 0x80;
  }
  EXPECT_TRUE(constTimeEqual(a, b, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
