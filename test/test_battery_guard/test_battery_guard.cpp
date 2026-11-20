// The battery guard's settings, validation, dry run and CLI lines (src/helpers/BatteryGuard.h, BatteryGuardCfg.h), and the
// classifier reading its thresholds from them.
#define BATTERY_GUARD_BOARD 1   // as a board of the support matrix defines it
#include <gtest/gtest.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "helpers/BatteryGuard.h"
#include "helpers/BatteryGuardPrefs.h"

using BatteryGuard::Cfg;

// The prefs file the Repeater and the Companion keep: the group serialised to text and read back, as a reboot does.
class InputStream : public Stream {
  const char* _text;
  int _pos = 0, _len;
public:
  explicit InputStream(const char* text) : _text(text), _len((int)strlen(text)) {}
  int available() override { return _len - _pos; }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};
class OutputStream : public Stream {
  std::string _t;
  size_t emit(long long v) { char b[24]; int n = snprintf(b, sizeof(b), "%lld", v); return write((const uint8_t*)b, (size_t)n); }
public:
  size_t write(uint8_t b) override { _t.push_back((char)b); return 1; }
  size_t write(const uint8_t* b, size_t n) override { _t.append((const char*)b, n); return n; }
  size_t print(unsigned char v, int = DEC) override { return emit(v); }
  size_t print(int v, int = DEC) override { return emit(v); }
  size_t print(unsigned int v, int = DEC) override { return emit(v); }
  size_t print(long v, int = DEC) override { return emit(v); }
  size_t print(unsigned long v, int = DEC) override { return emit(v); }
  size_t print(long long v, int = DEC) override { return emit(v); }
  size_t print(unsigned long long v, int = DEC) override { return emit(v); }
  const std::string& text() const { return _t; }
};

namespace {
struct Feed {
  BatteryLevel b;
  uint32_t now = 0;
  void one(uint16_t mv) { b.sample(now, mv); now += BatteryLevel::SAMPLE_MS; }
  void window(uint16_t mv) { for (int i = 0; i < BatteryLevel::WINDOW_READINGS; i++) one(mv); }
  // One CLI line, as the role calls it. Returns the reply; *changed says whether the settings changed.
  std::string cli(const char* line, bool external = false, bool* changed = nullptr) {
    char reply[150];
    bool ch = false;
    EXPECT_TRUE(BatteryGuard::handleCommand(line, reply, sizeof(reply), b, external, &ch));
    if (changed) *changed = ch;
    return reply;
  }
};
}  // namespace

TEST(BatteryGuardCfg, DefaultsAreOffWithTheSlice1Thresholds) {
  Feed f;
  const Cfg& c = f.b.settings();
  EXPECT_EQ(0, c.enabled);
  EXPECT_EQ(3550, c.low_mv);
  EXPECT_EQ(3400, c.critical_mv);
  EXPECT_EQ(100, c.margin_mv);
  EXPECT_EQ(900, c.wake_s);
  EXPECT_EQ(nullptr, BatteryGuard::validate(c));
  EXPECT_EQ("> enabled=off low=3550 critical=3400 margin=100 wake=900", f.cli("get battery.guard"));
}

TEST(BatteryGuardCfg, ValidationRefusesEachInvalidCombination) {
  struct { const char* text; const char* why; } cases[] = {
    {"low=3400", "low-not-above-critical"},                  // equal
    {"low=3450", "low-not-above-critical"},                  // above, but not by 100
    {"low=3499", "low-not-above-critical"},
    {"critical=3500", "low-not-above-critical"},             // critical raised past low - 100, with low stored
    {"critical=3600", "low-not-above-critical"},
    {"critical=2499", "critical-out-of-range"},
    {"critical=4001 low=4100", "critical-out-of-range"},
    {"low=2599 critical=2500", "low-out-of-range"},
    {"low=4101", "low-out-of-range"},
    {"margin=49", "margin-out-of-range"},
    {"margin=501", "margin-out-of-range"},
    {"low=4100 margin=501", "margin-out-of-range"},
    {"wake=299", "wake-out-of-range"},
    {"wake=3601", "wake-out-of-range"},
  };
  Feed f;
  for (const auto& t : cases) {
    Cfg out;
    const char* e = BatteryGuard::parse(t.text, f.b.settings(), &out);
    ASSERT_EQ(nullptr, e) << t.text;
    const char* v = BatteryGuard::validate(out);
    ASSERT_NE(nullptr, v) << t.text;
    EXPECT_STREQ(t.why, v) << t.text;
  }
  Cfg c;
  c.low_mv = 4100; c.margin_mv = 501;
  EXPECT_STREQ("margin-out-of-range", BatteryGuard::validate(c));
  c.low_mv = 4100; c.margin_mv = 500; c.critical_mv = 3400;
  EXPECT_EQ(nullptr, BatteryGuard::validate(c));
  c.low_mv = 4101;
  EXPECT_STREQ("low-out-of-range", BatteryGuard::validate(c));
}

TEST(BatteryGuardCfg, TheBoundariesAreInside) {
  Cfg c;
  c.critical_mv = 2500; c.low_mv = 2600; c.margin_mv = 50; c.wake_s = 300;
  EXPECT_EQ(nullptr, BatteryGuard::validate(c));
  c.critical_mv = 4000; c.low_mv = 4100; c.margin_mv = 500; c.wake_s = 3600;
  EXPECT_EQ(nullptr, BatteryGuard::validate(c));
}

TEST(BatteryGuardCfg, TheFormatIsStrict) {
  Cfg out;
  const Cfg base;
  const char* bad[] = {"", " ", "low", "low=", "low=abc", "low=35 50", "low=3550 low=3560", "foo=1", "enabled=yes", "enabled=1",
                       "low=3550 ", " low=3550", "low=3550  critical=3400", "low=-1", "low=100000", "LOW=3550", "low=3.5"};
  for (const char* t : bad) EXPECT_STREQ("bad-format", BatteryGuard::parse(t, base, &out)) << "[" << t << "]";
  ASSERT_EQ(nullptr, BatteryGuard::parse("wake=600 enabled=on critical=3300 low=3500 margin=80", base, &out));
  EXPECT_EQ(1, out.enabled);
  EXPECT_EQ(600, out.wake_s);
  EXPECT_EQ(3300, out.critical_mv);
  EXPECT_EQ(3500, out.low_mv);
  EXPECT_EQ(80, out.margin_mv);
  // a field left out comes from the base
  ASSERT_EQ(nullptr, BatteryGuard::parse("low=3600", base, &out));
  EXPECT_EQ(3400, out.critical_mv);
  char text[BatteryGuard::TEXT_MAX];
  BatteryGuard::format(out, text, sizeof(text));
  EXPECT_STREQ("enabled=off low=3600 critical=3400 margin=100 wake=900", text);
  EXPECT_TRUE(BatteryGuard::formatOk("low=3600"));
  EXPECT_FALSE(BatteryGuard::formatOk("low=x"));
}

// The private firmware validated one key against the other key's old stored value. A change of both thresholds that is
// valid as a whole must be accepted in one request, whatever the stored values were.
TEST(BatteryGuard, ARaisedPairIsValidatedAsAWholeNotAgainstTheStoredOne) {
  Feed f;
  // stored 3550 / 3400. Raising critical alone to 3500 would break "low above critical by 100" against the stored low;
  // together with low=3700 it is a valid pair.
  EXPECT_EQ("Err - low-not-above-critical", f.cli("set battery.guard critical=3500"));
  bool changed = false;
  EXPECT_EQ("OK", f.cli("set battery.guard critical=3500 low=3700", false, &changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(3500, f.b.settings().critical_mv);
  EXPECT_EQ(3700, f.b.settings().low_mv);
  // and a lowered pair, where the stored values would refuse the new critical against the new low the other way round
  EXPECT_EQ("OK", f.cli("set battery.guard low=3000 critical=2800"));
  EXPECT_EQ(3000, f.b.settings().low_mv);
  EXPECT_EQ(2800, f.b.settings().critical_mv);
}

TEST(BatteryGuard, ARefusedRequestChangesNothing) {
  Feed f;
  bool changed = true;
  EXPECT_EQ("Err - low-not-above-critical", f.cli("set battery.guard enabled=on low=3400 wake=600", false, &changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ("> enabled=off low=3550 critical=3400 margin=100 wake=900", f.cli("get battery.guard"));
  EXPECT_EQ("Err - bad-format", f.cli("set battery.guard "));
  EXPECT_EQ("Err - bad-format", f.cli("set battery.guard low=3600 junk"));
  EXPECT_EQ("> enabled=off low=3550 critical=3400 margin=100 wake=900", f.cli("get battery.guard"));
}

TEST(BatteryGuard, TheClassifierUsesTheStoredThresholds) {
  Feed f;
  f.window(3500);
  EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  ASSERT_EQ("OK", f.cli("set battery.guard low=3400 critical=3200"));   // 3500 is now low + margin: ok
  EXPECT_EQ(BatteryLevel::OK, f.b.level()) << "re-classified at once, not at the next window";
  f.window(3500);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
  f.window(3399);
  EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  f.window(3199);
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3299);                                         // critical + the stored margin is 3300
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3300);
  EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  ASSERT_EQ("OK", f.cli("set battery.guard margin=300"));
  f.window(3000);
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3499);                                         // critical + 300
  EXPECT_EQ(BatteryLevel::CRITICAL, f.b.level());
  f.window(3500);
  EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  f.window(3699);                                         // low + 300
  EXPECT_EQ(BatteryLevel::LOW_LEVEL, f.b.level());
  f.window(3700);
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
}

TEST(BatteryGuard, ArmingAtCriticalIsRefusedWouldSleepNow) {
  Feed f;
  f.window(3300);
  ASSERT_EQ(BatteryLevel::CRITICAL, f.b.level());
  bool changed = false;
  EXPECT_EQ("Err - would-sleep-now", f.cli("set battery.guard enabled=on", false, &changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ(0, f.b.settings().enabled);
  // turning it off is always allowed; changing thresholds while off is allowed
  EXPECT_EQ("OK", f.cli("set battery.guard enabled=off low=3600"));
  // thresholds that would classify the current window critical are refused when it would be on
  Feed g;
  g.window(3500);
  ASSERT_EQ("OK", g.cli("set battery.guard enabled=on"));
  EXPECT_EQ("Err - would-sleep-now", g.cli("set battery.guard critical=3600 low=3700"));
  EXPECT_EQ(3400, g.b.settings().critical_mv);
  // with external power sensed, the guard would not sleep, so it may be armed
  Feed h;
  h.window(3300);
  EXPECT_EQ("OK", h.cli("set battery.guard enabled=on", true));
  // an unknown level never refuses
  Feed u;
  EXPECT_EQ("OK", u.cli("set battery.guard enabled=on"));
}

TEST(BatteryGuard, TheDryRunAnswersAndWritesNothing) {
  Feed f;
  EXPECT_EQ("> accepted=yes level=unknown would_sleep=no reason=guard-off", f.cli("get battery.guard.dry"));
  f.window(4000);
  EXPECT_EQ("> accepted=yes level=ok would_sleep=no reason=guard-off", f.cli("get battery.guard.dry"));
  EXPECT_EQ("> accepted=yes level=ok would_sleep=no reason=level-ok", f.cli("get battery.guard.dry enabled=on"));
  EXPECT_EQ(0, f.b.settings().enabled) << "a dry run changes nothing";
  EXPECT_EQ("> accepted=yes level=low would_sleep=no reason=level-low",
            f.cli("get battery.guard.dry enabled=on low=4050 critical=3900"));
  EXPECT_EQ("> accepted=no refusal=low-not-above-critical level=ok would_sleep=no reason=guard-off",
            f.cli("get battery.guard.dry enabled=on low=3400"));
  EXPECT_EQ("Err - bad-format", f.cli("get battery.guard.dry junk"));
  // a proposal whose thresholds put the current window at critical is refused, and the answer says why
  Feed h;
  h.window(3990);
  EXPECT_EQ("> accepted=no refusal=would-sleep-now level=critical would_sleep=yes reason=level-critical",
            h.cli("get battery.guard.dry enabled=on low=4100 critical=4000"));
  EXPECT_EQ(0, h.b.settings().enabled);
  EXPECT_EQ("> enabled=off low=3550 critical=3400 margin=100 wake=900", f.cli("get battery.guard"));
  EXPECT_EQ(BatteryLevel::OK, f.b.level());
  // external power
  EXPECT_EQ("> accepted=yes level=ok would_sleep=no reason=external-power", f.cli("get battery.guard.dry enabled=on", true));
  // a stored armed guard at critical would sleep
  Feed g;
  g.window(3500);
  ASSERT_EQ("OK", g.cli("set battery.guard enabled=on"));
  g.window(3300);
  EXPECT_EQ("> accepted=no refusal=would-sleep-now level=critical would_sleep=yes reason=level-critical",
            g.cli("get battery.guard.dry"));
}

TEST(BatteryGuard, AnUnsupportedBoardRefusesToArm) {
  Feed f;
  Cfg out;
  // the support flag is a parameter of check(), so the refusal is tested without a build per board
  EXPECT_STREQ("unsupported", BatteryGuard::check("enabled=on", f.b, false, false, &out));
  EXPECT_EQ(nullptr, BatteryGuard::check("enabled=off low=3600", f.b, false, false, &out));
  EXPECT_EQ(nullptr, BatteryGuard::check("enabled=on", f.b, true, false, &out));
  EXPECT_FALSE(BatteryGuard::decide(out, f.b, false, false).would_sleep);
  EXPECT_STREQ("unsupported", BatteryGuard::decide(out, f.b, false, false).reason);
}

TEST(BatteryGuardPrefs, TheSettingsSurviveARoundTrip) {
  Cfg& c = batteryLevel().settings();
  c = Cfg();
  c.enabled = 1; c.low_mv = 3700; c.critical_mv = 3500; c.margin_mv = 150; c.wake_s = 1800;
  BatteryGuardPrefs prefs;
  OutputStream out;
  ASSERT_TRUE(prefs.saveSerial(out));
  c = Cfg();   // forgotten: a reboot
  ASSERT_EQ(0, c.enabled);
  InputStream in(out.text().c_str());
  BatteryGuardPrefs loaded;
  ASSERT_TRUE(loaded.loadSerial(in));
  EXPECT_EQ(1, c.enabled);
  EXPECT_EQ(3700, c.low_mv);
  EXPECT_EQ(3500, c.critical_mv);
  EXPECT_EQ(150, c.margin_mv);
  EXPECT_EQ(1800, c.wake_s);
  c = Cfg();
}

TEST(BatteryGuardPrefs, AFileWithoutTheGroupLeavesTheDefaults) {
  Cfg& c = batteryLevel().settings();
  c = Cfg();
  BatteryGuardPrefs loaded;
  InputStream in("{}");   // as a prefs file written before the group existed, seen by the group
  loaded.loadSerial(in);
  EXPECT_EQ(0, c.enabled);
  EXPECT_EQ(3550, c.low_mv);
  EXPECT_EQ(3400, c.critical_mv);
  EXPECT_EQ(100, c.margin_mv);
  EXPECT_EQ(900, c.wake_s);
}

TEST(BatteryGuard, OtherLinesAreNotThis) {
  Feed f;
  char reply[150] = "";
  bool ch = false;
  EXPECT_FALSE(BatteryGuard::handleCommand("get battery", reply, sizeof(reply), f.b, false, &ch));
  EXPECT_FALSE(BatteryGuard::handleCommand("get battery.guardx", reply, sizeof(reply), f.b, false, &ch));
  EXPECT_FALSE(BatteryGuard::handleCommand("set battery.guardx 1", reply, sizeof(reply), f.b, false, &ch));
  EXPECT_FALSE(BatteryGuard::handleCommand("get battery.guard.drying", reply, sizeof(reply), f.b, false, &ch));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
