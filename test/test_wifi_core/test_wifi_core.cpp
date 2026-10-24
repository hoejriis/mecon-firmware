// Decision logic of the Wi-Fi controller (src/helpers/wifi/WifiCore.*), driven by a fake radio that behaves like the
// platform layer does: scans take 300 ms, a join takes 400 ms and ends in success, auth failure or not-found, and a
// link drops with the Wi-Fi task's "lost" notification. No real time, no platform.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>
#include "helpers/wifi/WifiCore.h"

namespace {

struct Ap { std::string pwd; int8_t rssi; bool up; };

struct Rig {
  WifiProfiles prof;
  WifiTiming t;
  WifiCore core;
  std::map<std::string, Ap> aps;
  std::map<std::string, std::string> pwds;   // what the device would send for each SSID
  uint32_t now;
  std::vector<std::string> joins;            // SSID of every join attempt, in order
  std::vector<uint8_t> connects;             // profile of every Connect action
  // fake radio
  bool scan_running = false; uint32_t scan_end = 0;
  bool joining = false; uint32_t join_end = 0; std::string join_ssid;
  bool linked = false; std::string link_ssid;
  uint32_t scans = 0;
  bool scan_fails = false;    // a scan ends with the platform's "failed" instead of a result
  bool scan_silent = false;   // a scan never reports at all
  bool stale_drop = false;    // each join is preceded by a drop event from an attempt the driver started itself (IDF 5)

  explicit Rig(uint32_t start = 1000) : core(&prof, WifiTiming()), now(start) {}

  void profile(uint8_t i, const char* ssid, uint8_t prio, const char* pwd = "") {
    prof.setSsid(i, ssid); prof.setPwd(i, pwd); prof.setPriority(i, prio);
  }
  void ap(const char* ssid, const char* pwd = "", int8_t rssi = -50, bool up = true) { aps[ssid] = {pwd, rssi, up}; }
  void apDown(const char* ssid) { aps[ssid].up = false; }
  void apUp(const char* ssid) { aps[ssid].up = true; }
  void start() { core.configure(&prof, t); core.begin(now); }
  WifiState state() const { return core.state(); }
  const char* current() { uint8_t c = core.current(); return c == WifiCore::NONE ? "" : prof.ssid(c); }

  void act(WifiAction a) {
    switch (a.kind) {
      case WifiAction::Scan: scan_running = true; scan_end = now + 300; scans++; break;
      case WifiAction::Connect:
        connects.push_back(a.profile);
        joins.push_back(prof.ssid(a.profile));
        linked = false;   // joining drops the old link; the fake radio stays quiet about an intentional leave
        joining = true; join_end = now + 400; join_ssid = prof.ssid(a.profile);
        join_pwd = prof.pwd(a.profile);
        if (stale_drop) core.onDisconnected(now, WifiDrop::Other);   // reason 203, in the same instant as the join
        break;
      case WifiAction::Disconnect: joining = false; linked = false; break;
      default: break;
    }
  }
  std::string join_pwd;

  void tick() {   // advance 100 ms
    now += 100;
    if (scan_running && !scan_silent && (int32_t)(now - scan_end) >= 0) {
      scan_running = false;
      if (scan_fails) core.scanFailed(now);
      else {
        for (auto& kv : aps) if (kv.second.up) core.scanObserve(kv.first.c_str(), kv.second.rssi);
        core.scanDone(now);
      }
    }
    if (joining && (int32_t)(now - join_end) >= 0) {
      joining = false;
      auto it = aps.find(join_ssid);
      if (it == aps.end() || !it->second.up) core.onDisconnected(now, WifiDrop::NotFound);
      else if (!it->second.pwd.empty() && it->second.pwd != join_pwd) core.onDisconnected(now, WifiDrop::AuthFailed);
      else { linked = true; link_ssid = join_ssid; core.onConnected(now); }
    }
    if (linked) {
      auto it = aps.find(link_ssid);
      if (it == aps.end() || !it->second.up) { linked = false; core.onDisconnected(now, WifiDrop::Other); }
    }
    act(core.poll(now));
  }
  void run(uint32_t ms) { for (uint32_t e = now + ms; (int32_t)(now - e) < 0;) tick(); }
};

}  // namespace

// ---- selection

TEST(WifiCore, JoinsTheHighestPriorityVisibleProfile) {
  Rig r;
  r.profile(0, "home", 50); r.profile(1, "office", 90); r.profile(2, "phone", 10);
  r.ap("home"); r.ap("office"); r.ap("phone");
  r.start(); r.run(3000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_STREQ("office", r.current());
}

TEST(WifiCore, AHigherPriorityProfileThatIsNotVisibleIsSkipped) {
  Rig r;
  r.profile(0, "home", 50); r.profile(1, "office", 90);
  r.ap("home");   // office is not there
  r.start(); r.run(3000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_STREQ("home", r.current());
}

TEST(WifiCore, EqualPrioritiesGoToTheLowerProfileNumber) {
  Rig r;
  r.profile(0, "a", 50); r.profile(1, "b", 50); r.profile(2, "c", 50);
  r.ap("a"); r.ap("b"); r.ap("c");
  r.start(); r.run(3000);
  EXPECT_STREQ("a", r.current());
}

TEST(WifiCore, ProfileZeroFallsBackToTheBuildTimeNetwork) {
  Rig r;
  r.prof.build_ssid = "buildnet";
  r.ap("buildnet");
  r.start(); r.run(3000);
  EXPECT_STREQ("buildnet", r.current());
}

TEST(WifiCore, NoProfileMeansNoScanning) {
  Rig r;
  r.start(); r.run(60000);
  EXPECT_EQ(WifiState::Idle, r.state());
  EXPECT_EQ(0u, r.scans);
}

// ---- failover

TEST(WifiCore, WrongPasswordFailsOverToTheNextVisibleProfile) {
  Rig r;
  r.profile(0, "home", 90, "wrong-pw-fake"); r.profile(1, "office", 50, "office-pw-fake");
  r.ap("home", "right-pw-fake"); r.ap("office", "office-pw-fake");
  r.start(); r.run(5000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_STREQ("office", r.current());
  EXPECT_EQ(WifiFail::AuthFailed, r.core.lastFail(0));
  EXPECT_EQ(WifiFail::None, r.core.lastFail(1));
}

TEST(WifiCore, AWrongPasswordIsNotRetriedEveryMinuteButBacksOffEscalating) {
  Rig r;
  r.profile(0, "home", 90, "wrong-pw-fake"); r.profile(1, "office", 50);
  r.ap("home", "right-pw-fake"); r.ap("office");
  r.start();
  r.run(3600000);   // an hour on the lower profile
  uint32_t home_tries = 0;
  for (auto& j : r.joins) if (j == "home") home_tries++;
  EXPECT_GE(home_tries, 2u);   // it is tried again (the password may have been fixed on the access point) ...
  EXPECT_LE(home_tries, 8u);   // ... but not every upgrade scan: 120 s doubling to 1 h
  EXPECT_STREQ("office", r.current());
}

TEST(WifiCore, NotFoundAtJoinMovesOn) {
  Rig r;
  r.profile(0, "gone", 90); r.profile(1, "here", 50);
  r.ap("gone"); r.ap("here");
  r.start();
  r.run(600);        // scan done: both visible, "gone" chosen and the join under way
  r.apDown("gone");  // vanishes between the scan and the join
  r.run(4000);
  EXPECT_STREQ("here", r.current());
  EXPECT_EQ(WifiFail::NotFound, r.core.lastFail(0));
}

// ESP-IDF 5: a join is preceded by a drop event from an attempt it supersedes, and the link comes up right after. The
// drop must not count: counting it cooled the profile down and delayed the first link by ~17 s on v4test.
TEST(WifiCore, ADropReportedAsTheJoinStartsDoesNotCountWhenTheLinkFollows) {
  Rig r;
  r.stale_drop = true;
  r.profile(0, "home", 50);
  r.ap("home");
  r.start(); r.run(3000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_STREQ("home", r.current());
  EXPECT_EQ(1u, r.joins.size()) << "linked on the first join";
  EXPECT_EQ(WifiFail::None, r.core.lastFail(0));
}

// A drop with no link after it is still a failure, counted once the settle time has passed.
TEST(WifiCore, ADropWhileJoiningCountsWhenNoLinkFollows) {
  Rig r;
  r.profile(0, "gone", 90); r.profile(1, "here", 50);
  r.ap("gone"); r.ap("here");
  r.start();
  r.run(600);                                  // "gone" chosen, the join under way
  r.joining = false;                           // the radio reports a drop and nothing else
  r.core.onDisconnected(r.now, WifiDrop::Other);
  r.run(500);
  EXPECT_EQ(WifiState::Connecting, r.state()) << "not counted inside the settle time";
  EXPECT_EQ(WifiFail::None, r.core.lastFail(0));
  r.run(4000);
  EXPECT_EQ(WifiFail::Other, r.core.lastFail(0));
  EXPECT_STREQ("here", r.current());
}

TEST(WifiCore, JoinTimeoutMovesOnAndCoolsTheProfileDown) {
  Rig r;
  r.profile(0, "slow", 90); r.profile(1, "ok", 50);
  r.ap("slow"); r.ap("ok");
  r.start();
  r.run(400);
  r.joining = false;   // the join never reports back
  r.run(25000);
  EXPECT_STREQ("ok", r.current());
  EXPECT_EQ(WifiFail::Timeout, r.core.lastFail(0));
}

TEST(WifiCore, FailsOverWhenTheNetworkDisappears) {
  Rig r;
  r.profile(0, "home", 90); r.profile(1, "phone", 10);
  r.ap("home"); r.ap("phone");
  r.start(); r.run(3000);
  ASSERT_STREQ("home", r.current());
  r.apDown("home");
  r.run(6000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_STREQ("phone", r.current());
}

TEST(WifiCore, RepeatedQuickDropsPutTheProfileInCooldown) {
  Rig r;
  r.profile(0, "flaky", 90); r.profile(1, "steady", 10);
  r.ap("flaky"); r.ap("steady");
  r.start(); r.run(3000);
  ASSERT_STREQ("flaky", r.current());
  for (int k = 0; k < 4 && strcmp(r.current(), "steady") != 0; k++) {
    r.run(3000);
    r.linked = false;   // the link drops again and again, well before the stable time
    if (r.core.state() == WifiState::Connected) r.core.onDisconnected(r.now, WifiDrop::Other);
  }
  r.run(5000);
  EXPECT_STREQ("steady", r.current());
  EXPECT_EQ(WifiFail::Disconnects, r.core.lastFail(0));
}

TEST(WifiCore, ALongLinkResetsTheDropCount) {
  Rig r;
  r.profile(0, "home", 90); r.profile(1, "other", 10);
  r.ap("home"); r.ap("other");
  r.start(); r.run(3000);
  auto drop = [&r]() { r.linked = false; r.core.onDisconnected(r.now, WifiDrop::Other); r.run(4000); };
  for (int k = 0; k < 4; k++) {   // two quick drops, then a good long link, over and over: never three in a row
    r.run(3000); drop();
    r.run(3000); drop();
    r.run(60000); drop();
    ASSERT_STREQ("home", r.current()) << "round " << k;
  }
  EXPECT_EQ(WifiFail::None, r.core.lastFail(0));
}

// ---- return to a better profile

TEST(WifiCore, ReturnsToAHigherPriorityProfileOnlyAfterTheHoldDown) {
  Rig r;
  r.profile(0, "home", 90); r.profile(1, "phone", 10);
  r.ap("phone");   // home is away
  r.start(); r.run(3000);
  ASSERT_STREQ("phone", r.current());
  r.apUp("home");
  r.run(100000);   // seen, but not yet for the 120 s
  EXPECT_STREQ("phone", r.current());
  r.run(100000);
  EXPECT_STREQ("home", r.current());
  EXPECT_EQ(2u, r.joins.size());   // phone, then home: exactly one switch
}

TEST(WifiCore, ANetworkThatFlickersNeverGetsTheHoldDownAndCausesNoSwitch) {
  Rig r;
  r.profile(0, "home", 90); r.profile(1, "phone", 10);
  r.ap("phone");
  r.start(); r.run(3000);
  for (int k = 0; k < 30; k++) {   // visible for one scan, gone for the next, for an hour
    r.apUp("home");  r.run(60000);
    r.apDown("home"); r.run(60000);
  }
  EXPECT_STREQ("phone", r.current());
  EXPECT_EQ(1u, r.joins.size());   // the initial join and not one more
}

TEST(WifiCore, NoUpgradeScansWhileOnTheBestProfile) {
  Rig r;
  r.profile(0, "home", 90); r.profile(1, "phone", 10);
  r.ap("home"); r.ap("phone");
  r.start(); r.run(3000);
  uint32_t before = r.scans;
  r.run(3600000);
  EXPECT_EQ(before, r.scans);
  EXPECT_EQ(1u, r.joins.size());
}

TEST(WifiCore, ASwitchUpThatFailsFallsBackAndDoesNotFlap) {
  Rig r;
  r.profile(0, "home", 90, "wrong-pw-fake"); r.profile(1, "phone", 10);
  r.ap("phone");
  r.start(); r.run(3000);
  r.ap("home", "right-pw-fake");   // appears, but our password for it is wrong
  r.run(3600000);
  EXPECT_STREQ("phone", r.current());
  uint32_t home_tries = 0;
  for (auto& j : r.joins) if (j == "home") home_tries++;
  EXPECT_LE(home_tries, 6u);
}

// ---- a scan that fails

TEST(WifiCore, AFailedScanJoinsTheHighestPriorityProfileDirectly) {
  Rig r;
  r.profile(0, "lo", 10); r.profile(1, "hi", 90);
  r.ap("lo"); r.ap("hi");
  r.scan_fails = true;
  r.start(); r.run(5000);
  EXPECT_EQ(WifiState::Connected, r.state());
  ASSERT_EQ(1u, r.joins.size());
  EXPECT_EQ("hi", r.joins[0]);
}

TEST(WifiCore, AScanThatNeverReportsEndsAtTheTimeoutAndJoinsDirectly) {
  Rig r;
  r.profile(0, "net", 50);
  r.ap("net");
  r.scan_silent = true;
  r.start(); r.run(r.t.scan_timeout_ms - 1000);
  EXPECT_EQ(WifiState::Scanning, r.state());
  EXPECT_TRUE(r.joins.empty());
  r.run(3000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_EQ(1u, r.joins.size());
}

TEST(WifiCore, ABlindJoinThatMissesMovesOnToTheNextProfile) {
  Rig r;
  r.profile(0, "lo", 10); r.profile(1, "hi", 90);
  r.ap("lo");   // "hi" is not there
  r.scan_fails = true;
  r.start(); r.run(10000);
  EXPECT_EQ(WifiState::Connected, r.state());
  ASSERT_EQ(2u, r.joins.size());
  EXPECT_EQ("hi", r.joins[0]);
  EXPECT_EQ("lo", r.joins[1]);
}

TEST(WifiCore, ABlindJoinThatMissesIsNotRepeatedAtOnce) {
  Rig r;
  r.profile(0, "gone", 50);
  r.scan_fails = true;
  r.start(); r.run(12000);
  EXPECT_EQ(1u, r.joins.size());   // cooling for 15 s: the second backoff round has nothing to try
  r.run(120000);
  EXPECT_GE(r.joins.size(), 2u);   // but it is tried again later, not given up on
  EXPECT_LT(r.joins.size(), 12u);
}

TEST(WifiCore, AFailedUpgradeScanKeepsTheLinkAndGuessesNothing) {
  Rig r;
  r.profile(0, "lo", 10); r.profile(1, "hi", 90);
  r.ap("lo");
  r.start(); r.run(10000);
  ASSERT_EQ(WifiState::Connected, r.state());
  r.scan_fails = true;
  r.ap("hi");
  r.run(400000);
  EXPECT_EQ(WifiState::Connected, r.state());
  EXPECT_EQ(1u, r.joins.size());
  EXPECT_STREQ("lo", r.current());
}

TEST(WifiCore, TheScanAfterAFailedOneIsTrustedAgain) {
  Rig r;
  r.profile(0, "lo", 10); r.profile(1, "hi", 90);
  r.ap("lo");
  r.scan_fails = true;
  r.start(); r.run(10000);
  ASSERT_EQ(WifiState::Connected, r.state());
  ASSERT_STREQ("lo", r.current());
  r.scan_fails = false;
  r.ap("hi");
  r.run(400000);   // upgrade scans see "hi" for the whole hold-down time
  EXPECT_STREQ("hi", r.current());
}

TEST(WifiCore, AFailedScanStillSkipsAProfileWithAWrongPassword) {
  Rig r;
  r.profile(0, "lo", 10); r.profile(1, "hi", 90, "wrong-pw-fake");
  r.ap("lo"); r.ap("hi", "right-pw-fake");
  r.scan_fails = true;
  r.start(); r.run(10000);
  EXPECT_STREQ("lo", r.current());
  ASSERT_EQ(2u, r.joins.size());
  EXPECT_EQ("hi", r.joins[0]);
}

// ---- nothing reachable

TEST(WifiCore, BacksOffDoublingToACapWhenNothingIsReachable) {
  Rig r;
  r.profile(0, "nowhere", 50);
  r.start();
  r.run(3600000);
  EXPECT_NE(WifiState::Connected, r.state());
  // 5 s, 10 s, ... capped at 300 s: well under 40 scans in an hour, well over 10
  EXPECT_LT(r.scans, 40u);
  EXPECT_GT(r.scans, 10u);
}

TEST(WifiCore, ConnectsAsSoonAsANetworkAppearsAfterBackingOff) {
  Rig r;
  r.profile(0, "late", 50);
  r.start(); r.run(600000);
  ASSERT_NE(WifiState::Connected, r.state());
  r.ap("late");
  r.run(400000);   // longer than the backoff cap
  EXPECT_EQ(WifiState::Connected, r.state());
}

TEST(WifiCore, TheBackoffRestartsSmallAfterASuccessfulJoin) {
  Rig r;
  r.profile(0, "net", 50);
  r.start(); r.run(200000);        // backoff has grown
  r.ap("net"); r.run(400000);
  ASSERT_EQ(WifiState::Connected, r.state());
  r.apDown("net"); r.run(2000);
  r.apUp("net");
  r.run(20000);                     // a fresh, short backoff, not the grown one
  EXPECT_EQ(WifiState::Connected, r.state());
}

// ---- editing profiles

TEST(WifiCore, EditingTheProfileInUseRejoinsAtOnce) {
  Rig r;
  r.profile(0, "old", 50); r.ap("old"); r.ap("new");
  r.start(); r.run(3000);
  ASSERT_STREQ("old", r.current());
  r.prof.setSsid(0, "new");
  r.core.profileChanged(r.now, 0, true);
  r.run(4000);
  EXPECT_STREQ("new", r.current());
}

TEST(WifiCore, ChangingAPriorityDoesNotDropTheLinkButLooksForTheNewBest) {
  Rig r;
  r.profile(0, "a", 90); r.profile(1, "b", 10);
  r.ap("a"); r.ap("b", "", -50, false);
  r.start(); r.run(3000);
  ASSERT_STREQ("a", r.current());
  r.apUp("b");
  r.prof.setPriority(1, 200);
  r.core.profileChanged(r.now, 1, false);
  EXPECT_STREQ("a", r.current());   // not dropped by the edit itself
  r.run(300000);
  EXPECT_STREQ("b", r.current());   // switched after the hold-down
}

TEST(WifiCore, ClearingEveryProfileDropsTheLinkAndStopsScanning) {
  Rig r;
  r.profile(0, "net", 50); r.ap("net");
  r.start(); r.run(3000);
  ASSERT_EQ(WifiState::Connected, r.state());
  r.prof.clear(0);
  r.core.profileChanged(r.now, 0, true);
  r.run(1000);
  EXPECT_EQ(WifiState::Idle, r.state());
  EXPECT_FALSE(r.linked);
  uint32_t scans = r.scans;
  r.run(600000);
  EXPECT_EQ(scans, r.scans);
}

TEST(WifiCore, EditingAProfileForgetsItsCooldown) {
  Rig r;
  r.profile(0, "home", 90, "wrong-pw-fake"); r.profile(1, "phone", 10);
  r.ap("home", "right-pw-fake"); r.ap("phone");
  r.start(); r.run(5000);
  ASSERT_STREQ("phone", r.current());
  r.prof.setPwd(0, "right-pw-fake");
  r.core.profileChanged(r.now, 0, true);
  r.run(30000);   // well inside the 120 s cooldown the failed password had earned, and inside the hold-down
  EXPECT_STREQ("home", r.current());
}

// ---- ten profiles, and the clock wrapping

TEST(WifiCore, TenProfilesAreUsable) {
  Rig r;
  for (uint8_t i = 0; i < 10; i++) {
    std::string n = "net" + std::to_string(i);
    r.profile(i, n.c_str(), 10 + i);
    if (i == 3) r.ap(n.c_str());   // only one of the ten exists
  }
  r.start(); r.run(3000);
  EXPECT_STREQ("net3", r.current());
}

TEST(WifiCore, WorksAcrossTheMillisWrap) {
  Rig r(0xFFFFFFFFu - 20000);
  r.profile(0, "home", 90); r.profile(1, "phone", 10);
  r.ap("phone");
  r.start(); r.run(3000);
  ASSERT_STREQ("phone", r.current());
  r.apUp("home");
  r.run(300000);   // crosses 2^32
  EXPECT_STREQ("home", r.current());
}

// ---- status

TEST(WifiCore, ReportsTimeOnTheCurrentNetworkAndTheLastFailurePerProfile) {
  Rig r;
  r.profile(0, "home", 90, "wrong-pw-fake"); r.profile(1, "phone", 10);
  r.ap("home", "right-pw-fake"); r.ap("phone");
  r.start(); r.run(5000);
  uint32_t t0 = r.core.connectedMs(r.now);
  r.run(10000);
  EXPECT_GE(r.core.connectedMs(r.now) - t0, 9900u);
  EXPECT_EQ(WifiFail::AuthFailed, r.core.lastFail(0));
  EXPECT_STREQ("auth", WifiCore::failName(r.core.lastFail(0)));
  EXPECT_STREQ("connected", WifiCore::stateName(r.state()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
