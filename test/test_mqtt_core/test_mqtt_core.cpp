// Decision logic of the MQTT controller (src/helpers/mqtt/MqttCore.*), driven by a fake network that behaves like the
// platform layer does: a browse takes 200 ms and reports the responders the test defines, a connect ends at once in
// success or in the failure the test chose, and a session drops when the test says so. No real time, no platform.
#include <gtest/gtest.h>
#include <string.h>
#include <functional>
#include <string>
#include <vector>
#include "helpers/mqtt/MqttCore.h"

namespace {

const uint32_t A1 = 0x0A000001, A2 = 0x0A000002, A3 = 0x0A000003;

struct Resp { uint32_t addr; uint16_t port; bool secure; std::string site; };

struct Rig {
  MqttProfiles prof;
  MqttTiming t;
  MqttCore core;
  uint32_t now;
  std::vector<uint8_t> connects;      // profile of every Connect action, in order
  std::vector<uint32_t> cand_addrs;   // address of the candidate of every Connect to a discovery profile
  int browses = 0, disconnects = 0;
  std::vector<Resp> responders;
  std::function<MqttFail(uint8_t, uint32_t)> outcome = [](uint8_t, uint32_t) { return MqttFail::None; };
  bool browse_running = false; uint32_t browse_end = 0;
  bool linked = true;

  explicit Rig(uint32_t start = 1000) : now(start) {}

  void stat(uint8_t i, const char* host, uint8_t prio, bool tls = true) {
    MqttProfile& q = prof.p[i];
    q.kind = MqttProfile::Static; strcpy(q.host, host); q.prio = prio; q.tls = tls ? 1 : 0;
  }
  void disc(uint8_t i, const char* site, uint8_t prio) {
    MqttProfile& q = prof.p[i];
    q.kind = MqttProfile::Discovery; strcpy(q.site, site); q.prio = prio;
    memset(q.anchor, 'a', 64); q.anchor[64] = 0;
  }
  void start() { core.configure(&prof, t); core.setLink(now, linked); core.begin(now); core.setLink(now, linked); }
  uint8_t cur() { return core.current(); }
  int connectsTo(uint8_t p) { int n = 0; for (uint8_t c : connects) n += c == p; return n; }

  void act(MqttAction a) {
    switch (a.kind) {
      case MqttAction::Browse: browses++; browse_running = true; browse_end = now + 200; break;
      case MqttAction::Connect: {
        connects.push_back(a.profile);
        uint32_t addr = 0;
        if (a.cand != MqttCore::NONE) { MqttCandidate c; ASSERT_TRUE(core.candidate(a.cand, c)); addr = c.addr; cand_addrs.push_back(addr); }
        MqttFail f = outcome(a.profile, addr);
        if (f == MqttFail::None) core.onConnected(now, true);
        else core.onConnectFailed(now, f);
        break;
      }
      case MqttAction::Disconnect: disconnects++; break;
      default: break;
    }
  }
  void tick() {   // advance 100 ms
    now += 100;
    if (browse_running && (int32_t)(now - browse_end) >= 0) {
      browse_running = false;
      for (auto& r : responders) core.browseObserve(r.addr, r.port, r.secure, r.site.c_str());
      core.browseDone(now);
    }
    act(core.poll(now));
  }
  void run(uint32_t ms) { for (uint32_t e = now + ms; (int32_t)(now - e) < 0;) tick(); }
  void runUntilConnected(uint32_t max_ms = 30000) { for (uint32_t e = now + max_ms; core.state() != MqttState::Connected && (int32_t)(now - e) < 0;) tick(); }
};

}  // namespace

// ------------------------------------------------------------------ selection

TEST(MqttCore, TheHighestPriorityProfileWins) {
  Rig r;
  r.stat(0, "low", 10); r.stat(1, "high", 90); r.stat(2, "mid", 50);
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_EQ((std::vector<uint8_t>{1}), r.connects);   // no detour through the others
}

TEST(MqttCore, EqualPrioritiesGoToTheLowerProfileNumber) {
  Rig r;
  r.stat(0, "a", 50); r.stat(1, "b", 50); r.stat(2, "c", 50);
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(0, r.cur());
}

TEST(MqttCore, AnIncompleteProfileIsNeverUsed) {
  Rig r;
  r.prof.p[0].kind = MqttProfile::Static; r.prof.p[0].prio = 99;   // no host
  r.prof.p[1].kind = MqttProfile::Discovery; r.prof.p[1].prio = 98; strcpy(r.prof.p[1].site, "x");   // no anchor
  r.stat(2, "ok", 10);
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(2, r.cur());
}

TEST(MqttCore, NothingConfiguredDoesNothing) {
  Rig r;
  r.start();
  r.run(10000);
  EXPECT_EQ(MqttState::Idle, r.core.state());
  EXPECT_TRUE(r.connects.empty());
  EXPECT_EQ(0, r.browses);
}

// ------------------------------------------------------------------ failover

TEST(MqttCore, AFailedConnectFallsBackToTheNextProfile) {
  Rig r;
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  r.outcome = [](uint8_t p, uint32_t) { return p == 0 ? MqttFail::Tcp : MqttFail::None; };
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_EQ((std::vector<uint8_t>{0, 1}), r.connects);
  EXPECT_EQ(MqttFail::Tcp, r.core.lastFail(0));
}

TEST(MqttCore, EveryFailureKindFallsBack) {
  const MqttFail kinds[] = {MqttFail::Dns, MqttFail::Tcp, MqttFail::Tls, MqttFail::Pin, MqttFail::Auth, MqttFail::Timeout};
  for (MqttFail k : kinds) {
    Rig r;
    r.stat(0, "main", 90); r.stat(1, "backup", 50);
    r.outcome = [k](uint8_t p, uint32_t) { return p == 0 ? k : MqttFail::None; };
    r.start();
    r.runUntilConnected();
    EXPECT_EQ(1, r.cur()) << (int)k;
    EXPECT_EQ(k, r.core.lastFail(0));
  }
}

TEST(MqttCore, AFailedProfileIsNotHammered) {
  Rig r;
  r.stat(0, "main", 90);
  r.outcome = [](uint8_t, uint32_t) { return MqttFail::Tcp; };
  r.start();
  r.run(14000);
  EXPECT_EQ(1u, r.connects.size());    // 15 s cooldown after the first failure
  r.run(2000);
  EXPECT_EQ(2u, r.connects.size());
  r.run(29000);
  EXPECT_EQ(2u, r.connects.size());    // the second failure doubled it to 30 s
  r.run(2000);
  EXPECT_EQ(3u, r.connects.size());
  EXPECT_EQ(MqttState::Backoff, r.core.state());
}

TEST(MqttCore, TheFailureCooldownIsCapped) {
  Rig r;
  r.t.fail_cooldown_max_ms = 40000;
  r.stat(0, "main", 90);
  r.outcome = [](uint8_t, uint32_t) { return MqttFail::Tcp; };
  r.start();
  r.run(15000 + 30000 + 40000 + 1000);   // 15, 30, then capped at 40
  size_t n = r.connects.size();
  r.run(39000);
  EXPECT_EQ(n, r.connects.size());
  r.run(2000);
  EXPECT_EQ(n + 1, r.connects.size());
}

TEST(MqttCore, RefusedCredentialsEscalateAndAreNotRetriedEveryMinute) {
  Rig r;
  r.stat(0, "main", 90);
  r.outcome = [](uint8_t, uint32_t) { return MqttFail::Auth; };
  r.start();
  r.run(119000);
  EXPECT_EQ(1u, r.connects.size());
  r.run(2000);
  EXPECT_EQ(2u, r.connects.size());
  r.run(239000);
  EXPECT_EQ(2u, r.connects.size());    // doubled to 240 s
  r.run(2000);
  EXPECT_EQ(3u, r.connects.size());
}

TEST(MqttCore, ASessionThatDropsFallsBackAtOnce) {
  Rig r;
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(0, r.cur());
  r.run(40000);                         // a good session
  r.core.onSessionLost(r.now, MqttFail::Dropped);
  r.run(300);
  EXPECT_EQ(1, r.cur());                // main is cooling for the retry time
  EXPECT_EQ(MqttFail::Dropped, r.core.lastFail(0));
}

TEST(MqttCore, TheOnlyProfileIsRetriedSoonAfterAGoodSessionDropped) {
  Rig r;
  r.stat(0, "main", 90);
  r.start();
  r.runUntilConnected();
  r.run(40000);
  r.core.onSessionLost(r.now, MqttFail::Dropped);
  r.run(4000);
  EXPECT_EQ(1u, r.connects.size());
  r.run(2000);
  EXPECT_EQ(2u, r.connects.size());     // after 5 s, not after the 15 s a failure costs
}

TEST(MqttCore, ASessionThatDiesYoungCountsAsAFailure) {
  Rig r;
  r.stat(0, "main", 90);
  r.start();
  r.runUntilConnected();
  r.run(1000);
  r.core.onSessionLost(r.now, MqttFail::Dropped);   // after 1 s: not a good session
  uint32_t n = r.connects.size();
  r.run(14000);
  EXPECT_EQ(n, r.connects.size());                  // waits the failure cooldown, not the 5 s retry
  r.run(2000);
  EXPECT_EQ(n + 1, r.connects.size());
}

// ------------------------------------------------------------------ return to a better profile

TEST(MqttCore, ReturnsToTheBetterProfileOnlyAfterTheHoldDownAndOnlyOnce) {
  Rig r;
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  bool main_up = false;
  r.outcome = [&](uint8_t p, uint32_t) { return (p == 0 && !main_up) ? MqttFail::Tcp : MqttFail::None; };
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(1, r.cur());
  uint32_t on_backup = r.now;
  main_up = true;
  while (r.now - on_backup < 59000) r.tick();
  EXPECT_EQ(1, r.cur());                       // the session is not yet a hold-down old
  EXPECT_EQ(1, r.connectsTo(1));
  while (r.now - on_backup < 62000) r.tick();
  EXPECT_EQ(0, r.cur());
  r.run(300000);
  EXPECT_EQ(0, r.cur());
  EXPECT_EQ(1, r.connectsTo(1));               // backup once
  EXPECT_EQ(2, r.connectsTo(0));               // main: the failed first try, the return
  EXPECT_EQ(1, r.disconnects);
}

TEST(MqttCore, AFlappingBetterProfileCostsAtMostOneInterruptionPerHoldDown) {
  Rig r;
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  bool main_up = false;
  r.outcome = [&](uint8_t p, uint32_t) { return (p == 0 && !main_up) ? MqttFail::Tcp : MqttFail::None; };
  r.start();
  r.runUntilConnected();
  uint32_t t0 = r.now;
  for (int k = 0; k < 20; k++) {               // main comes and goes every 10 s for 200 s
    main_up = (k % 2) == 0;
    r.run(10000);
    if (r.core.state() == MqttState::Connected && r.cur() == 0) r.core.onSessionLost(r.now, MqttFail::Dropped);
  }
  EXPECT_LE(r.disconnects, (int)((r.now - t0) / 60000) + 1);
}

TEST(MqttCore, TheHoldDownIsNotAppliedToTheFirstConnect) {
  Rig r;
  r.stat(0, "main", 90);
  r.start();
  r.run(300);
  EXPECT_EQ(MqttState::Connected, r.core.state());
}

// ------------------------------------------------------------------ edits

TEST(MqttCore, EditingTheProfileInUseReconnectsAtOnce) {
  Rig r;
  r.stat(0, "main", 90);
  r.start();
  r.runUntilConnected();
  r.core.profileChanged(r.now, 0, true);
  r.run(300);
  EXPECT_EQ(1, r.disconnects);
  EXPECT_EQ(2, r.connectsTo(0));
}

TEST(MqttCore, AnEditForgetsTheCooldown) {
  Rig r;
  r.stat(0, "main", 90);
  r.outcome = [](uint8_t, uint32_t) { return MqttFail::Auth; };
  r.start();
  r.run(1000);
  ASSERT_EQ(1u, r.connects.size());
  r.core.profileChanged(r.now, 0, true);
  r.run(500);
  EXPECT_EQ(2u, r.connects.size());              // not after 120 s
}

TEST(MqttCore, EditingAPriorityAboveTheCurrentSwitchesAtOnce) {
  Rig r;
  r.stat(0, "a", 10); r.stat(1, "b", 50);
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(1, r.cur());
  r.run(2000);                           // far younger than the hold-down
  r.prof.p[0].prio = 90;
  r.core.profileChanged(r.now, 0, false);
  r.run(1000);
  EXPECT_EQ(0, r.cur());                 // the owner's edit does not wait for a hold-down
  EXPECT_EQ(1, r.disconnects);
}

// ------------------------------------------------------------------ link

TEST(MqttCore, AnEndedLinkEndsTheSessionWithoutPenaltyAndTheNextLinkReconnects) {
  Rig r;
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  r.start();
  r.runUntilConnected();
  r.core.setLink(r.now, false);
  r.tick();
  EXPECT_EQ(MqttState::NoLink, r.core.state());
  EXPECT_EQ(1, r.disconnects);
  r.run(5000);
  EXPECT_EQ(1u, r.connects.size());
  r.core.setLink(r.now, true);
  r.run(500);
  EXPECT_EQ(MqttState::Connected, r.core.state());
  EXPECT_EQ(0, r.cur());                         // the same profile: the link, not the broker, was at fault
  EXPECT_EQ(MqttFail::None, r.core.lastFail(0));
}

TEST(MqttCore, StopEndsEverything) {
  Rig r;
  r.stat(0, "main", 90);
  r.start();
  r.runUntilConnected();
  r.core.stop();
  r.run(1000);
  EXPECT_EQ(MqttState::Off, r.core.state());
  EXPECT_EQ(1u, r.connects.size());
}

// ------------------------------------------------------------------ discovery

TEST(MqttCore, AMatchingResponderIsPreferredToALowerStaticProfile) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(0, r.cur());
  EXPECT_EQ((std::vector<uint8_t>{0}), r.connects);   // the browse came first: no connect to the backup
  EXPECT_EQ((std::vector<uint32_t>{A1}), r.cand_addrs);
}

TEST(MqttCore, WithoutAResponderTheStaticProfileIsUsedAfterTheBrowse) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_EQ(1, r.browses);
}

TEST(MqttCore, AResponderWithAnotherSiteIdIsIgnored) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.responders = {{A1, 8883, true, "beta"}, {A2, 8883, true, ""}, {A3, 8883, true, "alph"}};
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_TRUE(r.cand_addrs.empty());
}

TEST(MqttCore, AResponderOfferingPlainMqttIsNeverACandidate) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.responders = {{A1, 1883, false, "alpha"}};
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_TRUE(r.cand_addrs.empty());
  EXPECT_EQ(MqttFail::Plain, r.core.lastFail(0));
}

TEST(MqttCore, ARogueCandidateThatFailsTheCertificateCheckDoesNotShutOutTheRealBroker) {
  Rig r;
  r.disc(0, "alpha", 90);
  r.responders = {{A1, 8883, true, "alpha"}, {A2, 8883, true, "alpha"}};   // A1 is the rogue
  r.outcome = [](uint8_t, uint32_t a) { return a == A1 ? MqttFail::Pin : MqttFail::None; };
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(MqttState::Connected, r.core.state());
  EXPECT_EQ((std::vector<uint32_t>{A1, A2}), r.cand_addrs);
}

TEST(MqttCore, ARogueCandidateIsNotRetriedForAWhile) {
  Rig r;
  r.disc(0, "alpha", 90);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.outcome = [](uint8_t, uint32_t) { return MqttFail::Pin; };
  r.start();
  r.run(299000);
  EXPECT_EQ(1u, r.connects.size());
  r.run(62000);
  EXPECT_EQ(2u, r.connects.size());
  EXPECT_EQ(MqttFail::Pin, r.core.lastFail(0));
}

TEST(MqttCore, ThereIsNoBrowsingOnTheBestProfile) {
  Rig r;
  r.stat(0, "main", 90); r.disc(1, "alpha", 50);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.run(600000);
  EXPECT_EQ(0, r.cur());
  EXPECT_EQ(0, r.browses);
}

TEST(MqttCore, ThereIsNoBrowsingOnTheBestDiscoveryProfile) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.run(600000);
  EXPECT_EQ(0, r.cur());
  EXPECT_EQ(1, r.browses);                // the one before the first connect
}

TEST(MqttCore, BrowsingContinuesWhileOnALowerProfile) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.start();
  r.run(300000);
  EXPECT_EQ(1, r.cur());
  EXPECT_GE(r.browses, 5);
  EXPECT_LE(r.browses, 6);                // one every 60 s
}

TEST(MqttCore, ABetterResponderIsTakenOnlyAfterItHasBeenSeenForTheHoldDown) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(1, r.cur());
  r.responders = {{A1, 8883, true, "alpha"}};
  uint32_t t0 = r.now;
  while (r.now - t0 < 140000 && r.connectsTo(0) == 0) r.tick();     // seen at the next browse (<= 60 s), then 60 s more
  EXPECT_EQ(0, r.cur());
  EXPECT_GE(r.now - t0, 115000u);              // seen from ~61 s on, plus the hold-down
  EXPECT_LE(r.now - t0, 125000u);
  EXPECT_EQ(1, r.connectsTo(1));
  r.run(300000);
  EXPECT_EQ(0, r.cur());
  EXPECT_EQ(1, r.disconnects);
}

TEST(MqttCore, AMissedBrowseRestartsTheHoldDownOfAResponder) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.start();
  r.runUntilConnected();
  r.responders = {{A1, 8883, true, "alpha"}};
  r.run(70000);                           // seen at ~60 s, hold-down running
  ASSERT_EQ(1, r.cur());
  r.responders.clear();
  r.run(60000);                           // the next browse misses it
  r.responders = {{A1, 8883, true, "alpha"}};
  uint32_t t1 = r.now;
  r.run(30000);
  EXPECT_EQ(1, r.cur());                  // it would be 60 s old by now had the miss not restarted the clock
  (void)t1;
}

TEST(MqttCore, ALostResponderFailsOver) {
  Rig r;
  r.disc(0, "never", 99);                 // keeps the device browsing while it is on the lower profile
  r.disc(1, "alpha", 70); r.stat(2, "backup", 10);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(1, r.cur());
  r.responders.clear();                   // the broker leaves the network
  r.run(65000);
  EXPECT_EQ(2, r.cur());
  EXPECT_EQ(MqttFail::Lost, r.core.lastFail(1));
}

TEST(MqttCore, ABrowseThatNeverReportsIsAbandoned) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.start();
  r.tick();
  ASSERT_EQ(1, r.browses);
  r.browse_running = false;               // the platform never answers
  r.run(11000);
  EXPECT_FALSE(r.core.browsing());
  EXPECT_EQ(1, r.cur());                  // the static profile was used after the timeout
}

TEST(MqttCore, ACandidateIsForgottenWhenItsProfileIsEdited) {
  Rig r;
  r.disc(0, "alpha", 90); r.stat(1, "backup", 50);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(0, r.cur());
  r.responders.clear();
  strcpy(r.prof.p[0].site, "beta");
  r.core.profileChanged(r.now, 0, true);
  r.run(20000);
  EXPECT_EQ(1, r.cur());
}

// ------------------------------------------------------------------ posture

TEST(MqttCore, HardenedNeverUsesAPlainStaticProfile) {
  Rig r;
  r.stat(0, "plain", 90, false); r.stat(1, "tls", 50, true);
  r.core.setPosture(Posture::HARDENED);
  r.start();
  r.core.setPosture(Posture::HARDENED);
  r.runUntilConnected();
  EXPECT_EQ(1, r.cur());
  EXPECT_EQ((std::vector<uint8_t>{1}), r.connects);
  EXPECT_EQ(MqttFail::Posture, r.core.lastFail(0));
}

TEST(MqttCore, NormalUsesAPlainStaticProfile) {
  Rig r;
  r.stat(0, "plain", 90, false); r.stat(1, "tls", 50, true);
  r.start();
  r.runUntilConnected();
  EXPECT_EQ(0, r.cur());
}

TEST(MqttCore, RaisingThePostureDropsAPlainSession) {
  Rig r;
  r.stat(0, "plain", 90, false); r.stat(1, "tls", 50, true);
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(0, r.cur());
  r.core.setPosture(Posture::HARDENED);
  r.run(500);
  EXPECT_EQ(1, r.disconnects);
  EXPECT_EQ(1, r.cur());
  EXPECT_EQ(1, r.connectsTo(0));          // never retried
}

TEST(MqttCore, HardenedLeavesDiscoveryProfilesAlone) {
  Rig r;
  r.disc(0, "alpha", 90);
  r.responders = {{A1, 8883, true, "alpha"}};
  r.start();
  r.core.setPosture(Posture::HARDENED);
  r.runUntilConnected();
  EXPECT_EQ(0, r.cur());
}

TEST(MqttCore, AllProfilesForbiddenMeansNoAttempts) {
  Rig r;
  r.stat(0, "plain", 90, false);
  r.core.setPosture(Posture::HARDENED);
  r.start();
  r.core.setPosture(Posture::HARDENED);
  r.run(60000);
  EXPECT_TRUE(r.connects.empty());
}

// ------------------------------------------------------------------ misc

TEST(MqttCore, TheClockMayWrap) {
  Rig r(0xFFFFFFFFu - 20000);
  r.stat(0, "main", 90); r.stat(1, "backup", 50);
  bool main_up = false;
  r.outcome = [&](uint8_t p, uint32_t) { return (p == 0 && !main_up) ? MqttFail::Tcp : MqttFail::None; };
  r.start();
  r.runUntilConnected();
  ASSERT_EQ(1, r.cur());
  main_up = true;
  r.run(150000);                          // across the wrap
  EXPECT_EQ(0, r.cur());
}

TEST(MqttCore, StateAndFailureNames) {
  EXPECT_STREQ("connected", MqttCore::stateName(MqttState::Connected));
  EXPECT_STREQ("no-link", MqttCore::stateName(MqttState::NoLink));
  EXPECT_STREQ("pin", MqttCore::failName(MqttFail::Pin));
  EXPECT_STREQ("posture", MqttCore::failName(MqttFail::Posture));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
