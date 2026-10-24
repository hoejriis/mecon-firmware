// Wi-Fi profiles in the Companion prefs (examples/companion_radio/NodePrefs.h) and their CLI
// (src/helpers/wifi/WifiProfiles.*): a stock prefs file still loads, ten profiles round-trip, and no reply ever
// carries a password.
#define ENABLE_WIFI_INTERFACE 1
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"
#include "helpers/wifi/WifiCore.h"

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

static std::string cmd(WifiProfiles& p, const char* c, const WifiCore* core = nullptr) {
  char reply[160] = {0};
  WifiProfileChange ch;
  if (!wifiProfileCommand(p, core, c, reply, 150, ch)) return "<unhandled>";
  return reply;
}

// ---- persistence

TEST(WifiPrefs, AStockPrefsFileStillLoadsIntoProfileZero) {
  // Exactly what stock 1.18 writes for the wifi section.
  ReplayStream in("{name:\"Stock\",wifi:{ssid:\"stocknet\",pwd:\"stockpw-fake\",enabled:1}}");
  NodePrefs p;
  ASSERT_TRUE(p.loadSerial(in));
  EXPECT_STREQ("stocknet", p.wifi_profiles.ssid(0));
  EXPECT_STREQ("stockpw-fake", p.wifi_profiles.pwd(0));
  EXPECT_EQ(1, p.wifi_enabled);
  EXPECT_EQ(WifiProfiles::DEFAULT_PRIORITY, p.wifi_profiles.priority(0));
  for (uint8_t i = 1; i < WifiProfiles::COUNT; i++) EXPECT_FALSE(p.wifi_profiles.configured(i));
}

TEST(WifiPrefs, ProfileZeroIsStillWrittenUnderTheStockKeys) {
  NodePrefs p;
  p.wifi_profiles.setSsid(0, "stocknet");
  p.wifi_profiles.setPwd(0, "stockpw-fake");
  CaptureStream out;
  ASSERT_TRUE(p.saveSerial(out));
  EXPECT_NE(std::string::npos, out.text().find("ssid:\"stocknet\""));
  EXPECT_NE(std::string::npos, out.text().find("pwd:\"stockpw-fake\""));
}

TEST(WifiPrefs, TenProfilesRoundTrip) {
  NodePrefs a;
  for (uint8_t i = 0; i < 10; i++) {
    char s[33], w[64];
    snprintf(s, sizeof(s), "net-%u-%s", i, "abcdefghijklmnopqrstuv");   // 32 characters
    snprintf(w, sizeof(w), "pw-%u-%s", i, "0123456789012345678901234567890123456789012345678901234");   // 63 characters
    ASSERT_TRUE(a.wifi_profiles.setSsid(i, s));
    ASSERT_TRUE(a.wifi_profiles.setPwd(i, w));
    a.wifi_profiles.setPriority(i, 200 - i * 7);
  }
  CaptureStream out;
  ASSERT_TRUE(a.saveSerial(out));
  printf("prefs file with ten full-length profiles: %u bytes\n", (unsigned)out.text().size());
  ReplayStream in(out.text().c_str());
  NodePrefs b;
  ASSERT_TRUE(b.loadSerial(in));
  for (uint8_t i = 0; i < 10; i++) {
    EXPECT_STREQ(a.wifi_profiles.ssid(i), b.wifi_profiles.ssid(i)) << int(i);
    EXPECT_STREQ(a.wifi_profiles.pwd(i), b.wifi_profiles.pwd(i)) << int(i);
    EXPECT_EQ(a.wifi_profiles.priority(i), b.wifi_profiles.priority(i)) << int(i);
  }
}

TEST(WifiPrefs, AnEmptyProfileSetAddsOnlyAFewHundredBytes) {
  NodePrefs p;
  CaptureStream out;
  ASSERT_TRUE(p.saveSerial(out));
  EXPECT_LT(out.text().size(), 1500u);
}

// ---- CLI

TEST(WifiProfileCli, SetsAndShowsAProfile) {
  WifiProfiles p;
  EXPECT_EQ("> profile 2 ssid is now cafe", cmd(p, "set wifi.profile 2 ssid cafe"));
  EXPECT_EQ("> profile 2 pwd updated", cmd(p, "set wifi.profile 2 pwd sekrit-fake"));
  EXPECT_EQ("> profile 2 priority is now 77", cmd(p, "set wifi.profile 2 priority 77"));
  EXPECT_EQ("> 2: cafe prio=77 pwd=set last=none", cmd(p, "get wifi.profile 2"));
  EXPECT_STREQ("sekrit-fake", p.pwd(2));
}

TEST(WifiProfileCli, SsidMayContainSpaces) {
  WifiProfiles p;
  cmd(p, "set wifi.profile 1 ssid My Home Net");
  EXPECT_STREQ("My Home Net", p.ssid(1));
}

TEST(WifiProfileCli, AnEmptyPasswordMeansAnOpenNetwork) {
  WifiProfiles p;
  cmd(p, "set wifi.profile 1 ssid open");
  cmd(p, "set wifi.profile 1 pwd x");
  EXPECT_TRUE(p.hasPwd(1));
  EXPECT_EQ("> profile 1 pwd cleared (open network)", cmd(p, "set wifi.profile 1 pwd"));
  EXPECT_FALSE(p.hasPwd(1));
}

TEST(WifiProfileCli, ClearResetsEverythingIncludingThePriority) {
  WifiProfiles p;
  cmd(p, "set wifi.profile 3 ssid x");
  cmd(p, "set wifi.profile 3 pwd y");
  cmd(p, "set wifi.profile 3 priority 5");
  EXPECT_EQ("> profile 3 cleared", cmd(p, "set wifi.profile 3 clear"));
  EXPECT_FALSE(p.configured(3));
  EXPECT_FALSE(p.hasPwd(3));
  EXPECT_EQ(WifiProfiles::DEFAULT_PRIORITY, p.priority(3));
}

TEST(WifiProfileCli, RejectsBadInputAndChangesNothing) {
  WifiProfiles p;
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 10 ssid x").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 12 ssid x").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile x ssid x").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 ssid").find("ERR"));   // empty SSID: use clear
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 ssid 123456789012345678901234567890123").find("ERR"));   // 33 characters
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 pwd 01234567890123456789012345678901234567890123456789012345678901234").find("ERR"));   // 65
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 priority 256").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 priority abc").find("ERR"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 bogus").find("ERR"));
  EXPECT_EQ(0u, p.configuredCount());
  EXPECT_FALSE(p.hasPwd(1));
}

TEST(WifiProfileCli, AcceptsTheLongestSsidAndPassword) {
  WifiProfiles p;
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 ssid 12345678901234567890123456789012").find(">"));
  EXPECT_EQ(0u, cmd(p, "set wifi.profile 1 pwd 012345678901234567890123456789012345678901234567890123456789012").find(">"));
}

TEST(WifiProfileCli, ReportsWhatChanged) {
  WifiProfiles p;
  char reply[160];
  WifiProfileChange ch;
  ASSERT_TRUE(wifiProfileCommand(p, nullptr, "set wifi.profile 4 ssid x", reply, 150, ch));
  EXPECT_TRUE(ch.changed); EXPECT_EQ(4, ch.index); EXPECT_TRUE(ch.credentials);
  ASSERT_TRUE(wifiProfileCommand(p, nullptr, "set wifi.profile 4 priority 9", reply, 150, ch));
  EXPECT_TRUE(ch.changed); EXPECT_EQ(4, ch.index); EXPECT_FALSE(ch.credentials);
  ASSERT_TRUE(wifiProfileCommand(p, nullptr, "get wifi.profiles", reply, 150, ch));
  EXPECT_FALSE(ch.changed);
  ASSERT_TRUE(wifiProfileCommand(p, nullptr, "set wifi.profile 4 ssid", reply, 150, ch));
  EXPECT_FALSE(ch.changed);
}

TEST(WifiProfileCli, IgnoresOtherCommands) {
  WifiProfiles p;
  EXPECT_EQ("<unhandled>", cmd(p, "get wifi.ssid"));
  EXPECT_EQ("<unhandled>", cmd(p, "set wifi.pwd x"));
  EXPECT_EQ("<unhandled>", cmd(p, "get name"));
}

TEST(WifiProfileCli, ListingPagesThroughAllTenProfiles) {
  WifiProfiles p;
  for (uint8_t i = 0; i < 10; i++) {
    char c[96];
    snprintf(c, sizeof(c), "set wifi.profile %u ssid a-fairly-long-network-name-%u", i, i);
    cmd(p, c);
  }
  std::string seen;
  std::string c = "get wifi.profiles";
  for (int page = 0; page < 12; page++) {
    std::string r = cmd(p, c.c_str());
    ASSERT_LT(r.size(), 150u);
    seen += r;
    size_t n = r.find("... next:");
    if (n == std::string::npos) break;
    c = "get wifi.profiles " + r.substr(n + 9, 1);
  }
  for (uint8_t i = 0; i < 10; i++) {
    char needle[48];
    snprintf(needle, sizeof(needle), "network-name-%u ", i);
    EXPECT_NE(std::string::npos, seen.find(needle)) << int(i);
  }
}

TEST(WifiProfileCli, ListingShowsTheLastFailurePerProfile) {
  WifiProfiles p;
  cmd(p, "set wifi.profile 0 ssid home");
  cmd(p, "set wifi.profile 1 ssid phone");
  WifiCore core(&p);
  core.begin(0);
  core.scanObserve("home", -40);   // the scan has not started: ignored; drive it properly
  core.poll(0);                    // Scan
  core.scanObserve("home", -40);
  core.scanObserve("phone", -60);
  core.scanDone(500);
  core.poll(500);                  // Connect home
  core.onDisconnected(900, WifiDrop::AuthFailed);
  core.poll(900 + WifiTiming().join_drop_settle_ms);   // a drop while joining counts once no link has followed
  std::string l = cmd(p, "get wifi.profiles", &core);
  EXPECT_NE(std::string::npos, l.find("0: home prio=50 pwd=unset last=auth"));
  EXPECT_NE(std::string::npos, l.find("1: phone prio=50 pwd=unset last=none"));
}

TEST(WifiProfileCli, NoReplyEverCarriesAPassword) {
  const char* SECRET = "TopSecretPw-fake-0001";
  WifiProfiles p;
  p.build_ssid = "buildnet";
  p.build_pwd = "BuildSecret-fake-0002";
  WifiCore core(&p);
  const char* setup[] = {
    "set wifi.profile 0 ssid zero", "set wifi.profile 0 pwd TopSecretPw-fake-0001",
    "set wifi.profile 5 ssid five", "set wifi.profile 5 pwd TopSecretPw-fake-0001",
  };
  std::string all;
  for (auto s : setup) all += cmd(p, s, &core) + "\n";
  for (uint8_t i = 0; i < 10; i++) {
    char c[40];
    snprintf(c, sizeof(c), "get wifi.profile %u", i);
    all += cmd(p, c, &core) + "\n";
  }
  all += cmd(p, "get wifi.profiles", &core) + "\n";
  all += cmd(p, "get wifi.profiles 3", &core) + "\n";
  all += cmd(p, "set wifi.profile 5 pwd", &core) + "\n";
  all += cmd(p, "set wifi.profile 5 clear", &core) + "\n";
  all += cmd(p, "set wifi.profile 5 pwd way-too-long-0123456789012345678901234567890123456789012345678901234567890123456789", &core) + "\n";
  EXPECT_EQ(std::string::npos, all.find(SECRET));
  EXPECT_EQ(std::string::npos, all.find("BuildSecret"));
  EXPECT_EQ(std::string::npos, all.find("way-too-long"));
  EXPECT_NE(std::string::npos, all.find("pwd=set"));
}

// ---- the stored password is only ever in the prefs file

TEST(WifiProfiles, ProfileZeroFallsBackToTheBuildNetworkOnlyWhileNothingIsStored) {
  WifiProfiles p;
  p.build_ssid = "buildnet"; p.build_pwd = "buildpw-fake";
  EXPECT_STREQ("buildnet", p.ssid(0));
  EXPECT_STREQ("buildpw-fake", p.pwd(0));
  EXPECT_EQ(1u, p.configuredCount());
  p.setSsid(0, "mine");
  EXPECT_STREQ("mine", p.ssid(0));
  EXPECT_STREQ("", p.pwd(0));   // the build password does not leak into a user-chosen network
}

TEST(WifiProfiles, AtLeastThreeProfilesAreGuaranteed) {
  EXPECT_GE(WifiProfiles::COUNT, 3);
  EXPECT_EQ(10, WifiProfiles::COUNT);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
