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
    if (sscanf(l.c_str(), "set wifi.profile %u %15s", &n, word) == 2 && !strcmp(word, "clear")) {
      ssid[n].clear(); pwd[n].clear(); prio[n] = 0;
      r = "> profile " + std::to_string(n) + " cleared";
      return true;
    }
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
// The tests that are not about commit-or-revert run as the unsigned cable, which applies without confirmation.
bool cableYes(void*) { return true; }
const ConfirmHost CABLE{nullptr, nullptr, cableYes, nullptr, nullptr, nullptr, nullptr, nullptr};

const OpEntry OPS[] = {
  {"config.schema", opConfigSchema, PRIV_READ, false},
  {"config.get", opConfigGet, PRIV_READ, false},
  {"config.apply", opConfigApply, PRIV_WRITE, true},
};

struct Rig {
  FakeCli cli;
  ConfigHost host{runCb, allowCb, &cli, nullptr, 0, &CABLE};
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

// The retry protection must cover an ordinary apply: a few keys and a 32-character id. With a 128-byte cache entry a
// one-key apply's result was too big to keep, and its retry got result-expired instead of the result (v3test).
TEST(IfaceConfig, ARetryOfAnOrdinaryApplyGetsItsResultBack) {
  Rig r;
  const std::string id(32, 'j');
  std::string m = "{\"v\":1,\"id\":\"" + id + R"(","op":"config.apply","args":{"set":{"name":"Lab","tx":12,"wifi.profile.3.priority":42}}})";
  std::vector<char> out(2048);
  int n1 = r.d.handle(m.data(), m.size(), out.data(), out.size(), &r.cache);
  std::string first(out.data(), n1);
  int n2 = r.d.handle(m.data(), m.size(), out.data(), out.size(), &r.cache);
  EXPECT_EQ(first, std::string(out.data(), n2));
  EXPECT_NE(std::string::npos, first.find("\"complete\":true")) << first;
  EXPECT_EQ(3u, r.cli.ran.size()) << "three writes, once";
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

// ------------------------------------------------------------ the Repeater / Room server table

namespace {
// A fake of the server CLI's on/off and coordinate lines (CommonCLI).
struct ServerCli {
  std::string repeat = "on", lat = "0.0", loop = "minimal";
  std::vector<std::string> ran;
  bool run(const std::string& l, std::string& r) {
    ran.push_back(l);
    if (l == "get repeat") { r = "> " + repeat; return true; }
    if (l == "get lat") { r = "> " + lat; return true; }
    if (l == "get loop.detect") { r = "> " + loop; return true; }
    if (l.rfind("set repeat ", 0) == 0) { repeat = l.substr(11) == "off" ? "off" : "on"; r = "OK"; return true; }
    if (l.rfind("set lat ", 0) == 0) { lat = l.substr(8); r = "OK"; return true; }
    if (l.rfind("set loop.detect ", 0) == 0) { loop = l.substr(16); r = "OK"; return true; }
    return false;
  }
};
bool serverRun(void* u, const char* line, char* reply, size_t cap) {
  std::string r;
  if (!static_cast<ServerCli*>(u)->run(line, r)) return false;
  snprintf(reply, cap, "%s", r.c_str());
  return true;
}
bool serverAllow(void*, const char*) { return true; }

struct ServerRig {
  ServerCli cli;
  ConfigHost host{serverRun, serverAllow, &cli, SERVER_KEYS, N_SERVER_KEYS, &CABLE};
  Dispatcher d{OPS, 3, authAll, nullptr, &host};
  ResultCache cache;
  int n = 0;
  std::string call(const std::string& op, const std::string& args) {
    std::string m = "{\"v\":1,\"id\":\"s" + std::to_string(++n) + "\",\"op\":\"" + op + "\",\"args\":" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), &cache);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
};
}  // namespace

TEST(IfaceConfig, ServerTableHasItsOwnKeys) {
  unsigned i;
  EXPECT_NE(nullptr, findKey(SERVER_KEYS, N_SERVER_KEYS, "lat", 3, &i));
  EXPECT_NE(nullptr, findKey(SERVER_KEYS, N_SERVER_KEYS, "advert.interval", 15, &i));
  EXPECT_EQ(nullptr, findKey(SERVER_KEYS, N_SERVER_KEYS, "wifi.enabled", 12, &i));   // no Wi-Fi CLI on a server
  EXPECT_EQ(nullptr, findKey("lat", 3, &i));                                            // and the Companion's has no lat
}

TEST(IfaceConfig, ServerSchemaListsTheServerTable) {
  ServerRig r;
  std::string all;
  std::string next = "0";
  for (int page = 0; page < 10 && next != "null"; page++) {
    std::string res = r.call("config.schema", "{\"cursor\":" + next + "}");
    all += res;
    size_t at = res.find("\"next\":");
    ASSERT_NE(std::string::npos, at) << res;
    next = res.substr(at + 7, res.find_first_of(",}", at + 7) - at - 7);
  }
  EXPECT_NE(std::string::npos, all.find("\"key\":\"loop.detect\""));
  EXPECT_NE(std::string::npos, all.find("\"format\":\"latitude\""));
  EXPECT_EQ(std::string::npos, all.find("wifi.profile"));
  size_t listed = 0;
  for (size_t at = all.find("\"key\":"); at != std::string::npos; at = all.find("\"key\":", at + 1)) listed++;
  EXPECT_EQ(N_SERVER_KEYS, listed);   // exactly the server table: no key past its end
}

TEST(IfaceConfig, OnOffKeysReachTheCliAsOnOff) {
  ServerRig r;
  std::string res = r.call("config.apply", "{\"set\":{\"repeat\":false}}");
  EXPECT_NE(std::string::npos, res.find("\"status\":\"ok\"")) << res;
  EXPECT_EQ("off", r.cli.repeat);   // "set repeat 0" would have left it on
  bool sent = false;
  for (auto& l : r.cli.ran) sent = sent || l == "set repeat off";
  EXPECT_TRUE(sent);
  res = r.call("config.get", "{\"keys\":[\"repeat\",\"loop.detect\"]}");
  EXPECT_NE(std::string::npos, res.find("\"repeat\":false")) << res;
  EXPECT_NE(std::string::npos, res.find("\"loop.detect\":\"minimal\"")) << res;
}

TEST(IfaceConfig, CoordinatesAreChecked) {
  EXPECT_TRUE(checkFormat("latitude", "56.1572"));
  EXPECT_TRUE(checkFormat("latitude", "-33.9"));
  EXPECT_TRUE(checkFormat("longitude", "-179.5"));
  EXPECT_FALSE(checkFormat("latitude", "91"));
  EXPECT_FALSE(checkFormat("longitude", "180.5"));
  EXPECT_FALSE(checkFormat("latitude", "56,15"));
  EXPECT_FALSE(checkFormat("latitude", "--1"));
  EXPECT_FALSE(checkFormat("latitude", ""));
  ServerRig r;
  std::string res = r.call("config.apply", "{\"set\":{\"lat\":\"abc\"}}");
  EXPECT_NE(std::string::npos, res.find("bad-format:lat")) << res;
  EXPECT_EQ("0.0", r.cli.lat);
}

// The Companion's one on/off key keeps its word: a broker profile's tls goes to the CLI as "on", never "1".
TEST(IfaceConfig, CompanionTlsStaysOnOff) {
  Rig r;
  r.call("config.apply", "{\"set\":{\"mqtt.profile.0.tls\":true}}");
  bool on = false, one = false;
  for (auto& l : r.cli.ran) {
    on = on || l == "set mqtt.profile 0 tls on";
    one = one || l == "set mqtt.profile 0 tls 1";
  }
  EXPECT_TRUE(on);
  EXPECT_FALSE(one);
}

// ---- commit-or-revert (OPERATIONS.md section 3.4): the snapshot, the job, the revert as the caller

namespace {

struct RamJobs : JobStore {
  std::vector<uint8_t> f;
  size_t load(uint8_t* b, size_t cap) override { if (f.empty() || f.size() > cap) return 0; memcpy(b, f.data(), f.size()); return f.size(); }
  bool save(const uint8_t* b, size_t n) override { f.assign(b, b + n); return true; }
};
struct RamSnap : SnapStore {
  std::vector<uint8_t> f;
  bool fail = false;
  int discards = 0;
  size_t load(uint8_t* b, size_t cap) override { if (f.empty() || f.size() > cap) return 0; memcpy(b, f.data(), f.size()); return f.size(); }
  bool save(const uint8_t* b, size_t n) override { if (fail) return false; f.assign(b, b + n); return true; }
  void discard() override { f.clear(); discards++; }
  std::string text() const { return std::string(f.begin(), f.end()); }
};

struct ConfirmRig {
  FakeCli cli;
  bool is_cable = false;
  std::vector<std::string> ran_as;   // the lines a revert ran, as the recorded caller
  uint8_t recorded = 0;
  RamJobs jf;
  RamSnap sf;
  JobService jobs;
  ConfirmHost ch{&jobs, &sf, cable, lineClass, readSecret, record, runAs, jobId};
  ConfigHost host{rigRun, rigAllow, this, nullptr, 0, &ch};
  Dispatcher d{OPS, 3, authAll, nullptr, &host};
  int n = 0;
  ConfirmRig() { ch.jobs = &jobs; jobs.begin(&jf, false, nullptr, nullptr, nullptr); }
  std::string call(const std::string& args, const std::string& id = "") {
    const std::string rid = id.empty() ? "c" + std::to_string(++n) : id;
    std::string m = "{\"v\":1,\"id\":\"" + rid + R"(","op":"config.apply","args":)" + args + "}";
    std::vector<char> out(2048);
    int len = d.handle(m.data(), m.size(), out.data(), out.size(), nullptr);
    return len < 0 ? "<overflow>" : std::string(out.data(), len);
  }
  static ConfirmRig* of(void* u) { return static_cast<ConfirmRig*>(u); }
  static bool rigRun(void* u, const char* line, char* reply, size_t cap) { return runCb(&of(u)->cli, line, reply, cap); }
  static bool rigAllow(void* u, const char* line) { return allowCb(&of(u)->cli, line); }
  static bool cable(void* u) { return of(u)->is_cable; }
  static uint8_t lineClass(void*, const char* line) { return strncmp(line, "set wifi.enabled", 16) == 0 ? 3 : 1; }
  static bool readSecret(void* u, const KeyDef& k, unsigned idx, char* out, size_t cap) {
    if (strcmp(k.key, "wifi.profile.{n}.pwd") != 0) return false;
    snprintf(out, cap, "%s", of(u)->cli.pwd[idx].c_str());
    return true;
  }
  static void record(void* u, Job& j) { j.who_audit = 7; of(u)->recorded++; }
  static bool runAs(void* u, const Job& j, const char* line, char* reply, size_t cap) {
    ConfirmRig* r = of(u);
    if (j.who_audit != 7) return false;   // only as the caller the job recorded
    r->ran_as.push_back(line);
    return runCb(&r->cli, line, reply, cap);
  }
  static bool jobId(void*, const Request& req, char* out, size_t cap) { return JobService::identity(nullptr, req, out, cap); }
  Job* job(const char* id) { Job* j = nullptr; return jobs.table()->find(id, &j) == JobTable::FOUND ? j : nullptr; }
};

bool has(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

}  // namespace

TEST(ConfigConfirm, ARemoteCallerMustAskToConfirmAPathKey) {
  ConfirmRig r;
  const std::string res = r.call(R"({"set":{"wifi.profile.1.ssid":"new"}})");
  EXPECT_TRUE(has(res, R"("status":"invalid","error":"confirm-required")")) << res;
  EXPECT_TRUE(r.cli.ran.empty()) << "nothing read or written";
  EXPECT_TRUE(has(r.call(R"({"set":{"name":"x"}})"), R"("status":"ok")")) << "an ordinary key needs nothing";
  EXPECT_TRUE(has(r.call(R"({"set":{"name":"y"},"confirm":{}})"), R"("error":"confirm-not-needed")"));
  r.is_cable = true;
  EXPECT_TRUE(has(r.call(R"({"set":{"wifi.profile.1.ssid":"cable"}})"), R"("complete":true)")) << "the cable keeps its path";
  EXPECT_EQ("cable", r.cli.ssid[1]);
  EXPECT_TRUE(r.sf.f.empty()) << "a plain apply leaves no snapshot";
}

TEST(ConfigConfirm, AConfirmedChangeStaysAndItsSnapshotGoes) {
  ConfirmRig r;
  r.cli.ssid[1] = "office"; r.cli.pwd[1] = "old-fake-pw"; r.cli.prio[1] = 40;
  const std::string res = r.call(R"({"set":{"wifi.profile.1.ssid":"hotel","wifi.profile.1.pwd":"new-fake-pw"},"confirm":{"within_s":60}})", "a1");
  EXPECT_TRUE(has(res, R"("complete":true,"reboot_needed":false,"restarting":false,"job":"a1","state":"awaiting-confirm","confirm_left_s":60)")) << res;
  EXPECT_FALSE(has(res, "old-fake-pw")) << "never a previous value in a result";
  EXPECT_EQ("hotel", r.cli.ssid[1]);
  EXPECT_TRUE(has(r.sf.text(), "office") && has(r.sf.text(), "old-fake-pw")) << "the snapshot holds the whole profile";
  Job* j = r.job("a1");
  ASSERT_NE(nullptr, j);
  EXPECT_EQ(JOB_AWAITING, j->state);
  EXPECT_EQ(1, r.recorded);
  EXPECT_EQ(1u << 1, j->cls) << "every class the lines needed";
  EXPECT_FALSE(r.jf.f.empty()) << "the job is saved";
  configConfirmed(r.host);
  EXPECT_TRUE(r.sf.f.empty());
  EXPECT_EQ("hotel", r.cli.ssid[1]);
}

TEST(ConfigConfirm, ARevertPutsTheWholeProfileBackAsTheCaller) {
  ConfirmRig r;
  r.cli.ssid[1] = "office"; r.cli.pwd[1] = "old-fake-pw"; r.cli.prio[1] = 40;
  r.call(R"({"set":{"wifi.profile.1.ssid":"hotel","wifi.profile.1.priority":90},"confirm":{}})", "b1");
  ASSERT_EQ("hotel", r.cli.ssid[1]);
  bool reboot = true;
  ASSERT_TRUE(configRevert(r.host, *r.job("b1"), &reboot));
  EXPECT_FALSE(reboot);
  EXPECT_EQ("office", r.cli.ssid[1]);
  EXPECT_EQ("old-fake-pw", r.cli.pwd[1]);
  EXPECT_EQ(40, r.cli.prio[1]);
  ASSERT_EQ(3u, r.ran_as.size());
  EXPECT_EQ("set wifi.profile 1 ssid office", r.ran_as[0]);
  EXPECT_TRUE(r.sf.f.empty()) << "the snapshot goes with the revert";
}

TEST(ConfigConfirm, ANewProfileIsRevertedByClearingIt) {
  ConfirmRig r;
  r.call(R"({"set":{"wifi.profile.4.ssid":"guest","wifi.profile.4.pwd":"fake-pw-x"},"confirm":{}})", "n1");
  ASSERT_EQ("guest", r.cli.ssid[4]);
  bool reboot;
  ASSERT_TRUE(configRevert(r.host, *r.job("n1"), &reboot));
  ASSERT_EQ(1u, r.ran_as.size());
  EXPECT_EQ("set wifi.profile 4 clear", r.ran_as[0]);
  EXPECT_TRUE(r.cli.ssid[4].empty() && r.cli.pwd[4].empty());
}

TEST(ConfigConfirm, APreviousValueNoLineMayCarryRefusesTheRequest) {
  ConfirmRig r;
  r.cli.ssid[2] = "lab\nset posture hardened";   // set through another route
  r.cli.prio[2] = 10;
  const std::string res = r.call(R"({"set":{"wifi.profile.2.priority":20},"confirm":{}})");
  EXPECT_TRUE(has(res, R"("status":"busy","error":"storage")")) << res;
  EXPECT_EQ(10, r.cli.prio[2]) << "nothing written";
  EXPECT_TRUE(r.sf.f.empty());
  EXPECT_EQ(nullptr, r.jobs.table()->openJob());
}

TEST(ConfigConfirm, ASnapshotOfAnotherJobIsNeverReplayed) {
  ConfirmRig r;
  r.cli.ssid[1] = "office";
  r.call(R"({"set":{"wifi.profile.1.ssid":"x"},"confirm":{}})", "own");
  ConfigSnap other;   // a well-formed snapshot a revert could replay, but of another job
  other.begin("other");
  unsigned idx;
  const KeyDef* tx = findKey("tx", 2, &idx);
  ASSERT_TRUE(other.add(ConfigSnap::R_SET, (uint8_t)(tx - KEYS), 0, "5"));
  std::vector<uint8_t> img(ConfigSnap::IMAGE_MAX);
  img.resize(other.serialize(img.data(), img.size()));
  r.sf.f = img;
  bool reboot;
  EXPECT_FALSE(configRevert(r.host, *r.job("own"), &reboot));
  EXPECT_TRUE(r.ran_as.empty());
  EXPECT_EQ(20, r.cli.tx);
  EXPECT_TRUE(r.sf.f.empty()) << "and it is removed";
}

TEST(ConfigConfirm, ARecordForAKeyARevertMayNotSetIsRefused) {
  ConfirmRig r;
  r.call(R"({"set":{"wifi.profile.1.ssid":"x"},"confirm":{}})", "k1");
  ConfigSnap s;
  s.begin("k1");
  unsigned idx;
  const KeyDef* name = findKey("name", 4, &idx);
  ASSERT_TRUE(s.add(ConfigSnap::R_SET, (uint8_t)(name - KEYS), 0, "Evil"));   // name is not a path key
  std::vector<uint8_t> img(ConfigSnap::IMAGE_MAX);
  img.resize(s.serialize(img.data(), img.size()));
  r.sf.f = img;
  bool reboot;
  EXPECT_FALSE(configRevert(r.host, *r.job("k1"), &reboot));
  EXPECT_EQ("Node1", r.cli.name);
}

TEST(ConfigConfirm, OneOpenJobAtATimeAndARetryRunsNothing) {
  ConfirmRig r;
  r.cli.ssid[1] = "office";
  r.call(R"({"set":{"wifi.profile.1.ssid":"a"},"confirm":{}})", "o1");
  const size_t ran = r.cli.ran.size();
  EXPECT_TRUE(has(r.call(R"({"set":{"wifi.profile.1.ssid":"a"},"confirm":{}})", "o1"), R"("job":"o1","op":"config.apply","state":"awaiting-confirm")"));
  EXPECT_EQ(ran, r.cli.ran.size()) << "the same job, never a second run";
  EXPECT_TRUE(has(r.call(R"({"set":{"wifi.profile.1.ssid":"b"},"confirm":{}})", "o2"), R"("status":"busy","error":"job-open:o1")"));
}

TEST(ConfigConfirm, AWriteThatFailsRevertsTheOthersAtOnce) {
  ConfirmRig r;
  r.cli.ssid[1] = "office"; r.cli.prio[1] = 40;
  r.cli.fail = {"set wifi.profile 1 priority 99"};
  const std::string res = r.call(R"({"set":{"wifi.profile.1.ssid":"hotel","wifi.profile.1.priority":99},"confirm":{}})", "f1");
  EXPECT_TRUE(has(res, R"("complete":false)") && has(res, R"("state":"failed","reverted":true)")) << res;
  EXPECT_EQ("office", r.cli.ssid[1]);
  EXPECT_EQ(JOB_FAILED, r.job("f1")->state);
  EXPECT_TRUE(r.sf.f.empty());
}

TEST(ConfigConfirm, ASnapshotThatCannotBeSavedWritesNothing) {
  ConfirmRig r;
  r.cli.ssid[1] = "office";
  r.sf.fail = true;
  EXPECT_TRUE(has(r.call(R"({"set":{"wifi.profile.1.ssid":"x"},"confirm":{}})"), R"("status":"busy","error":"storage")"));
  EXPECT_EQ("office", r.cli.ssid[1]);
  EXPECT_EQ(nullptr, r.jobs.table()->openJob());
}

TEST(ConfigConfirm, AtBootAnOrphanSnapshotGoes) {
  ConfirmRig r;
  r.sf.f = {1, 2, 3};
  configSnapBoot(r.host);
  EXPECT_TRUE(r.sf.f.empty());
  r.cli.ssid[1] = "office";
  r.call(R"({"set":{"wifi.profile.1.ssid":"x"},"confirm":{}})", "keep");
  const auto kept = r.sf.f;
  configSnapBoot(r.host);
  EXPECT_EQ(kept, r.sf.f) << "the awaiting job's own snapshot stays";
}

TEST(ConfigSnapImage, ADamagedOrForeignImageReadsAsNone) {
  ConfigSnap s;
  s.begin("j");
  ASSERT_TRUE(s.add(ConfigSnap::R_SET, 3, 1, "abc"));
  EXPECT_FALSE(s.add(ConfigSnap::R_SET, 3, 1, "a\rb")) << "no control byte";
  EXPECT_FALSE(s.add(ConfigSnap::R_SET, 3, 1, "a\x7f")) << "no DEL";
  std::vector<uint8_t> img(ConfigSnap::IMAGE_MAX);
  img.resize(s.serialize(img.data(), img.size()));
  ConfigSnap t;
  ASSERT_TRUE(t.parse(img.data(), img.size()));
  EXPECT_STREQ("j", t.job());
  for (size_t at : {(size_t)0, (size_t)5, img.size() / 2, img.size() - 1}) {
    std::vector<uint8_t> bad = img;
    bad[at] ^= 0x10;
    EXPECT_FALSE(t.parse(bad.data(), bad.size())) << at;
    EXPECT_EQ(0, t.count());
  }
  EXPECT_FALSE(t.parse(img.data(), img.size() - 1));
}
