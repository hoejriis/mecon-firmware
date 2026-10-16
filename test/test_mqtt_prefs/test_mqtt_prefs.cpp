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
  EXPECT_EQ("> 1: discovery site=home-1 anchor=set prio=50 user=unset pwd=unset last=none", cmd(p, "get mqtt.profile 1"));
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
