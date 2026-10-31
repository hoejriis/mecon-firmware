// MQTT broker profiles in the Companion prefs (examples/companion_radio/NodePrefs.h) and their CLI
// (src/helpers/mqtt/MqttProfiles.*): a prefs file without them still loads, six full profiles round-trip, the
// Hardened rule holds at the command, and no reply ever carries a credential.
#define ENABLE_WIFI_INTERFACE 1
#define ENABLE_MQTT 1
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"
#include "helpers/mqtt/MqttCore.h"

// CommonRadioPrefs.cpp needs the board layer (target.h); the radio settings are not under test here.
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }
bool CommonRadioPrefs::handleCommand(const char*, uint32_t, char*) { return false; }

class ReplayStream : public Stream {
  const char* _text;
  int _pos = 0;
  int _len;
public:
  explicit ReplayStream(const char* text) : _text(text), _len(strlen(text)) { }
  int available() override { return _len - _pos; }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};

class CaptureStream : public Stream {
  std::string _text;
  size_t emit(long long value) {
    char text[24];
    int length = snprintf(text, sizeof(text), "%lld", value);
    return write(reinterpret_cast<const uint8_t*>(text), length);
  }
public:
  size_t write(uint8_t value) override { _text.push_back(static_cast<char>(value)); return 1; }
  size_t write(const uint8_t* buffer, size_t size) override { _text.append(reinterpret_cast<const char*>(buffer), size); return size; }
  size_t print(unsigned char value, int = DEC) override { return emit(value); }
  size_t print(int value, int = DEC) override { return emit(value); }
  size_t print(unsigned int value, int = DEC) override { return emit(value); }
  size_t print(long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long value, int = DEC) override { return emit(value); }
  size_t print(long long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long long value, int = DEC) override { return emit(value); }
  const std::string& text() const { return _text; }
};


static std::string cmd(MqttProfiles& p, const char* c, uint8_t posture = Posture::NORMAL, const MqttCore* core = nullptr) {
  char reply[160] = {0};
  MqttProfileChange ch;
  if (!mqttProfileCommand(p, core, posture, c, reply, 150, ch)) return "<unhandled>";
  return reply;
}

static const char* ANCHOR = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

// ---- persistence

TEST(MqttPrefs, APrefsFileWithoutAnMqttSectionLoadsWithTheDefaults) {
  ReplayStream in("{name:\"Stock\",wifi:{ssid:\"stocknet\",pwd:\"stockpw-fake\",enabled:1}}");
  NodePrefs p;
  ASSERT_TRUE(p.loadSerial(in));
  EXPECT_EQ(1, p.mqtt_enabled);
  EXPECT_EQ(0u, p.mqtt_profiles.configuredCount());
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    EXPECT_EQ(MqttProfile::Unset, p.mqtt_profiles.p[i].kind);
    EXPECT_EQ(MqttProfiles::DEFAULT_PRIORITY, p.mqtt_profiles.p[i].prio);
    EXPECT_EQ(1, p.mqtt_profiles.p[i].tls);   // secure unless the owner says otherwise
  }
}

TEST(MqttPrefs, SixFullProfilesRoundTrip) {
  NodePrefs a;
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    MqttProfile& q = a.mqtt_profiles.p[i];
    q.kind = (i % 2) ? MqttProfile::Discovery : MqttProfile::Static;
    q.prio = 200 - i * 7;
    q.tls = (i % 3) != 0;
    q.port = 1000 + i;
    snprintf(q.host, sizeof(q.host), "h%u-%s", i, "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuv");   // 63 chars
    snprintf(q.site, sizeof(q.site), "s%u-%s", i, "abcdefghijklmnopqrs");   // 23 chars
    strcpy(q.anchor, ANCHOR);
    snprintf(q.user, sizeof(q.user), "u%u-%s", i, "abcdefghijklmnopqrstuvwxyz0123");   // 31 chars
    snprintf(q.pwd, sizeof(q.pwd), "pw%u-%s", i, "0123456789012345678901234567890123456789012345678901234567");   // 63 chars
    q.ws = (i % 2) == 0;
    snprintf(q.path, sizeof(q.path), "/p%u-%s", i, "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstu");   // 63 chars
  }
  a.mqtt_enabled = 0;
  CaptureStream out;
  ASSERT_TRUE(a.saveSerial(out));
  printf("prefs file with six full MQTT profiles: %u bytes\n", (unsigned)out.text().size());
  ReplayStream in(out.text().c_str());
  NodePrefs b;
  ASSERT_TRUE(b.loadSerial(in));
  EXPECT_EQ(0, b.mqtt_enabled);
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    const MqttProfile& x = a.mqtt_profiles.p[i];
    const MqttProfile& y = b.mqtt_profiles.p[i];
    EXPECT_EQ(x.kind, y.kind) << int(i);
    EXPECT_EQ(x.prio, y.prio) << int(i);
    EXPECT_EQ(x.tls, y.tls) << int(i);
    EXPECT_EQ(x.port, y.port) << int(i);
    EXPECT_STREQ(x.host, y.host) << int(i);
    EXPECT_STREQ(x.site, y.site) << int(i);
    EXPECT_STREQ(x.anchor, y.anchor) << int(i);
    EXPECT_STREQ(x.user, y.user) << int(i);
    EXPECT_STREQ(x.pwd, y.pwd) << int(i);
    EXPECT_EQ(x.ws, y.ws) << int(i);
    EXPECT_STREQ(x.path, y.path) << int(i);
  }
}

TEST(MqttPrefs, TheMqttSectionOfAnEmptyConfigurationIsSmall) {
  NodePrefs p;
  CaptureStream with;
  ASSERT_TRUE(p.saveSerial(with));
  EXPECT_LT(with.text().size(), 2600u);
  printf("prefs file with no MQTT profile: %u bytes\n", (unsigned)with.text().size());
}

// ---- CLI

TEST(MqttProfileCli, SetsAndShowsAStaticProfile) {
  MqttProfiles p;
  EXPECT_EQ("> profile 2 is a static profile", cmd(p, "set mqtt.profile 2 type static"));
  EXPECT_EQ("> profile 2 host updated", cmd(p, "set mqtt.profile 2 host broker.example.org"));
  EXPECT_EQ("> profile 2 port updated", cmd(p, "set mqtt.profile 2 port 8884"));
  EXPECT_EQ("> profile 2 user updated", cmd(p, "set mqtt.profile 2 user alice-fake"));
  EXPECT_EQ("> profile 2 pwd updated", cmd(p, "set mqtt.profile 2 pwd sekrit-fake"));
  EXPECT_EQ("> profile 2 priority is now 77", cmd(p, "set mqtt.profile 2 priority 77"));
  EXPECT_EQ("> 2: static broker.example.org:8884 tls=on anchor=unset prio=77 user=set pwd=set last=none",
            cmd(p, "get mqtt.profile 2"));
  EXPECT_STREQ("sekrit-fake", p.p[2].pwd);
  EXPECT_TRUE(p.configured(2));
}

TEST(MqttProfileCli, TheDefaultPortFollowsTheTlsSetting) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  EXPECT_EQ(8883, p.p[0].effectivePort());
  cmd(p, "set mqtt.profile 0 tls off");
  EXPECT_EQ(1883, p.p[0].effectivePort());
  cmd(p, "set mqtt.profile 0 port 9000");
  EXPECT_EQ(9000, p.p[0].effectivePort());
}

TEST(MqttProfileCli, SetsADiscoveryProfileAndNormalisesTheAnchor) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 1 type discovery");
  cmd(p, "set mqtt.profile 1 site home-1");
  EXPECT_FALSE(p.configured(1));   // no anchor yet
  EXPECT_EQ("> profile 1 anchor updated",
            cmd(p, "set mqtt.profile 1 anchor 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF"));
  EXPECT_STREQ(ANCHOR, p.p[1].anchor);
  EXPECT_TRUE(p.configured(1));
  EXPECT_EQ("> 1: discovery site=home-1 anchor=fingerprint prio=50 user=unset pwd=unset last=none", cmd(p, "get mqtt.profile 1"));
  EXPECT_EQ("> profile 1 anchor cleared", cmd(p, "set mqtt.profile 1 anchor clear"));
  EXPECT_FALSE(p.configured(1));
}

TEST(MqttProfileCli, ADiscoveryProfileRefusesPlainMqtt) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 1 type discovery");
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 tls off").find("ERR"));
  EXPECT_TRUE(p.p[1].useTls());
}

TEST(MqttProfileCli, ADiscoveryProfileIsTlsEvenIfTheSlotWasPlainBefore) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 tls off");
  cmd(p, "set mqtt.profile 0 type discovery");
  EXPECT_EQ(0, p.p[0].tls);          // the stored byte is still off ...
  EXPECT_TRUE(p.p[0].useTls());      // ... but a discovery profile is never plain
  EXPECT_EQ(8883, p.p[0].effectivePort());
}

TEST(MqttProfileCli, ClearResetsEverythingIncludingThePriorityAndTheCredentials) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 3 type static");
  cmd(p, "set mqtt.profile 3 host x");
  cmd(p, "set mqtt.profile 3 user u");
  cmd(p, "set mqtt.profile 3 pwd y");
  cmd(p, "set mqtt.profile 3 tls off");
  cmd(p, "set mqtt.profile 3 priority 5");
  EXPECT_EQ("> profile 3 cleared", cmd(p, "set mqtt.profile 3 clear"));
  EXPECT_FALSE(p.configured(3));
  EXPECT_EQ(0, p.p[3].user[0]);
  EXPECT_EQ(0, p.p[3].pwd[0]);
  EXPECT_EQ(MqttProfiles::DEFAULT_PRIORITY, p.p[3].prio);
  EXPECT_EQ(1, p.p[3].tls);
  EXPECT_EQ("> 3: (not set)", cmd(p, "get mqtt.profile 3"));
}

TEST(MqttProfileCli, AnEmptyUserOrPasswordClearsIt) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 1 user u");
  cmd(p, "set mqtt.profile 1 pwd w");
  EXPECT_EQ("> profile 1 pwd cleared", cmd(p, "set mqtt.profile 1 pwd"));
  EXPECT_EQ("> profile 1 user cleared", cmd(p, "set mqtt.profile 1 user"));
  EXPECT_EQ(0, p.p[1].user[0]);
  EXPECT_EQ(0, p.p[1].pwd[0]);
}

TEST(MqttProfileCli, RejectsBadInputAndChangesNothing) {
  MqttProfiles p;
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 6 host x").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile x host x").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 type nonsense").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 host").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 host bad host").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 host a/b").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 host 1234567890123456789012345678901234567890123456789012345678901234").find("ERR"));   // 64
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 port 65536").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 port abc").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 tls maybe").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 anchor 1234").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 anchor zz112233445566778899aabbccddeeff00112233445566778899aabbccddeeff").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 site").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 site 123456789012345678901234").find("ERR"));   // 24
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 user 12345678901234567890123456789012").find("ERR"));   // 32
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 pwd 01234567890123456789012345678901234567890123456789012345678901234").find("ERR"));   // 65
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 priority 256").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 bogus").find("ERR"));
  EXPECT_EQ(MqttProfile::Unset, p.p[1].kind);
  EXPECT_EQ(0, p.p[1].host[0]);
  EXPECT_EQ(0, p.p[1].anchor[0]);
  EXPECT_EQ(0, p.p[1].user[0]);
}

TEST(MqttProfileCli, AcceptsTheLongestValues) {
  MqttProfiles p;
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 host 123456789012345678901234567890123456789012345678901234567890123").find(">"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 site 12345678901234567890123").find(">"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 user 1234567890123456789012345678901").find(">"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 pwd 012345678901234567890123456789012345678901234567890123456789012").find(">"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 1 port 65535").find(">"));
}

TEST(MqttProfileCli, HardenedRefusesPlainMqttAtTheCommand) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  EXPECT_EQ("ERR: plain MQTT is refused in the Hardened posture", cmd(p, "set mqtt.profile 0 tls off", Posture::HARDENED));
  EXPECT_EQ(1, p.p[0].tls);
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 0 tls on", Posture::HARDENED).find(">"));   // TLS is always fine
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 0 tls off", Posture::NORMAL).find(">"));
  EXPECT_EQ(0, p.p[0].tls);
}

TEST(MqttProfileCli, AStoredPlainProfileIsNotPermittedInHardened) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  cmd(p, "set mqtt.profile 0 tls off");
  EXPECT_TRUE(MqttProfiles::permitted(p.p[0], Posture::NORMAL));
  EXPECT_FALSE(MqttProfiles::permitted(p.p[0], Posture::HARDENED));
  EXPECT_FALSE(MqttProfiles::permitted(p.p[0], 7));   // a damaged posture byte is Hardened
  EXPECT_TRUE(p.anyPermitted(Posture::NORMAL));
  EXPECT_FALSE(p.anyPermitted(Posture::HARDENED));
  cmd(p, "set mqtt.profile 1 type discovery");
  cmd(p, "set mqtt.profile 1 site s");
  cmd(p, "set mqtt.profile 1 anchor 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
  EXPECT_TRUE(p.anyPermitted(Posture::HARDENED));
}

TEST(MqttProfileCli, ReportsWhatChanged) {
  MqttProfiles p;
  char reply[160];
  MqttProfileChange ch;
  ASSERT_TRUE(mqttProfileCommand(p, nullptr, 0, "set mqtt.profile 4 host x", reply, 150, ch));
  EXPECT_TRUE(ch.changed); EXPECT_EQ(4, ch.index); EXPECT_TRUE(ch.credentials);
  ASSERT_TRUE(mqttProfileCommand(p, nullptr, 0, "set mqtt.profile 4 priority 9", reply, 150, ch));
  EXPECT_TRUE(ch.changed); EXPECT_EQ(4, ch.index); EXPECT_FALSE(ch.credentials);
  ASSERT_TRUE(mqttProfileCommand(p, nullptr, 0, "get mqtt.profiles", reply, 150, ch));
  EXPECT_FALSE(ch.changed);
  ASSERT_TRUE(mqttProfileCommand(p, nullptr, 0, "set mqtt.profile 4 host", reply, 150, ch));
  EXPECT_FALSE(ch.changed);
}

TEST(MqttProfileCli, IgnoresOtherCommands) {
  MqttProfiles p;
  EXPECT_EQ("<unhandled>", cmd(p, "get mqtt.status"));
  EXPECT_EQ("<unhandled>", cmd(p, "get wifi.profile 1"));
  EXPECT_EQ("<unhandled>", cmd(p, "get name"));
}

TEST(MqttProfileCli, NoReplyEverCarriesACredential) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  cmd(p, "set mqtt.profile 0 user user-marker-xq");
  cmd(p, "set mqtt.profile 0 pwd pwd-marker-xq");
  cmd(p, "set mqtt.profile 1 type discovery");
  cmd(p, "set mqtt.profile 1 pwd pwd-marker-zz");
  const char* cmds[] = {"set mqtt.profile 0 user user-marker-xq", "set mqtt.profile 0 pwd pwd-marker-xq",
                        "get mqtt.profile 0", "get mqtt.profile 1", "get mqtt.profiles", "get mqtt.profiles 1",
                        "set mqtt.profile 0 priority 3", "set mqtt.profile 0 pwd"};
  for (const char* c : cmds) {
    std::string r = cmd(p, c);
    EXPECT_EQ(std::string::npos, r.find("marker")) << c << " -> " << r;
  }
}

TEST(MqttProfileCli, ListingPagesThroughAllProfiles) {
  MqttProfiles p;
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    char c[96];
    snprintf(c, sizeof(c), "set mqtt.profile %u type static", i);
    cmd(p, c);
    snprintf(c, sizeof(c), "set mqtt.profile %u host a-fairly-long-broker-name-%u.example.org", i, i);
    cmd(p, c);
  }
  std::string seen;
  std::string c = "get mqtt.profiles";
  for (int page = 0; page < 12; page++) {
    std::string r = cmd(p, c.c_str());
    ASSERT_LT(r.size(), 150u);
    seen += r;
    size_t n = r.find("... next:");
    if (n == std::string::npos) break;
    c = "get mqtt.profiles " + r.substr(n + 9, 1);
  }
  for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
    char needle[48];
    snprintf(needle, sizeof(needle), "broker-name-%u.example.org:", i);
    EXPECT_NE(std::string::npos, seen.find(needle)) << int(i);
  }
}

TEST(MqttProfileCli, TheLongestProfileLineFitsTheReply) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host 123456789012345678901234567890123456789012345678901234567890123");
  cmd(p, "set mqtt.profile 0 port 65535");
  cmd(p, "set mqtt.profile 0 tls off");
  cmd(p, "set mqtt.profile 0 priority 255");
  cmd(p, "set mqtt.profile 0 user u");
  cmd(p, "set mqtt.profile 0 pwd p");
  cmd(p, "set mqtt.profile 0 anchor 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
  MqttCore core;
  core.configure(&p);
  core.setPosture(Posture::HARDENED);
  core.begin(0);
  std::string r = cmd(p, "get mqtt.profile 0", Posture::HARDENED, &core);
  EXPECT_NE(std::string::npos, r.find("last=posture")) << r;   // the longest failure name is 'timeout'; posture is as long
  EXPECT_NE(std::string::npos, r.find("pwd=set"));
  EXPECT_LT(r.size(), 149u);
}

TEST(MqttProfileCli, TheListingShowsTheLastFailurePerProfile) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host a.example");
  cmd(p, "set mqtt.profile 1 type static");
  cmd(p, "set mqtt.profile 1 host b.example");
  MqttCore core;
  core.configure(&p);
  core.setLink(0, true);
  core.begin(0);
  MqttAction a = core.poll(0);
  ASSERT_EQ(MqttAction::Connect, a.kind);
  core.onConnectFailed(10, MqttFail::Auth);
  std::string r = cmd(p, "get mqtt.profile 0", Posture::NORMAL, &core);
  EXPECT_NE(std::string::npos, r.find("last=auth")) << r;
  r = cmd(p, "get mqtt.profile 1", Posture::NORMAL, &core);
  EXPECT_NE(std::string::npos, r.find("last=none")) << r;
}

// ---- CA certificate anchors

// A syntactically valid DER SEQUENCE of `payload` bytes (not a real certificate: the platform parses the real thing),
// as base64.
static std::string fakeCaB64(size_t payload, uint8_t seed = 1) {
  std::string der;
  der.push_back(0x30);
  if (payload < 128) der.push_back((char)payload);
  else if (payload < 256) { der.push_back((char)0x81); der.push_back((char)payload); }
  else { der.push_back((char)0x82); der.push_back((char)(payload >> 8)); der.push_back((char)(payload & 255)); }
  for (size_t i = 0; i < payload; i++) der.push_back((char)(seed + i * 7));
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < der.size(); i += 3) {
    uint32_t v = (uint8_t)der[i] << 16;
    size_t rem = der.size() - i;
    if (rem > 1) v |= (uint8_t)der[i + 1] << 8;
    if (rem > 2) v |= (uint8_t)der[i + 2];
    out.push_back(T[(v >> 18) & 63]);
    out.push_back(T[(v >> 12) & 63]);
    out.push_back(rem > 1 ? T[(v >> 6) & 63] : '=');
    out.push_back(rem > 2 ? T[v & 63] : '=');
  }
  return out;
}

// Sends a CA the way a tool does over a short serial line: begin, pieces of 48 characters, end. Returns the last reply.
static std::string sendCa(MqttProfiles& p, int idx, const std::string& b64, size_t piece = 48) {
  char c[160];
  snprintf(c, sizeof(c), "set mqtt.profile %d ca begin", idx);
  std::string r = cmd(p, c);
  for (size_t i = 0; i < b64.size(); i += piece) {
    snprintf(c, sizeof(c), "set mqtt.profile %d ca add %s", idx, b64.substr(i, piece).c_str());
    r = cmd(p, c);
    if (r.compare(0, 3, "ERR") == 0) return r;
  }
  snprintf(c, sizeof(c), "set mqtt.profile %d ca end", idx);
  return cmd(p, c);
}

TEST(MqttCa, ASetInPiecesIsInstalledWholeAndListedAsCa) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 1 type discovery");
  cmd(p, "set mqtt.profile 1 site home-1");
  std::string ca = fakeCaB64(600);
  EXPECT_EQ("> profile 1 anchor is a CA certificate", sendCa(p, 1, ca));
  EXPECT_STREQ(ca.c_str(), p.p[1].ca);
  EXPECT_EQ(2, p.p[1].anchorKind());
  EXPECT_TRUE(p.configured(1));   // a CA alone makes a discovery profile usable
  EXPECT_EQ("> 1: discovery site=home-1 anchor=ca prio=50 user=unset pwd=unset last=none", cmd(p, "get mqtt.profile 1"));
  EXPECT_EQ(nullptr, p.stage_);   // the staging buffer is gone
}

TEST(MqttCa, TheListingNamesTheKindAndNeverTheBody) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  EXPECT_NE(std::string::npos, cmd(p, "get mqtt.profile 0").find("anchor=unset"));
  cmd(p, ("set mqtt.profile 0 anchor " + std::string(ANCHOR)).c_str());
  EXPECT_NE(std::string::npos, cmd(p, "get mqtt.profile 0").find("anchor=fingerprint"));
  std::string ca = fakeCaB64(300);
  sendCa(p, 0, ca);
  std::string r = cmd(p, "get mqtt.profile 0");
  EXPECT_NE(std::string::npos, r.find("anchor=ca")) << r;
  EXPECT_EQ(std::string::npos, r.find(ca.substr(0, 20)));
  EXPECT_LT(r.size(), 149u);
}

TEST(MqttCa, ACaReplacesAFingerprintAndAFingerprintReplacesACa) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  cmd(p, ("set mqtt.profile 0 anchor " + std::string(ANCHOR)).c_str());
  sendCa(p, 0, fakeCaB64(300));
  EXPECT_EQ(0, p.p[0].anchor[0]);       // one anchor per profile: the fingerprint is gone
  EXPECT_TRUE(p.p[0].hasCa());
  cmd(p, ("set mqtt.profile 0 anchor " + std::string(ANCHOR)).c_str());
  EXPECT_FALSE(p.p[0].hasCa());         // and back
  EXPECT_EQ(nullptr, p.p[0].ca);
  EXPECT_EQ(1, p.p[0].anchorKind());
}

TEST(MqttCa, AnchorClearAndCaClearAndProfileClearAllRemoveTheCa) {
  MqttProfiles p;
  for (const char* how : {"set mqtt.profile 2 anchor clear", "set mqtt.profile 2 ca clear", "set mqtt.profile 2 clear"}) {
    cmd(p, "set mqtt.profile 2 type static");
    sendCa(p, 2, fakeCaB64(300));
    ASSERT_TRUE(p.p[2].hasCa()) << how;
    cmd(p, how);
    EXPECT_FALSE(p.p[2].hasCa()) << how;
    EXPECT_EQ(nullptr, p.p[2].ca) << how;
  }
}

TEST(MqttCa, ABadCaIsRefusedAndTheProfileKeepsWhatItHad) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, ("set mqtt.profile 0 anchor " + std::string(ANCHOR)).c_str());
  std::string good = fakeCaB64(300);
  // truncated paste: the DER declares more than arrived
  EXPECT_EQ(0u, sendCa(p, 0, good.substr(0, good.size() - 40)).find("ERR")) ;
  // not base64
  cmd(p, "set mqtt.profile 0 ca begin");
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 0 ca add not*base64!").find("ERR"));
  // base64 of something that is not a DER SEQUENCE
  EXPECT_EQ(0u, sendCa(p, 0, std::string(120, 'A')).find("ERR"));
  // trailing garbage inside the DER length
  EXPECT_EQ(0u, sendCa(p, 0, fakeCaB64(300) + "AAAA").find("ERR"));
  EXPECT_FALSE(p.p[0].hasCa());
  EXPECT_STREQ(ANCHOR, p.p[0].anchor);   // the fingerprint survived every refusal
  EXPECT_EQ(nullptr, p.stage_);
}

TEST(MqttCa, TheStagingProtocolIsStrict) {
  MqttProfiles p;
  EXPECT_EQ("ERR: ca begin first", cmd(p, "set mqtt.profile 0 ca add AAAA"));
  EXPECT_EQ("ERR: ca begin first", cmd(p, "set mqtt.profile 0 ca end"));
  cmd(p, "set mqtt.profile 0 ca begin");
  EXPECT_EQ("ERR: ca begin first", cmd(p, "set mqtt.profile 1 ca add AAAA"));   // another profile is not the one being staged
  EXPECT_EQ("> ca 4 chars", cmd(p, "set mqtt.profile 0 ca add AAAA"));
  EXPECT_EQ("> ca 4 chars", cmd(p, "set mqtt.profile 0 ca add -----BEGIN CERTIFICATE-----"));   // armour lines are skipped
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 0 ca add").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set mqtt.profile 0 ca frob").find("ERR"));
  EXPECT_EQ(4, p.stage_len_);
  cmd(p, "set mqtt.profile 0 ca begin");   // begin starts again, empty
  EXPECT_EQ(0, p.stage_len_);
  p.dropStage();
}

TEST(MqttCa, NothingIsSavedBeforeCaEnd) {
  // The Companion saves its prefs whenever a command reports a change, so begin and add must not report one.
  MqttProfiles p;
  MqttProfileChange ch;
  char reply[160];
  auto run = [&](const char* c) { mqttProfileCommand(p, nullptr, Posture::NORMAL, c, reply, 150, ch); return ch.changed; };
  EXPECT_FALSE(run("set mqtt.profile 0 ca begin"));
  EXPECT_FALSE(run("set mqtt.profile 0 ca add AAAA"));
  p.dropStage();
  std::string ca = fakeCaB64(300);
  run("set mqtt.profile 0 ca begin");
  EXPECT_FALSE(run(("set mqtt.profile 0 ca add " + ca).c_str()));
  EXPECT_TRUE(run("set mqtt.profile 0 ca end"));
  EXPECT_TRUE(p.p[0].hasCa());
  EXPECT_TRUE(run("set mqtt.profile 0 ca clear"));
}

TEST(MqttCa, TheLongestCaFitsAndOneMoreCharacterDoesNot) {
  MqttProfiles p;
  // 1,437 payload bytes + 4 header = 1,441 bytes = 1,924 characters: just over; 1,436 + 4 = 1,440 bytes = 1,920 characters
  std::string fits = fakeCaB64(1436);
  ASSERT_EQ(MqttProfile::CA_B64_MAX, fits.size());
  EXPECT_EQ("> profile 0 anchor is a CA certificate", sendCa(p, 0, fits));
  std::string over = fakeCaB64(1440);
  std::string r = sendCa(p, 1, over);
  EXPECT_EQ(0u, r.find("ERR: ca is longer than")) << r;
  EXPECT_FALSE(p.p[1].hasCa());
  EXPECT_EQ(nullptr, p.stage_);
}

TEST(MqttCa, DecodeEdgeCases) {
  uint8_t der[2048];
  std::string ok = fakeCaB64(200);
  EXPECT_EQ(203, mqttCaDecode(ok.c_str(), der, sizeof(der)));
  EXPECT_EQ(0x30, der[0]);
  EXPECT_EQ(-1, mqttCaDecode(ok.c_str(), der, 100));                 // does not fit
  EXPECT_EQ(-1, mqttCaDecode("", der, sizeof(der)));
  EXPECT_EQ(-1, mqttCaDecode((ok + "=A").c_str(), der, sizeof(der)));   // data after the padding
  EXPECT_EQ(-1, mqttCaDecode((ok.substr(0, 40) + " " + ok.substr(40)).c_str(), der, sizeof(der)));
  EXPECT_TRUE(mqttHostIsLiteral("10.0.0.5"));
  EXPECT_FALSE(mqttHostIsLiteral("broker.example"));
  EXPECT_FALSE(mqttHostIsLiteral("1.example"));
  EXPECT_FALSE(mqttHostIsLiteral(""));
}

TEST(MqttCaPrefs, ACaRoundTripsThroughThePrefsFile) {
  NodePrefs a;
  std::string ca0 = fakeCaB64(450, 3), ca3 = fakeCaB64(1000, 9);
  sendCa(a.mqtt_profiles, 0, ca0);
  a.mqtt_profiles.p[0].kind = MqttProfile::Static;
  a.mqtt_profiles.p[3].kind = MqttProfile::Discovery;
  sendCa(a.mqtt_profiles, 3, ca3);
  CaptureStream out;
  ASSERT_TRUE(a.saveSerial(out));
  NodePrefs b;
  ReplayStream in(out.text().c_str());
  ASSERT_TRUE(b.loadSerial(in));
  ASSERT_NE(nullptr, b.mqtt_profiles.p[0].ca);
  ASSERT_NE(nullptr, b.mqtt_profiles.p[3].ca);
  EXPECT_STREQ(ca0.c_str(), b.mqtt_profiles.p[0].ca);
  EXPECT_STREQ(ca3.c_str(), b.mqtt_profiles.p[3].ca);
  EXPECT_EQ(nullptr, b.mqtt_profiles.p[1].ca);
  // loading again over the same object replaces, never appends
  ReplayStream in2(out.text().c_str());
  ASSERT_TRUE(b.loadSerial(in2));
  EXPECT_STREQ(ca0.c_str(), b.mqtt_profiles.p[0].ca);
}

TEST(MqttCaPrefs, TheFileSizeWithoutACaIsUnchangedAndSixLargeOnesAreMeasured) {
  NodePrefs none;
  CaptureStream base;
  ASSERT_TRUE(none.saveSerial(base));
  NodePrefs one;
  sendCa(one.mqtt_profiles, 0, fakeCaB64(450));
  CaptureStream with1;
  ASSERT_TRUE(one.saveSerial(with1));
  NodePrefs six;
  for (int i = 0; i < 6; i++) sendCa(six.mqtt_profiles, i, fakeCaB64(1436));
  CaptureStream with6;
  ASSERT_TRUE(six.saveSerial(with6));
  printf("prefs file: %u bytes with no CA, %u with one EC-sized CA (450 DER bytes), %u with six of the largest CA (1,440 DER bytes)\n",
         (unsigned)base.text().size(), (unsigned)with1.text().size(), (unsigned)with6.text().size());
  EXPECT_EQ(std::string::npos, base.text().find("c000"));          // no CA, no CA keys
  EXPECT_LT(with1.text().size() - base.text().size(), 800u);
  EXPECT_LT(with6.text().size() - base.text().size(), 6u * 2100u);
}

TEST(MqttCaPrefs, ACaWithAGapIsKeptAsAnUnusableAnchorNotDroppedToNoAnchor) {
  NodePrefs a;
  sendCa(a.mqtt_profiles, 0, fakeCaB64(450));
  CaptureStream out;
  ASSERT_TRUE(a.saveSerial(out));
  std::string text = out.text();
  size_t k = text.find("c001:");     // remove piece 1
  ASSERT_NE(std::string::npos, k);
  size_t e = text.find('"', text.find('"', k) + 1);
  text.erase(k, e + 2 - k);
  NodePrefs b;
  ReplayStream in(text.c_str());
  ASSERT_TRUE(b.loadSerial(in));
  // Dropping it would turn a pinned profile into an unanchored one that connects unverified: it stays, and fails closed.
  EXPECT_TRUE(b.mqtt_profiles.p[0].hasCa());
  EXPECT_FALSE(MqttProfiles::anchorUsable(b.mqtt_profiles.p[0]));
}

TEST(MqttCaPrefs, AMissingFirstPieceAndACorruptedPieceAreUnusableToo) {
  NodePrefs a;
  sendCa(a.mqtt_profiles, 0, fakeCaB64(450));
  CaptureStream out;
  ASSERT_TRUE(a.saveSerial(out));
  std::string noFirst = out.text();
  size_t k = noFirst.find("c000:");
  ASSERT_NE(std::string::npos, k);
  size_t e = noFirst.find('"', noFirst.find('"', k) + 1);
  noFirst.erase(k, e + 2 - k);
  NodePrefs b;
  ReplayStream in(noFirst.c_str());
  ASSERT_TRUE(b.loadSerial(in));
  EXPECT_TRUE(b.mqtt_profiles.p[0].hasCa());
  EXPECT_FALSE(MqttProfiles::anchorUsable(b.mqtt_profiles.p[0]));
  std::string bad = out.text();
  size_t q = bad.find("c001:\"") + 6;
  bad[q] = '*';
  NodePrefs c;
  ReplayStream in2(bad.c_str());
  ASSERT_TRUE(c.loadSerial(in2));
  EXPECT_FALSE(MqttProfiles::anchorUsable(c.mqtt_profiles.p[0]));
  EXPECT_TRUE(MqttProfiles::anchorUsable(a.mqtt_profiles.p[0]));
}

TEST(MqttCaPrefs, AMalformedFingerprintIsUnusableAndNoAnchorIsFine) {
  MqttProfile q;
  memset(&q, 0, sizeof(q));
  EXPECT_TRUE(MqttProfiles::anchorUsable(q));
  strcpy(q.anchor, "00112233");
  EXPECT_FALSE(MqttProfiles::anchorUsable(q));
  strcpy(q.anchor, ANCHOR);
  EXPECT_TRUE(MqttProfiles::anchorUsable(q));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---- MQTT over WebSocket

TEST(MqttProfileCli, TheLongestWebSocketProfileLineKeepsEveryParsedField) {
  MqttProfiles p;
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host 123456789012345678901234567890123456789012345678901234567890123");
  cmd(p, "set mqtt.profile 0 port 65535");
  cmd(p, "set mqtt.profile 0 tls off");
  cmd(p, "set mqtt.profile 0 transport ws");
  cmd(p, "set mqtt.profile 0 path /12345678901234567890123456789012345678901234567890123456789012");
  cmd(p, "set mqtt.profile 0 priority 255");
  cmd(p, "set mqtt.profile 0 user u");
  cmd(p, "set mqtt.profile 0 pwd p");
  cmd(p, "set mqtt.profile 0 anchor 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
  // The worst case outgrows the 150-byte reply by its last field. transport=ws sits before the anchor, so what is cut is
  // last= (as manage=on already can), never a field the interface parses: a cut transport token would read wrong.
  std::string r = cmd(p, "get mqtt.profile 0");
  EXPECT_NE(std::string::npos, r.find(" tls=off transport=ws anchor=fingerprint prio=255 user=set pwd=set ")) << r;
  EXPECT_LE(r.size(), 149u) << r;
  EXPECT_EQ("> /12345678901234567890123456789012345678901234567890123456789012", cmd(p, "get mqtt.profile 0 path"));
}

TEST(MqttProfileCli, TransportPathAndTheirDefaults) {
  MqttProfiles p;
  EXPECT_EQ("> /mqtt", cmd(p, "get mqtt.profile 3 path")) << "the default, even unset";
  cmd(p, "set mqtt.profile 0 type static");
  cmd(p, "set mqtt.profile 0 host b.example");
  EXPECT_EQ(8883, p.p[0].effectivePort());
  EXPECT_EQ(0, cmd(p, "set mqtt.profile 0 transport ws").rfind(">", 0));
  EXPECT_EQ(443, p.p[0].effectivePort());
  cmd(p, "set mqtt.profile 0 tls off");
  EXPECT_EQ(80, p.p[0].effectivePort());
  cmd(p, "set mqtt.profile 0 port 8080");
  EXPECT_EQ(8080, p.p[0].effectivePort());
  EXPECT_EQ(0, cmd(p, "set mqtt.profile 0 path /mqtt").rfind(">", 0));
  EXPECT_STREQ("", p.p[0].path) << "the default is stored as nothing";
  for (const char* bad : {"set mqtt.profile 0 path mqtt", "set mqtt.profile 0 path /a b", "set mqtt.profile 0 path /q?x",
                          "set mqtt.profile 0 path /f#x", "set mqtt.profile 0 path /\"", "set mqtt.profile 0 transport wss",
                          "set mqtt.profile 0 path /1234567890123456789012345678901234567890123456789012345678901234"})
    EXPECT_EQ(0, cmd(p, bad).rfind("ERR", 0)) << bad;
  EXPECT_STREQ("", p.p[0].path) << "a refused path changes nothing";
  cmd(p, "set mqtt.profile 1 type discovery");
  EXPECT_EQ(0, cmd(p, "set mqtt.profile 1 transport ws").rfind("ERR", 0));
  p.p[1].ws = 1;   // as a damaged prefs file might have it
  EXPECT_FALSE(p.p[1].useWs()) << "a discovery profile never uses WebSocket";
  EXPECT_EQ(0, cmd(p, "set mqtt.profile 0 clear").rfind(">", 0));
  EXPECT_EQ(0, p.p[0].ws);
}
