// Standing instructions (src/helpers/iface/StandingOps.h) through the real dispatcher: when a run falls due. A loop takes
// its time once and handles a request later in the same pass, so a put can be stamped after the time the next tick is given.
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/iface/IfaceDispatch.h"
#include "helpers/iface/StandingOps.h"

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
static bool hSend(void*, uint8_t, const uint8_t*, int, uint32_t* seq) { g_sends++; *seq = g_sends; return true; }
static uint8_t hProduced(void*) { return RF_HEALTH | RF_RADIO; }
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
