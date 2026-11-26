// mqtt.profile.anchor in pieces (src/helpers/iface/NetProfileOps.cpp over the CLI's `ca begin|add|end` staging in
// src/helpers/mqtt/MqttProfiles.cpp, docs/contract/OPERATIONS.md section 5.1): pieces in order, a gap or a repeat
// refused with nothing appended, the size and SHA-256 checked at the end, a truncated certificate refused, the in-use
// rule, and the staging buffer freed after ten idle minutes.
#include <gtest/gtest.h>
#include <Arduino.h>
#include <Utils.h>
#include <string>
#include <vector>
#include <string.h>
#include <helpers/iface/IfaceDispatch.h>
#include <helpers/iface/NetProfileOps.h>
#include <helpers/mqtt/MqttProfiles.h>

using namespace Iface;

namespace {

const char* const STATIC_ANCHOR = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

// A syntactically valid DER SEQUENCE of `payload` bytes (not a real certificate: the platform parses the real thing).
std::vector<uint8_t> fakeDer(size_t payload, uint8_t seed = 1) {
  std::vector<uint8_t> der;
  der.push_back(0x30);
  if (payload < 128) der.push_back((uint8_t)payload);
  else if (payload < 256) { der.push_back(0x81); der.push_back((uint8_t)payload); }
  else { der.push_back(0x82); der.push_back((uint8_t)(payload >> 8)); der.push_back((uint8_t)(payload & 255)); }
  for (size_t i = 0; i < payload; i++) der.push_back((uint8_t)(seed + i * 7));
  return der;
}

std::string b64(const std::vector<uint8_t>& der) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < der.size(); i += 3) {
    uint32_t v = der[i] << 16;
    size_t rem = der.size() - i;
    if (rem > 1) v |= der[i + 1] << 8;
    if (rem > 2) v |= der[i + 2];
    out.push_back(T[(v >> 18) & 63]);
    out.push_back(T[(v >> 12) & 63]);
    out.push_back(rem > 1 ? T[(v >> 6) & 63] : '=');
    out.push_back(rem > 2 ? T[v & 63] : '=');
  }
  return out;
}

std::string shaHex(const std::vector<uint8_t>& der) {
  uint8_t h[32];
  mesh::Utils::sha256(h, sizeof(h), der.data(), (int)der.size());
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
  return hex;
}

struct Rig;
bool runCli(void* u, const char* line, char* reply, size_t cap);
bool authAll(void*, const OpEntry&, const Request&) { return true; }

Status opAnchor(OpContext& c);
const OpEntry OPS[] = {{"mqtt.profile.anchor", opAnchor, PRIV_WRITE, true}};

struct Rig {
  MqttProfiles profiles;
  bool cable = false;
  uint8_t in_use = NET_NONE;
  Dispatcher d{OPS, 1, authAll, nullptr, this};
  ResultCache cache;
  int n = 0;
  Rig() { g_mock_millis = 1000; }
  std::string call(const std::string& args) {
    std::string m = "{\"v\":1,\"id\":\"t" + std::to_string(++n) + "\",\"op\":\"mqtt.profile.anchor\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  std::string begin(unsigned profile, size_t size, const std::string& sha) {
    return call("{\"profile\":" + std::to_string(profile) + ",\"ca_begin\":{\"size\":" + std::to_string(size) + ",\"sha256\":\"" + sha + "\"}}");
  }
  std::string add(unsigned profile, size_t offset, const std::string& data) {
    return call("{\"profile\":" + std::to_string(profile) + ",\"ca_add\":{\"offset\":" + std::to_string(offset) + ",\"data\":\"" + data + "\"}}");
  }
  std::string end(unsigned profile) { return call("{\"profile\":" + std::to_string(profile) + ",\"ca_end\":true}"); }
  // The whole certificate in pieces of `piece` characters; returns the answer to the last request.
  std::string send(unsigned profile, const std::vector<uint8_t>& der, size_t piece = 768) {
    const std::string text = b64(der);
    std::string r = begin(profile, der.size(), shaHex(der));
    for (size_t at = 0; at < text.size() && ok(r); at += piece) r = add(profile, at, text.substr(at, piece));
    return ok(r) ? end(profile) : r;
  }
  static bool ok(const std::string& r) { return r.find("\"status\":\"ok\"") != std::string::npos; }
  std::string cli(const char* line) {
    char reply[160];
    MqttProfileChange ch;
    mqttProfileCommand(profiles, nullptr, Posture::NORMAL, line, reply, sizeof(reply), ch);
    return reply;
  }
};

bool runCli(void* u, const char* line, char* reply, size_t cap) {
  Rig* r = static_cast<Rig*>(u);
  MqttProfileChange ch;
  return mqttProfileCommand(r->profiles, nullptr, Posture::NORMAL, line, reply, cap, ch);
}

Status opAnchor(OpContext& c) {
  Rig* r = static_cast<Rig*>(c.user);
  NetProfileHost h{runCli, r, r->cable, 0, MqttProfiles::COUNT, NET_NONE, r->in_use};
  return opMqttAnchor(c, h);
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST(AnchorPieces, PiecesInOrderInstallOneCompleteCertificate) {
  Rig r;
  r.cli("set mqtt.profile 1 type static");
  const std::vector<uint8_t> der = fakeDer(1000);   // 1,004 bytes: 1,340 characters, two pieces of 768
  const std::string text = b64(der);
  std::string a = r.begin(1, der.size(), shaHex(der));
  EXPECT_TRUE(has(a, "\"status\":\"ok\"") && has(a, "\"staged\":0")) << a;
  EXPECT_FALSE(r.profiles.p[1].hasCa()) << "nothing changes at begin";
  a = r.add(1, 0, text.substr(0, 768));
  EXPECT_TRUE(has(a, "\"staged\":768")) << a;
  EXPECT_FALSE(r.profiles.p[1].hasCa()) << "nor at an add";
  a = r.add(1, 768, text.substr(768));
  EXPECT_TRUE(has(a, "\"staged\":" + std::to_string(text.size()))) << a;
  EXPECT_EQ(nullptr, r.profiles.p[1].ca);
  a = r.end(1);
  EXPECT_TRUE(has(a, "\"status\":\"ok\"") && has(a, "\"anchor\":\"ca\"")) << a;
  EXPECT_STREQ(text.c_str(), r.profiles.p[1].ca);
  EXPECT_EQ(nullptr, r.profiles.stage_) << "the staging buffer is freed on end";
}

TEST(AnchorPieces, TheLongestCertificateGoesInInPiecesOfAnySize) {
  for (size_t piece : {size_t(24), size_t(96), size_t(768), size_t(1024)}) {
    Rig r;
    const std::vector<uint8_t> der = fakeDer(1436);   // 1,440 bytes: exactly 1,920 characters
    std::string a = r.send(0, der, piece);
    EXPECT_TRUE(Rig::ok(a)) << piece << " " << a;
    EXPECT_STREQ(b64(der).c_str(), r.profiles.p[0].ca) << piece;
  }
}

TEST(AnchorPieces, ACertificateInPiecesReplacesAFingerprint) {
  Rig r;
  r.cli("set mqtt.profile 0 type static");
  r.cli((std::string("set mqtt.profile 0 anchor ") + STATIC_ANCHOR).c_str());
  const std::vector<uint8_t> der = fakeDer(300);
  EXPECT_TRUE(Rig::ok(r.send(0, der, 100)));
  EXPECT_EQ(0, r.profiles.p[0].anchor[0]) << "a profile has one anchor";
  EXPECT_TRUE(r.profiles.p[0].hasCa());
}

TEST(AnchorPieces, AGapOrARepeatIsRefusedAndNothingIsAppended) {
  Rig r;
  const std::vector<uint8_t> der = fakeDer(500);
  const std::string text = b64(der);
  ASSERT_TRUE(Rig::ok(r.begin(0, der.size(), shaHex(der))));
  ASSERT_TRUE(Rig::ok(r.add(0, 0, text.substr(0, 200))));
  std::string a = r.add(0, 300, text.substr(300, 100));   // a gap: the piece 200..300 never came
  EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << a;
  EXPECT_EQ(200, r.profiles.stage_len_);
  a = r.add(0, 0, text.substr(0, 200));                    // a repeat of the first piece
  EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << a;
  EXPECT_EQ(200, r.profiles.stage_len_);
  a = r.add(0, 100, text.substr(100, 100));                // an overlap
  EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << a;
  EXPECT_EQ(200, r.profiles.stage_len_);
  EXPECT_EQ(0, memcmp(r.profiles.stage_, text.data(), 200)) << "the staged text is what was sent in order";
  ASSERT_TRUE(Rig::ok(r.add(0, 200, text.substr(200)))) << "the stage is still there for the right piece";
  EXPECT_TRUE(Rig::ok(r.end(0)));
  EXPECT_STREQ(text.c_str(), r.profiles.p[0].ca);
}

TEST(AnchorPieces, AnAddMustFollowABeginOfTheSameProfile) {
  Rig r;
  EXPECT_TRUE(has(r.add(0, 0, "AAAA"), "\"status\":\"invalid\""));
  EXPECT_TRUE(has(r.end(0), "\"status\":\"invalid\""));
  ASSERT_TRUE(Rig::ok(r.begin(0, 100, std::string(64, 'a'))));
  EXPECT_TRUE(has(r.add(1, 0, "AAAA"), "\"status\":\"invalid\"")) << "another profile is not the one staged";
  EXPECT_EQ(0, r.profiles.stage_len_);
}

TEST(AnchorPieces, ABadPieceAppendsNothing) {
  Rig r;
  ASSERT_TRUE(Rig::ok(r.begin(0, 300, std::string(64, 'a'))));
  ASSERT_TRUE(Rig::ok(r.add(0, 0, "AAAA")));
  EXPECT_TRUE(has(r.add(0, 4, "AAAA*AAAA"), "\"status\":\"invalid\"")) << "not base64";
  EXPECT_TRUE(has(r.add(0, 4, "AAAA\\nAAAA"), "\"status\":\"invalid\"")) << "an escape";
  EXPECT_TRUE(has(r.add(0, 4, ""), "\"status\":\"invalid\""));
  EXPECT_TRUE(has(r.add(0, 4, std::string(1025, 'A')), "\"status\":\"invalid\"")) << "over the piece bound";
  EXPECT_TRUE(has(r.add(0, 1900, std::string(100, 'A')), "\"status\":\"invalid\"")) << "past 1,920 characters";
  EXPECT_EQ(4, r.profiles.stage_len_);
}

TEST(AnchorPieces, ASizeOrHashMismatchIsRefusedAtEndAndTheProfileKeepsItsAnchor) {
  const std::vector<uint8_t> der = fakeDer(400);
  const std::string good = shaHex(der);
  std::string wrongHash = good;
  wrongHash[10] = wrongHash[10] == '0' ? '1' : '0';
  struct Case { size_t size; std::string sha; const char* what; };
  const Case cases[] = {{der.size() + 1, good, "size one too many"}, {der.size() - 1, good, "size one too few"},
                        {der.size(), wrongHash, "one hex digit of the hash"}, {der.size(), std::string(64, '0'), "an empty hash"}};
  for (const Case& cs : cases) {
    Rig r;
    r.cli("set mqtt.profile 0 type static");
    r.cli((std::string("set mqtt.profile 0 anchor ") + STATIC_ANCHOR).c_str());
    const std::string text = b64(der);
    ASSERT_TRUE(Rig::ok(r.begin(0, cs.size, cs.sha))) << cs.what;
    ASSERT_TRUE(Rig::ok(r.add(0, 0, text))) << cs.what;
    std::string a = r.end(0);
    EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << cs.what << " " << a;
    EXPECT_FALSE(r.profiles.p[0].hasCa()) << cs.what;
    EXPECT_STREQ(STATIC_ANCHOR, r.profiles.p[0].anchor) << cs.what;
    EXPECT_EQ(nullptr, r.profiles.stage_) << cs.what << ": the buffer is freed on a refused end too";
    EXPECT_TRUE(has(r.end(0), "\"status\":\"invalid\"")) << cs.what << ": and a second end finds nothing";
  }
}

TEST(AnchorPieces, ATruncatedCertificateIsRefusedWhateverItsHashSays) {
  const std::vector<uint8_t> der = fakeDer(400);
  // The hash and size of the whole certificate, the text cut short.
  {
    Rig r;
    ASSERT_TRUE(Rig::ok(r.begin(0, der.size(), shaHex(der))));
    ASSERT_TRUE(Rig::ok(r.add(0, 0, b64(der).substr(0, 400))));
    EXPECT_TRUE(has(r.end(0), "\"status\":\"invalid\""));
    EXPECT_FALSE(r.profiles.p[0].hasCa());
  }
  // The size and hash of the cut-short bytes themselves: they match, and the certificate is still not whole.
  {
    Rig r;
    const std::vector<uint8_t> cut(der.begin(), der.begin() + 300);
    std::string a = r.send(0, cut);
    EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << a;
    EXPECT_FALSE(r.profiles.p[0].hasCa());
    EXPECT_EQ(nullptr, r.profiles.stage_);
  }
}

TEST(AnchorPieces, ANewBeginRestartsTheBufferAndOnlyOneCertificateStagesAtATime) {
  Rig r;
  const std::vector<uint8_t> der = fakeDer(300);
  ASSERT_TRUE(Rig::ok(r.begin(0, 10, std::string(64, 'a'))));
  ASSERT_TRUE(Rig::ok(r.add(0, 0, "AAAAAAAA")));
  ASSERT_TRUE(Rig::ok(r.begin(1, der.size(), shaHex(der)))) << "a second profile takes the one buffer";
  EXPECT_EQ(0, r.profiles.stage_len_);
  EXPECT_EQ(1, r.profiles.stage_idx_);
  EXPECT_TRUE(has(r.add(0, 0, "AAAA"), "\"status\":\"invalid\"")) << "the first profile's stage is gone";
  EXPECT_TRUE(Rig::ok(r.send(1, der))) << "and a begin after a begin starts the sending again";
  EXPECT_TRUE(r.profiles.p[1].hasCa());
  EXPECT_FALSE(r.profiles.p[0].hasCa());
}

TEST(AnchorPieces, TheInUseRuleHoldsForEveryStepButNotForTheCable) {
  Rig r;
  r.in_use = 2;
  const std::vector<uint8_t> der = fakeDer(300);
  std::string a = r.begin(2, der.size(), shaHex(der));
  EXPECT_TRUE(has(a, "\"status\":\"refused\"") && has(a, "in-use")) << a;
  EXPECT_EQ(nullptr, r.profiles.stage_);
  // A buffer begun for another profile cannot be fed or ended through the one in use.
  ASSERT_TRUE(Rig::ok(r.begin(3, der.size(), shaHex(der))));
  a = r.add(2, 0, b64(der));
  EXPECT_TRUE(has(a, "\"status\":\"refused\"") && has(a, "in-use")) << a;
  a = r.end(2);
  EXPECT_TRUE(has(a, "\"status\":\"refused\"") && has(a, "in-use")) << a;
  EXPECT_EQ(0, r.profiles.stage_len_);
  EXPECT_TRUE(Rig::ok(r.send(3, der))) << "a profile it is not on";
  EXPECT_FALSE(r.profiles.p[2].hasCa());
  r.cable = true;
  EXPECT_TRUE(Rig::ok(r.send(2, der))) << "the cable may";
  EXPECT_TRUE(r.profiles.p[2].hasCa());
}

TEST(AnchorPieces, AnIdleBufferIsFreedAfterTenMinutes) {
  Rig r;
  const std::vector<uint8_t> der = fakeDer(300);
  const std::string text = b64(der);
  ASSERT_TRUE(Rig::ok(r.begin(0, der.size(), shaHex(der))));
  g_mock_millis += MqttProfiles::STAGE_TIMEOUT_MS - 1;
  ASSERT_TRUE(Rig::ok(r.add(0, 0, text.substr(0, 100)))) << "just inside the bound";
  g_mock_millis += MqttProfiles::STAGE_TIMEOUT_MS - 1;   // an add counts as activity: ten minutes since the last
  r.profiles.stageTick(g_mock_millis);
  EXPECT_NE(nullptr, r.profiles.stage_);
  ASSERT_TRUE(Rig::ok(r.add(0, 100, text.substr(100, 100))));
  g_mock_millis += MqttProfiles::STAGE_TIMEOUT_MS;
  std::string a = r.add(0, 200, text.substr(200));
  EXPECT_TRUE(has(a, "\"status\":\"invalid\"")) << a << ": the stage timed out before it was read";
  EXPECT_EQ(nullptr, r.profiles.stage_);
  EXPECT_FALSE(r.profiles.p[0].hasCa());
  EXPECT_TRUE(has(r.end(0), "\"status\":\"invalid\""));
}

TEST(AnchorPieces, TheTimeoutFreesTheBufferWithNobodyTyping) {
  Rig r;
  ASSERT_TRUE(Rig::ok(r.begin(0, 100, std::string(64, 'a'))));
  ASSERT_NE(nullptr, r.profiles.stage_);
  r.profiles.stageTick(g_mock_millis + MqttProfiles::STAGE_TIMEOUT_MS - 1);
  EXPECT_NE(nullptr, r.profiles.stage_);
  r.profiles.stageTick(g_mock_millis + MqttProfiles::STAGE_TIMEOUT_MS);
  EXPECT_EQ(nullptr, r.profiles.stage_);
  // and across the 32-bit clock wrapping
  g_mock_millis = 0xFFFFFFF0u;
  ASSERT_TRUE(Rig::ok(r.begin(0, 100, std::string(64, 'a'))));
  r.profiles.stageTick(g_mock_millis + 5);
  EXPECT_NE(nullptr, r.profiles.stage_);
  r.profiles.stageTick(g_mock_millis + MqttProfiles::STAGE_TIMEOUT_MS + 10);
  EXPECT_EQ(nullptr, r.profiles.stage_);
}

TEST(AnchorPieces, TheRequestIsOneFormOnly) {
  Rig r;
  const std::vector<uint8_t> der = fakeDer(300);
  const std::string sha = shaHex(der);
  EXPECT_TRUE(has(r.call("{\"profile\":0,\"ca_end\":true,\"clear\":true}"), "\"status\":\"invalid\""));
  EXPECT_TRUE(has(r.call("{\"profile\":0,\"ca_begin\":{\"size\":300,\"sha256\":\"" + sha + "\"},\"ca_end\":true}"), "\"status\":\"invalid\""));
  EXPECT_TRUE(has(r.call("{\"profile\":0,\"ca_end\":false}"), "\"status\":\"invalid\""));
  EXPECT_TRUE(has(r.begin(0, 0, sha), "\"status\":\"invalid\"")) << "size 0";
  EXPECT_TRUE(has(r.begin(0, 1441, sha), "\"status\":\"invalid\"")) << "over 1,440 DER bytes";
  EXPECT_TRUE(has(r.begin(0, 300, sha.substr(1)), "\"status\":\"invalid\"")) << "63 digits";
  EXPECT_TRUE(has(r.begin(0, 300, "g" + sha.substr(1)), "\"status\":\"invalid\"")) << "not hex";
  EXPECT_TRUE(has(r.begin(6, 300, sha), "\"status\":\"invalid\"")) << "no profile 6";
  EXPECT_TRUE(has(r.call("{\"profile\":0,\"ca_begin\":5}"), "\"status\":\"invalid\""));
  EXPECT_EQ(nullptr, r.profiles.stage_) << "none of those began anything";
}

TEST(AnchorPieces, TheWholeCertificateFormAndTheConsoleStillWork) {
  Rig r;
  const std::vector<uint8_t> der = fakeDer(300);
  EXPECT_TRUE(Rig::ok(r.call("{\"profile\":0,\"ca\":\"" + b64(der) + "\"}")));
  EXPECT_TRUE(r.profiles.p[0].hasCa());
  EXPECT_EQ("> profile 1 ca: send ca add <base64>, then ca end", r.cli("set mqtt.profile 1 ca begin"));
  EXPECT_EQ("> ca 4 chars", r.cli("set mqtt.profile 1 ca add AAAA"));
  EXPECT_EQ(0u, r.cli("set mqtt.profile 1 ca end").find("ERR: ca is not one complete")) << "no size or hash promised, none checked";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
