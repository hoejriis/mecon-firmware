// The MQTT adapter of the command-line engine (src/helpers/cli/CliMqtt.h, docs/CLI.md): topics, request parsing,
// response framing.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/cli/CliMqtt.h"

using namespace Cli;

// ---- the MQTT framing

namespace {
bool parse(const std::string& s, Mqtt::Request& r) { return Mqtt::parseRequest((const uint8_t*)s.data(), s.size(), r); }
}

TEST(CliMqtt, Topics) {
  char t[40];
  Mqtt::requestTopic(t, sizeof(t), "a1b2c3d4");
  EXPECT_STREQ(t, "mecon/a1b2c3d4/cli/req");
  Mqtt::responseTopic(t, sizeof(t), "a1b2c3d4");
  EXPECT_STREQ(t, "mecon/a1b2c3d4/cli/resp");
}

TEST(CliMqtt, ParsesARequest) {
  Mqtt::Request r;
  ASSERT_TRUE(parse("{\"id\":\"c-1\",\"cmd\":\"get name\"}", r));
  EXPECT_STREQ(r.id, "c-1");
  EXPECT_STREQ(r.cmd, "get name");
  ASSERT_TRUE(parse("  { \"cmd\" : \"ver\" , \"id\" : \"A.b_c:9\" , \"extra\": 12 , \"x\":true }  ", r));   // any order, spaces, other keys
  EXPECT_STREQ(r.id, "A.b_c:9");
  EXPECT_STREQ(r.cmd, "ver");
  ASSERT_TRUE(parse("{\"id\":\"1\",\"cmd\":\"set name \\\"Q\\\" \\\\ \\u00e9\"}", r));   // escapes
  EXPECT_STREQ(r.cmd, "set name \"Q\" \\ \xC3\xA9");
}

TEST(CliMqtt, RejectsWhatIsNotAWellFormedRequest) {
  Mqtt::Request r;
  const char* bad[] = {
    "", "{}", "[]", "null", "{\"id\":\"1\"}", "{\"cmd\":\"ver\"}", "{\"id\":\"\",\"cmd\":\"ver\"}", "{\"id\":\"1\",\"cmd\":\"\"}",
    "{\"id\":\"1\",\"cmd\":\"ver\"} x", "{\"id\":\"1\",\"cmd\":\"ver\"", "{\"id\":\"1\" \"cmd\":\"ver\"}", "{\"id\":1,\"cmd\":\"ver\"}",
    "{\"id\":\"1\",\"id\":\"2\",\"cmd\":\"ver\"}", "{\"id\":\"1\",\"cmd\":\"ver\",\"cmd\":\"reboot\"}",   // ambiguity: duplicate keys
    "{\"id\":\"has space\",\"cmd\":\"ver\"}", "{\"id\":\"a/b\",\"cmd\":\"ver\"}", "{\"id\":\"a#\",\"cmd\":\"ver\"}",
    "{\"id\":\"1\",\"cmd\":\"ver\\nreboot\"}", "{\"id\":\"1\",\"cmd\":\"ver\\u000areboot\"}", "{\"id\":\"1\",\"cmd\":\"a\tb\"}",
    "{\"id\":\"1\",\"cmd\":\"ver\\u0000\"}", "{\"id\":\"1\",\"cmd\":\"\\ud83d\\ude00\"}", "{\"id\":\"1\",\"cmd\":\"ver\",\"o\":{\"a\":1}}",
    "{\"id\":\"1\",\"cmd\":\"ver\",\"o\":[1]}", "{\"id\":\"1\",\"cmd\":\"\\q\"}", "{\"id\":\"1\",\"cmd\":\"ver",
  };
  for (const char* b : bad) EXPECT_FALSE(parse(b, r)) << b;
}

TEST(CliMqtt, LengthLimitsAreExact) {
  Mqtt::Request r;
  std::string id32(Mqtt::ID_MAX, 'a'), id33(Mqtt::ID_MAX + 1, 'a');
  EXPECT_TRUE(parse("{\"id\":\"" + id32 + "\",\"cmd\":\"ver\"}", r));
  EXPECT_FALSE(parse("{\"id\":\"" + id33 + "\",\"cmd\":\"ver\"}", r));
  std::string cmd_ok(Mqtt::CMD_MAX, 'x'), cmd_long(Mqtt::CMD_MAX + 1, 'x');
  EXPECT_TRUE(parse("{\"id\":\"1\",\"cmd\":\"" + cmd_ok + "\"}", r));
  EXPECT_EQ(strlen(r.cmd), (size_t)Mqtt::CMD_MAX);
  EXPECT_FALSE(parse("{\"id\":\"1\",\"cmd\":\"" + cmd_long + "\"}", r));
}

// A deliberately small checker for what formatResponse must produce: one object with the four known fields, strings
// that are properly escaped, nothing after the closing brace.
static bool wellFormedResponse(const std::string& s, std::string* reply_out = nullptr, bool* truncated = nullptr) {
  size_t i = 0;
  auto lit = [&](const char* t) { size_t n = strlen(t); if (s.compare(i, n, t) != 0) return false; i += n; return true; };
  auto str = [&](std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    while (i < s.size()) {
      unsigned char c = s[i++];
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c == '\\') {
        if (i >= s.size()) return false;
        char e = s[i++];
        if (e == 'n') out += '\n';
        else if (e == 'r') out += '\r';
        else if (e == 't') out += '\t';
        else if (e == '"' || e == '\\') out += e;
        else if (e == 'u') { if (i + 4 > s.size()) return false; out += (char)strtol(s.substr(i, 4).c_str(), nullptr, 16); i += 4; }
        else return false;
      } else out += (char)c;
    }
    return false;
  };
  std::string id, status, reply;
  if (!lit("{\"id\":") || !str(id) || !lit(",\"status\":") || !str(status)) return false;
  if (!(lit(",\"ok\":true") || lit(",\"ok\":false"))) return false;
  if (!lit(",\"reply\":") || !str(reply)) return false;
  bool cut = false;
  if (lit(",\"truncated\":true")) cut = true;
  if (!lit("}") || i != s.size()) return false;
  if (reply_out) *reply_out = reply;
  if (truncated) *truncated = cut;
  return true;
}

TEST(CliMqtt, ResponsesAreWellFormedAndCarryTheCorrelationId) {
  char out[512];
  size_t n = Mqtt::formatResponse(out, sizeof(out), "c-1", Mqtt::S_OK, "> Sim");
  EXPECT_STREQ(out, "{\"id\":\"c-1\",\"status\":\"ok\",\"ok\":true,\"reply\":\"> Sim\"}");
  EXPECT_EQ(n, strlen(out));
  Mqtt::formatResponse(out, sizeof(out), "x", Mqtt::S_REFUSED, "ERR: not permitted");
  EXPECT_STREQ(out, "{\"id\":\"x\",\"status\":\"refused\",\"ok\":false,\"reply\":\"ERR: not permitted\"}");
  Mqtt::formatResponse(out, sizeof(out), "", Mqtt::S_BAD_REQUEST, "ERR: bad request");
  EXPECT_NE(strstr(out, "\"status\":\"bad-request\""), nullptr);
  Mqtt::formatResponse(out, sizeof(out), "u", Mqtt::S_UNKNOWN, "Unknown command");
  EXPECT_NE(strstr(out, "\"status\":\"unknown\""), nullptr);
}

TEST(CliMqtt, TheReplyIsEscapedAndRoundTrips) {
  const std::string nasty = "line1\nline2\t\"quoted\" back\\slash \x01 ctrl \x7f del caf\xC3\xA9 \xE2\x82\xAC";
  char out[512];
  Mqtt::formatResponse(out, sizeof(out), "e", Mqtt::S_OK, nasty.c_str());
  std::string reply;
  bool cut = true;
  ASSERT_TRUE(wellFormedResponse(out, &reply, &cut)) << out;
  EXPECT_FALSE(cut);
  EXPECT_EQ(reply, nasty);
}

TEST(CliMqtt, ARepliesTooLongForTheBufferIsCutOnACharacterBoundaryAndSaysSo) {
  std::string reply;
  for (int i = 0; i < 150; i++) reply += (i % 7 == 0) ? "\xE2\x82\xAC" : (i % 5 == 0 ? "\"" : "a");   // quotes double, euro signs are 3 bytes
  for (size_t cap = 80; cap < 700; cap++) {
    std::vector<char> out(cap);
    size_t n = Mqtt::formatResponse(out.data(), cap, "id1", Mqtt::S_OK, reply.c_str());
    ASSERT_LT(n, cap) << cap;
    ASSERT_EQ(strlen(out.data()), n) << cap;
    std::string got;
    bool cut = false;
    ASSERT_TRUE(wellFormedResponse(out.data(), &got, &cut)) << "cap " << cap << ": " << out.data();
    if (cut) {
      EXPECT_LT(got.size(), reply.size()) << cap;
      EXPECT_EQ(reply.compare(0, got.size(), got), 0) << cap;                     // a prefix of the reply
      // no half of a multi-byte character
      if (!got.empty()) EXPECT_NE((unsigned char)got.back() & 0xC0, 0xC0) << cap;
      if (got.size() >= 1) {
        size_t k = got.size();
        while (k > 0 && ((unsigned char)got[k - 1] & 0xC0) == 0x80) k--;
        if (k > 0 && (unsigned char)got[k - 1] >= 0xC0) {
          unsigned char lead = got[k - 1];
          size_t need = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : 2);
          EXPECT_EQ(got.size() - (k - 1), need) << cap;
        }
      }
    } else {
      EXPECT_EQ(got, reply) << cap;
    }
  }
}

TEST(CliMqtt, TheSessionContext) {
  Context v = Mqtt::sessionContext(true, 0, false);
  EXPECT_EQ(principalOf(v), P_MQTT);
  Context u = Mqtt::sessionContext(false, 0, true);
  EXPECT_EQ(principalOf(u), P_MQTT_UNVERIFIED);
  EXPECT_TRUE(u.remote);
  EXPECT_TRUE(Mqtt::sessionContext(true, 1, true).mgmt);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
