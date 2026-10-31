// MQTT over WebSocket, the parts that need no network (src/helpers/mqtt/MqttWs.h): SHA-1 and base64 against published
// vectors, the handshake against RFC 6455's own example, the frame header, and the decoder's acceptance rules.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/mqtt/MqttWs.h"

using namespace MqttWs;

static std::string hex(const uint8_t* d, size_t n) {
  static const char H[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += H[d[i] >> 4]; s += H[d[i] & 15]; }
  return s;
}

static std::string sha1Of(const std::string& s) {
  uint8_t d[20];
  sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size(), d);
  return hex(d, 20);
}

TEST(MqttWsSha1, PublishedVectors) {
  EXPECT_EQ("da39a3ee5e6b4b0d3255bfef95601890afd80709", sha1Of(""));
  EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", sha1Of("abc"));
  EXPECT_EQ("84983e441c3bd26ebaae4aa1f95129e5e54670f1", sha1Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  EXPECT_EQ("34aa973cd4c4daa4f61eeb2bdbad27316534016f", sha1Of(std::string(1000000, 'a')));
  EXPECT_EQ("c1c8bbdc22796e28c0e15163d20899b65621d65a", sha1Of(std::string(55, 'a')));   // the padding fits one block
  EXPECT_EQ("c2db330f6083854c99d4b5bfb6e8f29f201be699", sha1Of(std::string(56, 'a')));   // it needs a second
}

TEST(MqttWsBase64, Rfc4648Vectors) {
  const char* in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* out[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (int i = 0; i < 7; i++) {
    char b[16];
    EXPECT_EQ(strlen(out[i]), base64(reinterpret_cast<const uint8_t*>(in[i]), strlen(in[i]), b));
    EXPECT_STREQ(out[i], b);
  }
}

TEST(MqttWsHandshake, TheRfcExampleKeyAndAccept) {
  const uint8_t raw[16] = {'t', 'h', 'e', ' ', 's', 'a', 'm', 'p', 'l', 'e', ' ', 'n', 'o', 'n', 'c', 'e'};
  char key[25], acc[29];
  keyText(raw, key);
  EXPECT_STREQ("dGhlIHNhbXBsZSBub25jZQ==", key);
  acceptFor(key, acc);
  EXPECT_STREQ("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", acc);
}

TEST(MqttWsHandshake, TheRequestNamesThePortOnlyWhenItIsNotTheDefault) {
  char b[400];
  ASSERT_GT(request(b, sizeof(b), "broker.example.test", 443, true, "/mqtt", "dGhlIHNhbXBsZSBub25jZQ=="), 0u);
  EXPECT_EQ(std::string("GET /mqtt HTTP/1.1\r\nHost: broker.example.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                        "Sec-WebSocket-Protocol: mqtt\r\n\r\n"), b);
  request(b, sizeof(b), "10.0.0.2", 8080, false, "/", "k");
  EXPECT_NE(nullptr, strstr(b, "Host: 10.0.0.2:8080\r\n"));
  request(b, sizeof(b), "h", 80, false, "/", "k");
  EXPECT_NE(nullptr, strstr(b, "Host: h\r\n"));
  request(b, sizeof(b), "h", 80, true, "/", "k");
  EXPECT_NE(nullptr, strstr(b, "Host: h:80\r\n"));
  EXPECT_EQ(0u, request(b, 40, "h", 80, false, "/", "k")) << "does not fit";
}

static const char KEY[] = "dGhlIHNhbXBsZSBub25jZQ==";

static Answer answer(const std::string& headers, const std::string& status = "HTTP/1.1 101 Switching Protocols") {
  const std::string t = status + "\r\n" + headers + "\r\n";
  return checkAnswer(t.data(), t.size(), KEY);
}

static const std::string GOOD = "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                                "Sec-WebSocket-Protocol: mqtt\r\n";

TEST(MqttWsHandshake, AGoodAnswer) {
  EXPECT_EQ(Answer::Ok, answer(GOOD));
  EXPECT_EQ(Answer::Ok, answer("UPGRADE:WebSocket\r\nconnection: keep-alive, upgrade \r\nsec-websocket-accept:  s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                               "Sec-WebSocket-Protocol: mqtt\r\nServer: x\r\n"))
      << "names and the Upgrade and Connection values without case, whitespace around values";
  EXPECT_EQ(Answer::Ok, answer(GOOD, "HTTP/1.1 101"));
}

TEST(MqttWsHandshake, EveryWayAnAnswerFails) {
  EXPECT_EQ(Answer::Status, answer(GOOD, "HTTP/1.1 200 OK"));
  EXPECT_EQ(Answer::Status, answer(GOOD, "HTTP/1.1 1010"));
  EXPECT_EQ(Answer::Status, answer(GOOD, "HTTP/1.0 101 Switching Protocols"));
  EXPECT_EQ(Answer::Upgrade, answer("Connection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: mqtt\r\n"));
  EXPECT_EQ(Answer::Upgrade, answer("Upgrade: h2c\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: mqtt\r\n"));
  EXPECT_EQ(Answer::Upgrade, answer("Upgrade: websocket\r\nConnection: upgraded\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: mqtt\r\n"));
  EXPECT_EQ(Answer::Accept, answer("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOx=\r\nSec-WebSocket-Protocol: mqtt\r\n"));
  EXPECT_EQ(Answer::Accept, answer("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: mqtt\r\n"));
  EXPECT_EQ(Answer::Protocol, answer("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
  EXPECT_EQ(Answer::Protocol, answer("Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\nSec-WebSocket-Protocol: MQTT\r\n"));
  EXPECT_EQ(Answer::Extension, answer(GOOD + "Sec-WebSocket-Extensions: permessage-deflate\r\n"));
  EXPECT_EQ(Answer::Malformed, answer(GOOD + "no colon here\r\n"));
  const std::string cut = "HTTP/1.1 101 Switching Protocols\r\n" + GOOD;   // no empty line
  EXPECT_EQ(Answer::Malformed, checkAnswer(cut.data(), cut.size(), KEY));
}

TEST(MqttWsFrame, TheHeaderForEachLengthForm) {
  const uint8_t m[4] = {1, 2, 3, 4};
  uint8_t h[14];
  ASSERT_EQ(6u, frameHeader(h, OP_BINARY, 0, m));
  EXPECT_EQ("82800102" "0304", hex(h, 6));
  ASSERT_EQ(6u, frameHeader(h, OP_BINARY, 125, m));
  EXPECT_EQ("82fd01020304", hex(h, 6));
  ASSERT_EQ(8u, frameHeader(h, OP_BINARY, 126, m));
  EXPECT_EQ("82fe007e01020304", hex(h, 8));
  ASSERT_EQ(8u, frameHeader(h, OP_PONG, 65535, m));
  EXPECT_EQ("8afeffff01020304", hex(h, 8));
  ASSERT_EQ(14u, frameHeader(h, OP_BINARY, 65536, m));
  EXPECT_EQ("82ff000000000001000001020304", hex(h, 14));
}

// Feeds bytes; returns the data bytes delivered and the last non-More, non-Data event (More if none).
static Decoder::Event feed(Decoder& d, const std::vector<uint8_t>& in, std::string* data) {
  Decoder::Event last = Decoder::More;
  for (uint8_t b : in) {
    const Decoder::Event e = d.push(b);
    if (e == Decoder::Data) { if (data) *data += (char)b; }
    else if (e != Decoder::More) last = e;
  }
  return last;
}

TEST(MqttWsDecoder, BinaryFramesWholeAndFragmented) {
  Decoder d;
  std::string got;
  EXPECT_EQ(Decoder::More, feed(d, {0x82, 0x03, 'a', 'b', 'c'}, &got));
  EXPECT_EQ(Decoder::More, feed(d, {0x02, 0x02, 'd', 'e', 0x00, 0x00, 0x80, 0x01, 'f'}, &got)) << "begin, empty middle, end";
  EXPECT_EQ(Decoder::More, feed(d, {0x82, 0x00}, &got)) << "an empty message";
  EXPECT_EQ("abcdef", got);
  EXPECT_FALSE(d.failed());
}

TEST(MqttWsDecoder, ExtendedLengths) {
  Decoder d;
  std::vector<uint8_t> f = {0x82, 126, 0x01, 0x2c};
  f.insert(f.end(), 300, 'x');
  f.insert(f.end(), {0x82, 127, 0, 0, 0, 0, 0, 1, 0, 0});
  f.insert(f.end(), 65536, 'y');
  std::string got;
  feed(d, f, &got);
  EXPECT_EQ(std::string(300, 'x') + std::string(65536, 'y'), got);
  EXPECT_FALSE(d.failed());
}

TEST(MqttWsDecoder, ControlFramesBetweenFragments) {
  Decoder d;
  std::string got;
  feed(d, {0x02, 0x01, 'a'}, &got);
  EXPECT_EQ(Decoder::Ping, feed(d, {0x89, 0x02, 'h', 'i'}, &got));
  EXPECT_EQ(2, d.ctrlLen());
  EXPECT_EQ(0, memcmp(d.ctrl(), "hi", 2));
  EXPECT_EQ(Decoder::Pong, feed(d, {0x8a, 0x00}, &got));
  EXPECT_EQ(0, d.ctrlLen());
  feed(d, {0x80, 0x01, 'b'}, &got);
  EXPECT_EQ("ab", got);
  EXPECT_EQ(Decoder::Close, feed(d, {0x88, 0x02, 0x03, 0xe8}, &got));
  EXPECT_EQ(2, d.ctrlLen());
}

TEST(MqttWsDecoder, EveryRefusedFrame) {
  const std::vector<std::vector<uint8_t>> bad = {
    {0x81, 0x01, 'a'},                // text
    {0x82, 0x81, 1, 2, 3, 4, 'a'},    // masked
    {0xc2, 0x01, 'a'},                // RSV1
    {0x92, 0x01, 'a'},                // RSV3
    {0x83, 0x01, 'a'},                // a reserved data opcode
    {0x8b, 0x00},                     // a reserved control opcode
    {0x09, 0x00},                     // a fragmented ping
    {0x89, 0x7e, 0x00, 0x7e},         // a control frame over 125 bytes
    {0x80, 0x01, 'a'},                // a continuation of nothing
    {0x02, 0x01, 'a', 0x82, 0x01, 'b'},   // a new message inside a fragmented one
    {0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 0},   // the 64-bit length's top bit
  };
  for (size_t i = 0; i < bad.size(); i++) {
    Decoder d;
    EXPECT_EQ(Decoder::Error, feed(d, bad[i], nullptr)) << i;
    EXPECT_TRUE(d.failed()) << i;
    EXPECT_EQ(Decoder::Error, d.push(0x82)) << "an error is final, " << i;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
