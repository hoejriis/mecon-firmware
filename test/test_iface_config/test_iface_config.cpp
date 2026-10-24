// config.schema / config.get / config.apply over a fake CLI that answers like the Companion's
// (src/helpers/iface/IfaceConfig.cpp, docs/contract/DEVICE_INTERFACE.md section 5).
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include <helpers/iface/IfaceConfig.h>

using namespace Iface;

namespace {

struct FakeCli {
  std::string name = "Node1";
  int tx = 20;
  std::string ssid[10], pwd[10];
  int prio[10] = {0};
  bool wifi_on = true;
  std::vector<std::string> ran;            // every line run, in order
  std::vector<std::string> refuse;         // lines the caller may not run (by prefix)
  std::vector<std::string> fail;           // lines that fail when run (by prefix)

  bool refused(const std::string& l) const {
    for (auto& p : refuse) if (l.compare(0, p.size(), p) == 0) return true;
    return false;
  }
  bool run(const std::string& l, std::string& r) {
    ran.push_back(l);
    for (auto& p : fail) if (l.compare(0, p.size(), p) == 0) { r = "ERR: cannot"; return true; }
    unsigned n;
    char word[16], rest[96];
    if (l == "get name") { r = "> " + name; return true; }
    if (l == "get tx") { r = "> " + std::to_string(tx); return true; }
    if (l == "get wifi.enabled") { r = std::string("> ") + (wifi_on ? "1" : "0"); return true; }
    if (sscanf(l.c_str(), "get wifi.profile %u", &n) == 1) {
      if (ssid[n].empty() && pwd[n].empty()) { r = "> " + std::to_string(n) + ": (not set)"; return true; }
      r = "> " + std::to_string(n) + ": " + ssid[n] + " prio=" + std::to_string(prio[n]) + " pwd=" + (pwd[n].empty() ? "unset" : "set") + " last=none";
      return true;
    }
    if (l.rfind("set name ", 0) == 0) { name = l.substr(9); r = "OK"; return true; }
    if (l.rfind("set tx ", 0) == 0) { tx = atoi(l.c_str() + 7); r = "OK"; return true; }
    if (l.rfind("set wifi.enabled ", 0) == 0) { wifi_on = l.back() == '1'; r = "> wifi.enabled is now 1"; return true; }
    if (sscanf(l.c_str(), "set wifi.profile %u %15s", &n, word) == 2) {
      const char* v = strstr(l.c_str(), word) + strlen(word) + 1;
      if (!strcmp(word, "ssid")) ssid[n] = v;
      else if (!strcmp(word, "pwd")) pwd[n] = v;
      else if (!strcmp(word, "priority")) prio[n] = atoi(v);
      r = "> profile " + std::to_string(n) + " updated";
      (void)rest;
      return true;
    }
    return false;
  }
};

bool runCb(void* u, const char* line, char* reply, size_t cap) {
  std::string r;
  if (!static_cast<FakeCli*>(u)->run(line, r)) return false;
  snprintf(reply, cap, "%s", r.c_str());
  return true;
}
bool allowCb(void* u, const char* line) { return !static_cast<FakeCli*>(u)->refused(line); }
bool authAll(void*, const OpEntry&, const Request&) { return true; }

const OpEntry OPS[] = {
  {"config.schema", opConfigSchema, PRIV_READ, false},
  {"config.get", opConfigGet, PRIV_READ, false},
  {"config.apply", opConfigApply, PRIV_WRITE, true},
};

struct Rig {
  FakeCli cli;
  ConfigHost host{runCb, allowCb, &cli};
  Dispatcher d{OPS, 3, authAll, nullptr, &host};
  ResultCache cache;
  std::string call(const std::string& op, const std::string& args) {
    std::string m = "{\"v\":1,\"id\":\"t" + std::to_string(++n) + "\",\"op\":\"" + op + "\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  int n = 0;
};

}  // namespace

TEST(IfaceConfig, KeysWithAnIndexAreZeroBasedAndBounded) {
  unsigned i;
  ASSERT_NE(nullptr, findKey("wifi.profile.0.ssid", 19, &i)); EXPECT_EQ(0u, i);
  ASSERT_NE(nullptr, findKey("wifi.profile.9.pwd", 18, &i)); EXPECT_EQ(9u, i);
  EXPECT_EQ(nullptr, findKey("wifi.profile.10.ssid", 20, &i));   // 10 profiles: 0 to 9
  EXPECT_EQ(nullptr, findKey("wifi.profile.01.ssid", 20, &i));   // one spelling per key
  EXPECT_EQ(nullptr, findKey("wifi.profile..ssid", 18, &i));
  ASSERT_NE(nullptr, findKey("mqtt.profile.5.host", 19, &i));
  EXPECT_EQ(nullptr, findKey("mqtt.profile.6.host", 19, &i));    // 6 broker profiles
  EXPECT_EQ(nullptr, findKey("lat", 3, &i));                      // the Companion CLI has no line for it
}

TEST(IfaceConfig, RadioMustHaveItsFourFields) {
  EXPECT_TRUE(checkFormat("freq,bw,sf,cr", "869.618,62.5,8,8"));
  EXPECT_TRUE(checkFormat("freq,bw,sf,cr", "869.618,125,8,8"));     // an integral value without a decimal point
  EXPECT_TRUE(checkFormat("freq,bw,sf,cr", "869.525,250.0,11,5"));
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.618,125,8"));      // three fields
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.618,125,8,8,1"));
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.618,125,13,8"));   // SF past 12
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.618,125,8,9"));    // CR past 8
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "-869,125,8,8"));
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.,125,8,8"));
  EXPECT_FALSE(checkFormat("freq,bw,sf,cr", "869.618, 125,8,8"));
}

TEST(IfaceConfig, ParsesTheProfileLines) {
  char v[64]; bool null;
  ASSERT_TRUE(parseField(F_WIFI_SSID, "> 2: lab net prio=70 pwd=set last=none", v, sizeof v, &null));
  EXPECT_STREQ("lab net", v);   // an SSID may hold spaces
  ASSERT_TRUE(parseField(F_WIFI_PRIO, "> 2: lab net prio=70 pwd=set last=none", v, sizeof v, &null)); EXPECT_STREQ("70", v);
  ASSERT_TRUE(parseField(F_WIFI_PWD, "> 2: lab net prio=70 pwd=set last=none", v, sizeof v, &null)); EXPECT_STREQ("set", v);
  ASSERT_TRUE(parseField(F_WIFI_PRIO, "> 5: (not set)", v, sizeof v, &null)); EXPECT_TRUE(null);
  ASSERT_TRUE(parseField(F_WIFI_PWD, "> 5: (not set)", v, sizeof v, &null)); EXPECT_FALSE(null); EXPECT_STREQ("unset", v);
  const char* st = "> 1: static broker.example:8883 tls=on anchor=unset prio=50 user=set pwd=unset last=none";
  ASSERT_TRUE(parseField(F_MQTT_HOST, st, v, sizeof v, &null)); EXPECT_STREQ("broker.example", v);
  ASSERT_TRUE(parseField(F_MQTT_PORT, st, v, sizeof v, &null)); EXPECT_STREQ("8883", v);
  ASSERT_TRUE(parseField(F_MQTT_USER, st, v, sizeof v, &null)); EXPECT_STREQ("set", v);
  ASSERT_TRUE(parseField(F_MQTT_SITE, st, v, sizeof v, &null)); EXPECT_TRUE(null);   // a static profile has no site
  const char* dy = "> 2: discovery site=home anchor=ca prio=40 user=unset pwd=unset last=none";
  ASSERT_TRUE(parseField(F_MQTT_SITE, dy, v, sizeof v, &null)); EXPECT_STREQ("home", v);
  ASSERT_TRUE(parseField(F_MQTT_TLS, dy, v, sizeof v, &null)); EXPECT_STREQ("on", v);   // discovery is always TLS
  ASSERT_TRUE(parseField(F_MQTT_HOST, dy, v, sizeof v, &null)); EXPECT_TRUE(null);
  EXPECT_FALSE(parseField(F_WHOLE, "ERR: refused", v, sizeof v, &null));
}

TEST(IfaceConfig, SchemaIsPagedAndEndsWithNullNext) {
  Rig r;
  size_t seen = 0;
  std::string cursor = "0";
  for (int page = 0; page < 10; page++) {
    std::string res = r.call("config.schema", "{\"cursor\":" + cursor + "}");
    ASSERT_NE(std::string::npos, res.find("\"status\":\"ok\"")) << res;
    for (size_t p = 0; (p = res.find("\"key\":", p)) != std::string::npos; p++) seen++;
    size_t nx = res.find("\"next\":");
    std::string next = res.substr(nx + 7, res.find_first_of(",}", nx) - nx - 7);
    if (next == "null") break;
    cursor = next;
  }
  EXPECT_EQ(NKEYS, seen);
}

TEST(IfaceConfig, GetReadsThroughTheCliAndNeverReturnsASecret) {
  Rig r;
  r.cli.ssid[1] = "office"; r.cli.pwd[1] = "TopSecret-fake"; r.cli.prio[1] = 40;
  std::string res = r.call("config.get", R"({"keys":["name","tx","wifi.enabled","wifi.profile.1.ssid","wifi.profile.1.pwd","wifi.profile.1.priority","wifi.profile.3.priority"]})");
  EXPECT_NE(std::string::npos, res.find(R"("values":{"name":"Node1","tx":20,"wifi.enabled":true,"wifi.profile.1.ssid":"office","wifi.profile.1.pwd":"set","wifi.profile.1.priority":40,"wifi.profile.3.priority":null})")) << res;
  EXPECT_EQ(std::string::npos, res.find("TopSecret")) << res;
}

TEST(IfaceConfig, GetReportsEachKeyItCouldNotRead) {
  Rig r;
  r.cli.refuse = {"get tx"};
  std::string res = r.call("config.get", R"({"keys":["name","tx","lat"]})");
  EXPECT_NE(std::string::npos, res.find(R"("values":{"name":"Node1"},"errors":{"tx":"refused","lat":"unknown-key"})")) << res;
  for (auto& l : r.cli.ran) EXPECT_NE("get tx", l) << "a refused read is not run";
}

TEST(IfaceConfig, ApplyWritesEveryKeyThroughItsCliLine) {
  Rig r;
  std::string res = r.call("config.apply", R"({"set":{"name":"Lab node","wifi.profile.2.ssid":"lab net","wifi.profile.2.pwd":"pw-fake-9","wifi.profile.2.priority":70,"wifi.enabled":false}})");
  EXPECT_NE(std::string::npos, res.find(R"("applied":{"name":"ok","wifi.profile.2.ssid":"ok","wifi.profile.2.pwd":"ok","wifi.profile.2.priority":"ok","wifi.enabled":"ok"},"complete":true,"reboot_needed":true,"restarting":false)")) << res;
  EXPECT_EQ("Lab node", r.cli.name);
  EXPECT_EQ("lab net", r.cli.ssid[2]);
  EXPECT_EQ("pw-fake-9", r.cli.pwd[2]);
  EXPECT_EQ(70, r.cli.prio[2]);
  EXPECT_FALSE(r.cli.wifi_on);
  EXPECT_EQ(std::string::npos, res.find("pw-fake-9")) << "the result never quotes a value";
}

// The whole request is checked before anything is written: one bad key, value or refused line, and nothing changes.
TEST(IfaceConfig, ApplyWritesNothingUnlessEveryKeyPassesItsChecks) {
  struct { const char* args; const char* status; const char* error; } cases[] = {
    {R"({"set":{"name":"New","tx":99}})", "invalid", "out-of-range:tx"},
    {R"({"set":{"name":"New","lat":55}})", "invalid", "unknown-key:lat"},
    {R"({"set":{"name":"New","wifi.enabled":"yes"}})", "invalid", "not-a-boolean:wifi.enabled"},
    {R"({"set":{"name":""}})", "invalid", "bad-length:name"},
    {R"({"set":{"name":"New","connectivity.mode":"lora"}})", "invalid", "not-a-choice:connectivity.mode"},
    {R"({"set":{"name":"New","wifi.profile.12.ssid":"x"}})", "invalid", "unknown-key:wifi.profile.12.ssid"},
    {R"({"set":{"name":"New","radio":"869.618,125,8"}})", "invalid", "bad-format:radio"},
  };
  for (auto& c : cases) {
    Rig r;
    std::string res = r.call("config.apply", c.args);
    EXPECT_NE(std::string::npos, res.find(std::string("\"status\":\"") + c.status + "\"")) << c.args << " " << res;
    EXPECT_NE(std::string::npos, res.find(std::string("\"error\":\"") + c.error + "\"")) << c.args << " " << res;
    EXPECT_EQ("Node1", r.cli.name) << c.args;
    EXPECT_TRUE(r.cli.ran.empty()) << c.args;
  }
  Rig r;
  r.cli.refuse = {"set wifi.profile"};
  std::string res = r.call("config.apply", R"({"set":{"name":"New","wifi.profile.0.ssid":"x"}})");
  EXPECT_NE(std::string::npos, res.find(R"("status":"refused","error":"refused:wifi.profile.0.ssid")")) << res;
  EXPECT_EQ("Node1", r.cli.name);
}

TEST(IfaceConfig, AWriteThatFailsAfterTheChecksIsReportedAndTheOthersStand) {
  Rig r;
  r.cli.fail = {"set tx"};
  std::string res = r.call("config.apply", R"({"set":{"name":"New","tx":10}})");
  EXPECT_NE(std::string::npos, res.find(R"("status":"ok")")) << res;
  EXPECT_NE(std::string::npos, res.find(R"("applied":{"name":"ok","tx":"failed"},"complete":false)")) << res;
  EXPECT_EQ("New", r.cli.name);
}

TEST(IfaceConfig, ARetriedApplyIsNotWrittenTwice) {
  Rig r;
  std::string m = R"({"v":1,"id":"same","op":"config.apply","args":{"set":{"name":"Once"}}})";
  std::vector<char> out(2048);
  r.d.handle(m.data(), m.size(), out.data(), out.size(), &r.cache);
  r.cli.name = "Changed meanwhile";
  r.d.handle(m.data(), m.size(), out.data(), out.size(), &r.cache);
  EXPECT_EQ("Changed meanwhile", r.cli.name);
  EXPECT_EQ(1u, r.cli.ran.size());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
