// Standing instructions (src/helpers/iface/StandingOps.h) through the real dispatcher: when a run falls due. A loop takes
// its time once and handles a request later in the same pass, so a put can be stamped after the time the next tick is given.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/IfaceDispatch.h"
#include "helpers/iface/StandingOps.h"
#include "helpers/iface/KeptReports.h"
#include <map>

using namespace Iface;

struct RamStore : SnapStore {
  std::vector<uint8_t> file;
  size_t load(uint8_t* buf, size_t cap) override {
    if (file.empty() || file.size() > cap) return 0;
    memcpy(buf, file.data(), file.size());
    return file.size();
  }
  bool save(const uint8_t* buf, size_t len) override { file.assign(buf, buf + len); return true; }
  void discard() override { file.clear(); }
};

static int g_sends = 0;
static bool hSend(void*, uint8_t, const uint8_t*, int, uint32_t* seq, uint8_t*, size_t*) { g_sends++; *seq = g_sends; return true; }
static uint8_t hProduced(void*) { return RF_HEALTH | RF_RADIO | RF_COUNTERS; }
static bool hNo(void*) { return false; }
static const char* hIssuer(void*) { return ""; }
static bool hValid(void*, const char*) { return true; }
static uint32_t hRandom(void*) { return 0; }   // no jitter: a run is due exactly one period on
static bool hYes(void*) { return true; }

static StandingService* S(OpContext& c) { return static_cast<StandingService*>(c.user); }
static uint32_t g_put_at = 0;
static Status oPut(OpContext& c) { return S(c)->opPut(c, g_put_at); }
static Status oList(OpContext& c) { return S(c)->opList(c); }
static bool allow(void*, const OpEntry&, const Request&) { return true; }
static const OpEntry OPS[] = {{"standing.put", oPut, PRIV_WRITE, true}, {"standing.list", oList, PRIV_READ, false}};

static std::string call(StandingService& s, const std::string& op, const std::string& args = "") {
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), allow, nullptr, &s);
  const std::string m = R"({"v":1,"id":"q","op":")" + op + "\"" + (args.empty() ? "" : R"(,"args":)" + args) + "}";
  std::vector<char> out(800);
  const int n = d.handle(m.data(), m.size(), out.data(), out.size(), nullptr);
  return n < 0 ? "<overflow>" : std::string(out.data(), n);
}

static const char* PUT = R"({"id":"h","fields":["health"],"every_s":900,"to":"abababababababababababababababababababababababababababababababab"})";

struct StandingTest : ::testing::Test {
  RamStore store;
  StandingService s;
  void SetUp() override {
    g_sends = 0;
    s.begin(ReportHost{hSend, hProduced, hNo, hIssuer, hValid, hRandom, hYes, &store, nullptr}, 1000);
  }
};

TEST_F(StandingTest, APutStampedAfterTheLoopsTimeDoesNotRunAtOnce) {
  // The loop read 5000; the request it handled in that pass read 5040. The tick that follows is given 5000.
  g_put_at = 5040;
  ASSERT_NE(call(s, "standing.put", PUT).find("\"ok\""), std::string::npos);
  s.tick(5000);
  s.tick(5040);
  s.tick(5100);
  EXPECT_EQ(g_sends, 0) << "a put runs one period on, not in the pass that took it";
  s.tick(5040 + 900000 - 1000);
  EXPECT_EQ(g_sends, 0);
  s.tick(5100 + 900000);
  EXPECT_EQ(g_sends, 1) << "and one period on it runs";
}

TEST_F(StandingTest, APutStampedWithTheLoopsTimeRunsOnePeriodOn) {
  g_put_at = 5000;
  ASSERT_NE(call(s, "standing.put", PUT).find("\"ok\""), std::string::npos);
  s.tick(5000);
  EXPECT_EQ(g_sends, 0);
  s.tick(5000 + 900000 - 1);
  EXPECT_EQ(g_sends, 0);
  s.tick(5000 + 900000);
  EXPECT_EQ(g_sends, 1);
  s.tick(5000 + 900000 + 1);
  EXPECT_EQ(g_sends, 1) << "the next is a period after that";
}

TEST_F(StandingTest, ABoundedInstructionKeepsItsCountAcrossAReboot) {
  g_put_at = 5000;
  const std::string put = R"({"id":"h","fields":["health"],"every_s":900,"runs":2,"to":"abababababababababababababababababababababababababababababababab"})";
  ASSERT_NE(call(s, "standing.put", put).find("\"ok\""), std::string::npos);
  s.tick(5000);
  s.tick(5000 + 900000);
  ASSERT_EQ(g_sends, 1) << "the first of two";
  StandingService b;   // a reboot: the same store, a new service
  b.begin(ReportHost{hSend, hProduced, hNo, hIssuer, hValid, hRandom, hYes, &store, nullptr}, 1000);
  b.tick(1000);   // no jitter here: a boot's first run is due at once
  EXPECT_EQ(g_sends, 2) << "the second";
  EXPECT_EQ(call(b, "standing.list").find("\"h\""), std::string::npos) << "and then it is gone: " << call(b, "standing.list");
  StandingService c;   // another reboot finds nothing left to run
  c.begin(ReportHost{hSend, hProduced, hNo, hIssuer, hValid, hRandom, hYes, &store, nullptr}, 1000);
  c.tick(1000);
  c.tick(1000 + 900000);
  EXPECT_EQ(g_sends, 2) << "no third run";
}

TEST_F(StandingTest, TheCountersAreNamedAndKeptWithTheOtherFields) {
  g_put_at = 5000;
  const std::string put = R"({"id":"c","fields":["radio","counters"],"every_s":900,"to":"abababababababababababababababababababababababababababababababab"})";
  ASSERT_NE(call(s, "standing.put", put).find("\"ok\""), std::string::npos) << call(s, "standing.put", put);
  const std::string list = call(s, "standing.list");
  EXPECT_NE(list.find(R"("fields":["radio","counters"])"), std::string::npos) << list;
  StandingService b;   // after a reboot: the bit is kept as stored
  b.begin(ReportHost{hSend, hProduced, hNo, hIssuer, hValid, hRandom, hYes, &store, nullptr}, 1000);
  EXPECT_NE(call(b, "standing.list").find(R"("fields":["radio","counters"])"), std::string::npos) << call(b, "standing.list");
  const std::string four = R"({"id":"d","fields":["health","radio","location","counters"],"every_s":900,"to":"abababababababababababababababababababababababababababababababab"})";
  EXPECT_EQ(call(s, "standing.put", four).find("\"invalid\""), std::string::npos) << "four names are a valid list";
}

// ---- reports kept for later (docs/contract/TELEMETRY.md): the keep flag, report.list and report.ack

namespace {

struct RamKept : KeptStore {
  std::map<int, std::vector<uint8_t>> f;
  size_t size(int slot) override { return f.count(slot) ? f[slot].size() : 0; }
  bool read(int slot, size_t off, uint8_t* buf, size_t n) override {
    if (!f.count(slot) || off + n > f[slot].size()) return false;
    memcpy(buf, f[slot].data() + off, n);
    return true;
  }
  bool append(int slot, const uint8_t* buf, size_t n) override { f[slot].insert(f[slot].end(), buf, buf + n); return true; }
  bool replace(int slot, const uint8_t* buf, size_t n) override { f[slot].assign(buf, buf + n); return true; }
  void remove(int slot) override { f.erase(slot); }
  uint32_t freeBytes() override { return 0xFFFFFFFFu; }
};

uint32_t g_seq = 0;
bool g_priv = true;
const char* g_who = "";
size_t g_budget = 1100;
bool g_has_location = false;
bool g_send_ok = false;
// The report as the firmware signs one: header, fields, and 64 bytes standing for the signature.
std::vector<uint8_t> sentReport(uint8_t fields, uint32_t seq) {
  ReportData d;
  uint8_t b[REPORT_MAX];
  size_t n = encodeReport(b, sizeof(b), fields, 3, seq, 5000 + seq, d);
  for (int i = 0; i < 64; i++) b[n++] = (uint8_t)(seq * 3 + i);
  return std::vector<uint8_t>(b, b + n);
}
bool kSend(void*, uint8_t fields, const uint8_t*, int, uint32_t* seq, uint8_t* built, size_t* built_len) {
  *seq = ++g_seq;
  const std::vector<uint8_t> r = sentReport(fields, g_seq);
  if (built) { memcpy(built, r.data(), r.size()); *built_len = r.size(); }
  return g_send_ok;   // false: nobody heard it, the datagram went nowhere
}
std::string b64enc(const std::vector<uint8_t>& in) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < in.size(); i += 3) {
    const uint32_t v = ((uint32_t)in[i] << 16) | ((i + 1 < in.size() ? in[i + 1] : 0) << 8) | (i + 2 < in.size() ? in[i + 2] : 0);
    out += A[v >> 18]; out += A[(v >> 12) & 63];
    out += i + 1 < in.size() ? A[(v >> 6) & 63] : '='; out += i + 2 < in.size() ? A[v & 63] : '=';
  }
  return out;
}
uint8_t kProduced(void*) { return RF_HEALTH | RF_RADIO | RF_COUNTERS | (g_has_location ? RF_LOCATION : 0); }
bool kPriv(void*) { return g_priv; }
const char* kWho(void*) { return g_who; }
size_t kBudget(void*) { return g_budget; }

Status oList(OpContext& c);
Status oRepList(OpContext& c) { return S(c)->opReportList(c); }
Status oRepAck(OpContext& c) { return S(c)->opReportAck(c); }
Status oPutK(OpContext& c) { return S(c)->opPut(c, g_put_at); }
Status oNowK(OpContext& c) { return S(c)->opNow(c, 1000000); }
const OpEntry KOPS[] = {{"standing.put", oPutK, PRIV_WRITE, true}, {"standing.list", ::oList, PRIV_READ, false},
                        {"report.now", oNowK, PRIV_WRITE, false}, {"report.list", oRepList, PRIV_READ, false}, {"report.ack", oRepAck, PRIV_WRITE, false}};

std::string kcall(StandingService& s, const std::string& op, const std::string& args = "") {
  Dispatcher d(KOPS, sizeof(KOPS) / sizeof(KOPS[0]), allow, nullptr, &s);
  const std::string m = R"({"v":1,"id":"q","op":")" + op + "\"" + (args.empty() ? "" : R"(,"args":)" + args) + "}";
  std::vector<char> out(2000);
  const int n = d.handle(m.data(), m.size(), out.data(), out.size(), nullptr);
  return n < 0 ? "<overflow>" : std::string(out.data(), n);
}

struct KeptOps : ::testing::Test {
  RamStore store;
  RamKept kept;
  StandingService s;
  void boot(StandingService& x, uint32_t at = 1000) {
    x.begin(ReportHost{kSend, kProduced, kPriv, kWho, hValid, hRandom, hYes, &store, nullptr, &kept, kBudget}, at);
  }
  void SetUp() override {
    g_seq = 0; g_send_ok = false; g_priv = true; g_who = ""; g_budget = 1100; g_has_location = false; g_put_at = 0;
    boot(s);
  }
  // Puts a keeping instruction and runs it `n` times.
  void run(int n, const char* fields = R"(["health","radio"])", const char* id = "k") {
    const std::string put = std::string(R"({"id":")") + id + R"(","fields":)" + fields + R"(,"every_s":900,"keep":true,"to":"abababababababababababababababababababababababababababababababab"})";
    ASSERT_NE(kcall(s, "standing.put", put).find("\"ok\""), std::string::npos) << kcall(s, "standing.put", put);
    uint32_t t = 0;
    s.tick(t);
    for (int i = 0; i < n; i++) { t += 900000; s.tick(t); }
  }
};

}  // namespace

TEST_F(KeptOps, AKeepingInstructionKeepsWhatItSendsWhetherOrNotAnyoneHeardIt) {
  run(3);
  const std::string l = kcall(s, "report.list");
  EXPECT_EQ(g_seq, 3u);
  EXPECT_NE(l.find(R"("cursor":1,"boot":3,"seq":1,"report":")"), std::string::npos) << l;
  for (uint32_t i = 1; i <= 3; i++)
    EXPECT_NE(l.find("\"report\":\"" + b64enc(sentReport(RF_HEALTH | RF_RADIO, i)) + "\""), std::string::npos) << "the exact signed bytes of report " << i << ": " << l;
  EXPECT_NE(l.find(R"("cursor":3,"boot":3,"seq":3,"report":")"), std::string::npos) << l;
  EXPECT_NE(l.find(R"("more":false,"dropped":0,"withheld":0)"), std::string::npos) << l;
  EXPECT_NE(kcall(s, "standing.list").find(R"("keep":true)"), std::string::npos);
}

TEST_F(KeptOps, AnInstructionWithoutKeepKeepsNothingAndReportNowIsNeverKept) {
  g_put_at = 0;
  ASSERT_NE(kcall(s, "standing.put", PUT).find("\"ok\""), std::string::npos);
  s.tick(0);
  s.tick(900000);
  s.tick(1800000);
  EXPECT_GE(g_seq, 2u);
  EXPECT_NE(kcall(s, "report.list").find(R"("reports":[])"), std::string::npos) << kcall(s, "report.list");
  EXPECT_EQ(kcall(s, "standing.list").find("keep"), std::string::npos);
}

TEST_F(KeptOps, OnTheNrf52KeptReportsShareWritesAndTheHourBoundCutsAFifteenMinuteBatchToFour) {
  EXPECT_EQ(keptBatchFor(true), 8) << "the nRF52 setting";
  EXPECT_EQ(keptBatchFor(false), 1);
  StandingService b;
  b.begin(ReportHost{kSend, kProduced, kPriv, kWho, hValid, hRandom, hYes, &store, nullptr, &kept, kBudget, keptBatchFor(true)}, 1000);
  const std::string put = R"({"id":"k","fields":["health"],"every_s":900,"keep":true,"to":"abababababababababababababababababababababababababababababababab"})";
  g_put_at = 0;
  ASSERT_NE(kcall(b, "standing.put", put).find("\"ok\""), std::string::npos);
  uint32_t t = 0;
  b.tick(t);
  for (int i = 0; i < 64; i++) { t += 900000; b.tick(t); }
  EXPECT_EQ(g_seq, 64u);
  EXPECT_EQ(b.keptAppends(), 15u) << "a write when an hour has passed since the oldest waiting: four reports at this period, not 64 writes";
  const std::string l = kcall(b, "report.list", R"({"after":61})");
  EXPECT_NE(l.find(R"("cursor":64)"), std::string::npos) << "a list flushes first: " << l;
  EXPECT_EQ(b.keptAppends(), 16u) << "the last three waited";
}

TEST_F(KeptOps, AReportNowIsNeverKept) {
  g_send_ok = true;
  const std::string r = kcall(s, "report.now", R"({"fields":["health"],"to":"abababababababababababababababababababababababababababababababab"})");
  EXPECT_NE(r.find(R"("status":"ok")"), std::string::npos) << r;
  EXPECT_EQ(g_seq, 1u) << "it did build and send one";
  EXPECT_NE(kcall(s, "report.list").find(R"("reports":[])"), std::string::npos) << kcall(s, "report.list");
}

TEST_F(KeptOps, ABoardWithNoFlashForTheRingAnswersKeepWithUnsupported) {
  StandingService b;
  b.begin(ReportHost{kSend, kProduced, kPriv, kWho, hValid, hRandom, hYes, &store, nullptr, nullptr, nullptr}, 1000);
  const std::string r = kcall(b, "standing.put", R"({"id":"k","fields":["health"],"every_s":900,"keep":true,"to":"abababababababababababababababababababababababababababababababab"})");
  EXPECT_NE(r.find(R"("status":"failed")"), std::string::npos) << r;
  EXPECT_NE(r.find(R"("error":"unsupported")"), std::string::npos) << r;
}

TEST_F(KeptOps, TheKeepFlagAndTheReportsSurviveAReboot) {
  run(2);
  StandingService b;
  boot(b);
  EXPECT_NE(kcall(b, "standing.list").find(R"("keep":true)"), std::string::npos) << kcall(b, "standing.list");
  const std::string l = kcall(b, "report.list");
  EXPECT_NE(l.find(R"("cursor":2)"), std::string::npos) << l;
  b.tick(1000);   // a boot's first run is due at once; it keeps, and its cursor follows
  EXPECT_NE(kcall(b, "report.list", R"({"after":2})").find(R"("cursor":3)"), std::string::npos);
}

TEST_F(KeptOps, APageStopsWhereTheLinkWouldNotTakeMoreAndMoreSaysSo) {
  run(8);
  g_budget = 760;   // a LoRa message after its envelope
  const std::string p1 = kcall(s, "report.list");
  EXPECT_NE(p1.find(R"("more":true)"), std::string::npos) << p1;
  size_t got = 0;
  for (size_t at = 0; (at = p1.find("\"cursor\"", at)) != std::string::npos; at++) got++;
  EXPECT_GE(got, 3u);
  EXPECT_LE(got, 4u) << "about four reports of this size";
  EXPECT_LE(p1.size(), 760u + 200u);
  // reading on from the last cursor reaches the rest, once each
  uint32_t after = (uint32_t)got, total = (uint32_t)got;
  for (int guard = 0; guard < 8; guard++) {
    const std::string p = kcall(s, "report.list", "{\"after\":" + std::to_string(after) + "}");
    size_t n = 0;
    for (size_t at = 0; (at = p.find("\"cursor\"", at)) != std::string::npos; at++) n++;
    after += n; total += n;
    if (p.find(R"("more":false)") != std::string::npos) break;
  }
  EXPECT_EQ(total, 8u);
  const std::string lim = kcall(s, "report.list", R"({"limit":2})");
  EXPECT_NE(lim.find(R"("more":true)"), std::string::npos);
  EXPECT_NE(kcall(s, "report.list", R"({"limit":0})").find("invalid"), std::string::npos);
}

TEST_F(KeptOps, AnAckReleasesUpToItsCursorAndAnOlderOneChangesNothing) {
  run(5);
  std::string a = kcall(s, "report.ack", R"({"upto":3})");
  EXPECT_NE(a.find(R"("acked":3,"released":3,"remaining":2)"), std::string::npos) << a;
  EXPECT_EQ(kcall(s, "report.list").find(R"("cursor":3)"), std::string::npos);
  EXPECT_NE(kcall(s, "report.list").find(R"("cursor":4)"), std::string::npos);
  a = kcall(s, "report.ack", R"({"upto":2})");
  EXPECT_NE(a.find(R"("released":0)"), std::string::npos) << a;
  EXPECT_NE(kcall(s, "report.ack", R"({"upto":99})").find("unknown-cursor"), std::string::npos);
  EXPECT_NE(kcall(s, "report.ack", R"({"upto":0})").find("invalid"), std::string::npos);
}

TEST_F(KeptOps, ALocationReportIsListedOnlyToACallerAllowedToReadItAndCountedWithheldForOthers) {
  g_has_location = true;
  run(1, R"(["health","location"])", "loc");   // report 1: with location
  g_has_location = false;
  run(2, R"(["health","radio"])", "plain");   // reports 2 and 3 (the first instruction runs again as well)
  g_priv = false;
  const std::string l = kcall(s, "report.list");
  EXPECT_EQ(l.find(R"("cursor":1,)"), std::string::npos) << "never cut out: left out whole\n" << l;
  EXPECT_NE(l.find(R"("withheld":)"), std::string::npos);
  EXPECT_EQ(l.find(R"("withheld":0)"), std::string::npos) << l;
  g_priv = true;
  EXPECT_NE(kcall(s, "report.list").find(R"("cursor":1,)"), std::string::npos);
}

TEST_F(KeptOps, AnAckNeverReleasesWhatItsCallerCouldNotList) {
  g_has_location = true;
  run(1, R"(["health","location"])", "loc");
  g_has_location = false;
  uint32_t last = g_seq;
  ASSERT_GE(last, 1u);
  g_priv = false;
  const std::string r = kcall(s, "report.ack", "{\"upto\":" + std::to_string(last) + "}");
  EXPECT_NE(r.find(R"("status":"refused")"), std::string::npos) << r;
  EXPECT_NE(r.find(R"("error":"location-withheld")"), std::string::npos) << r;
  g_priv = true;
  EXPECT_NE(kcall(s, "report.list").find(R"("cursor":1,)"), std::string::npos) << "and it released nothing";
  EXPECT_NE(kcall(s, "report.ack", "{\"upto\":" + std::to_string(last) + "}").find(R"("status":"ok")"), std::string::npos);
}

TEST_F(KeptOps, TheAckBelongsToTheMemberWhoSetTheKeepingInstruction) {
  g_who = "alice";
  run(3);
  g_who = "bob";
  const std::string r = kcall(s, "report.ack", R"({"upto":2})");
  EXPECT_NE(r.find(R"("error":"not-owner")"), std::string::npos) << r;
  EXPECT_NE(kcall(s, "report.list").find(R"("cursor":1,)"), std::string::npos) << "any caller may read, and nothing was released";
  g_who = "";
  EXPECT_NE(kcall(s, "report.ack", R"({"upto":2})").find("not-owner"), std::string::npos) << "an unsigned caller is not the member either";
  g_who = "alice";
  EXPECT_NE(kcall(s, "report.ack", R"({"upto":2})").find(R"("released":2)"), std::string::npos);
}

TEST_F(KeptOps, TheRingOverflowShowsInDroppedAndInTheCursors) {
  run(70);
  const std::string l = kcall(s, "report.list");
  EXPECT_NE(l.find(R"("dropped":6)"), std::string::npos) << l.substr(l.size() > 120 ? l.size() - 120 : 0);
  EXPECT_EQ(l.find(R"("cursor":6,)"), std::string::npos);
  EXPECT_NE(l.find(R"("cursor":7,)"), std::string::npos) << "the first one it still holds is 7: the gap shows in the cursors";
  const std::string a = kcall(s, "report.ack", R"({"upto":10})");
  EXPECT_NE(a.find(R"("released":4)"), std::string::npos) << a;
  EXPECT_NE(kcall(s, "report.list", R"({"after":10})").find(R"("dropped":0)"), std::string::npos);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
