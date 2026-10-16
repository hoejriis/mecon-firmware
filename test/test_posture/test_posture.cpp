#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include "helpers/Posture.h"

using namespace Posture;

// ---- the posture byte

TEST(Posture, AnyValueButNormalIsHardened) {
  EXPECT_FALSE(isHardened(NORMAL));
  EXPECT_TRUE(isHardened(HARDENED));
  EXPECT_TRUE(isHardened(2));      // a damaged byte only makes the device stricter
  EXPECT_TRUE(isHardened(255));
}

// ---- start-up decisions

TEST(Posture, BleWithADefaultPinStartsOnlyInNormal) {
  EXPECT_TRUE(bleMayStart(NORMAL, 0));
  EXPECT_TRUE(bleMayStart(NORMAL, 123456));
  EXPECT_FALSE(bleMayStart(HARDENED, 0));
  EXPECT_FALSE(bleMayStart(HARDENED, 123456));
  EXPECT_TRUE(bleMayStart(HARDENED, 654321));     // positive control: a chosen PIN
  EXPECT_TRUE(bleMayStart(HARDENED, 100000));
  EXPECT_FALSE(bleMayStart(2, 123456));           // damaged byte = hardened
}

TEST(Posture, TcpServersStartOnlyInNormal) {
  EXPECT_TRUE(tcpServerMayStart(NORMAL));
  EXPECT_FALSE(tcpServerMayStart(HARDENED));
  EXPECT_FALSE(tcpServerMayStart(2));
}

// ---- per-command decisions

TEST(Posture, KeyTransferIsRefusedInHardenedOnEveryTransport) {
  EXPECT_TRUE(keyTransferAllowed(NORMAL));
  EXPECT_FALSE(keyTransferAllowed(HARDENED));
}

TEST(Posture, OnlyACableFactoryResetsAHardenedDevice) {
  EXPECT_TRUE(factoryResetAllowed(HARDENED, CALLER_CABLE));
  EXPECT_FALSE(factoryResetAllowed(HARDENED, CALLER_BLE));
  EXPECT_FALSE(factoryResetAllowed(HARDENED, CALLER_NET));
  EXPECT_TRUE(factoryResetAllowed(NORMAL, CALLER_BLE));    // Normal: any trusted transport, as before
}

TEST(Posture, DefaultAdminPasswordIsRecognised) {
  EXPECT_TRUE(isDefaultAdminPassword("password", "password"));
  EXPECT_TRUE(isDefaultAdminPassword("password", "other"));      // the stock literal counts whatever the build says
  EXPECT_TRUE(isDefaultAdminPassword("buildpw", "buildpw"));     // so does the build's own default
  EXPECT_TRUE(isDefaultAdminPassword("", "password"));           // an empty password is not a password
  EXPECT_FALSE(isDefaultAdminPassword("s3cret", "password"));
  EXPECT_FALSE(isDefaultAdminPassword("Password", "password"));
}

TEST(Posture, RemoteReadOnlyList) {
  const char* ok[] = {"get name", "get radio", "get dutycycle", "ver", "board", "reboot", "clock", "neighbors", "power",
                      "region", "region get x", "sensor get k", "sensor list", "gps", "get posture"};
  for (const char* c : ok) EXPECT_TRUE(isRemoteReadOnly(c)) << c;
  const char* no[] = {"set name x", "password x", "setperm aa 1", "advert", "advert.zerohop", "start ota", "clock sync",
                      "erase", "discover.neighbors", "tempradio 1,2,3,4,5", "region load", "region put a", "poweroff",
                      "shutdown", "set posture normal confirm", "set prv.key 00", "log start", "sensor set a b",
                      "gps on", "powersaving on", "clear stats", "reboot now", "time 5", "neighbor.remove aa"};
  for (const char* c : no) EXPECT_FALSE(isRemoteReadOnly(c)) << c;
}

// ---- posture / audit commands

struct Ctl : public ::testing::Test {
  uint8_t posture = NORMAL;
  char reply[160];
  bool changed = false;
  void SetUp() override { audit().clear(); }
  std::string run(const char* cmd, uint8_t caller, bool* handled = nullptr) {
    reply[0] = 0;
    changed = false;
    bool h = handleCommand(cmd, caller, posture, 1000, reply, 150, changed);
    if (handled) *handled = h;
    return reply;
  }
  int count(uint8_t ev, uint8_t caller, uint8_t what) {
    for (int i = 0; i < audit().n; i++) {
      auto& e = audit().e[i];
      if (e.event == ev && e.caller == caller && e.what == what) return e.count;
    }
    return 0;
  }
};

TEST_F(Ctl, UnrelatedCommandsAreNotHandled) {
  bool h = true;
  run("get name", CALLER_CABLE, &h);
  EXPECT_FALSE(h);
  run("set postures hardened", CALLER_CABLE, &h);
  EXPECT_FALSE(h);
}

TEST_F(Ctl, GetPostureAnswersEveryone) {
  EXPECT_EQ(run("get posture", CALLER_REMOTE_ADMIN), "> normal");
  posture = HARDENED;
  EXPECT_EQ(run("get posture", CALLER_REMOTE_CLI), "> hardened");
}

TEST_F(Ctl, RaiseFromCableAndBle) {
  EXPECT_EQ(run("set posture hardened", CALLER_CABLE), "OK - posture hardened");
  EXPECT_TRUE(changed);
  EXPECT_EQ(posture, HARDENED);
  posture = NORMAL;
  EXPECT_EQ(run("set posture hardened", CALLER_BLE), "OK - posture hardened");
  EXPECT_EQ(posture, HARDENED);
  EXPECT_EQ(count(EV_POSTURE_SET, CALLER_BLE, HARDENED), 1);
}

TEST_F(Ctl, RaisingTwiceChangesNothing) {
  run("set posture hardened", CALLER_CABLE);
  EXPECT_EQ(run("set posture hardened", CALLER_CABLE), "OK - already hardened");
  EXPECT_FALSE(changed);
}

TEST_F(Ctl, NobodyRemoteChangesThePostureEitherWay) {
  for (uint8_t c : {CALLER_REMOTE_ADMIN, CALLER_REMOTE_CLI, CALLER_NET}) {
    EXPECT_EQ(run("set posture hardened", c), "ERR: not permitted");
    EXPECT_FALSE(changed);
    EXPECT_EQ(posture, NORMAL);
  }
  posture = HARDENED;
  for (uint8_t c : {CALLER_REMOTE_ADMIN, CALLER_REMOTE_CLI, CALLER_NET}) {
    EXPECT_EQ(run("set posture normal confirm", c), "ERR: not permitted");
    EXPECT_EQ(posture, HARDENED);
  }
  EXPECT_EQ(count(EV_REFUSED, CALLER_REMOTE_ADMIN, W_POSTURE), 2);
}

TEST_F(Ctl, LoweringNeedsACableAndTheConfirmToken) {
  posture = HARDENED;
  EXPECT_EQ(run("set posture normal confirm", CALLER_BLE), "ERR: not permitted");   // a paired phone is not enough
  EXPECT_EQ(posture, HARDENED);
  EXPECT_NE(run("set posture normal", CALLER_CABLE).find("confirm"), std::string::npos);
  EXPECT_EQ(posture, HARDENED);
  EXPECT_EQ(run("set posture normal confirm", CALLER_CABLE), "OK - posture normal");
  EXPECT_EQ(posture, NORMAL);
  EXPECT_TRUE(changed);
  EXPECT_EQ(count(EV_POSTURE_SET, CALLER_CABLE, NORMAL), 1);
}

TEST_F(Ctl, DamagedPostureByteCanBeLoweredFromACableOnly) {
  posture = 7;
  EXPECT_EQ(run("get posture", CALLER_CABLE), "> hardened");
  EXPECT_EQ(run("set posture normal confirm", CALLER_CABLE), "OK - posture normal");
  EXPECT_EQ(posture, NORMAL);
}

TEST_F(Ctl, AuditIsReadableOnTrustedLocalTransportsOnly) {
  run("set posture hardened", CALLER_CABLE);
  EXPECT_NE(run("get audit", CALLER_CABLE).find("1 entries"), std::string::npos);
  EXPECT_NE(run("get audit", CALLER_BLE).find("1 entries"), std::string::npos);
  EXPECT_EQ(run("get audit", CALLER_REMOTE_ADMIN), "ERR: not permitted");
  EXPECT_EQ(run("get audit 0", CALLER_REMOTE_CLI), "ERR: not permitted");
  EXPECT_EQ(count(EV_REFUSED, CALLER_REMOTE_ADMIN, W_AUDIT), 1);
}

// ---- the front door of Repeater / Room server

struct Door : public ::testing::Test {
  uint8_t posture = NORMAL;
  char reply[160];
  bool changed = false;
  void SetUp() override { audit().clear(); }
  bool run(const char* cmd, bool remote, const char* pw = "s3cret") {
    reply[0] = 0;
    changed = false;
    return adminFrontDoor(cmd, remote, posture, pw, "password", 1000, reply, 150, changed);
  }
};

TEST_F(Door, DefaultPasswordRefusesRemoteAdminInBothPostures) {
  EXPECT_TRUE(run("get name", true, "password"));
  EXPECT_NE(std::string(reply).find("password"), std::string::npos);
  posture = HARDENED;
  EXPECT_TRUE(run("get name", true, "password"));
  EXPECT_TRUE(run("get name", true, ""));
  EXPECT_FALSE(run("get name", true));   // positive control: a set password lets a read through
}

TEST_F(Door, DefaultPasswordNeverBlocksTheLocalConsole) {
  EXPECT_FALSE(run("set name x", false, "password"));
  posture = HARDENED;
  EXPECT_FALSE(run("set name x", false, "password"));
}

TEST_F(Door, NormalPostureLeavesRemoteAdminAsStock) {
  EXPECT_FALSE(run("set name x", true));
  EXPECT_FALSE(run("password newone", true));
  EXPECT_FALSE(run("advert", true));
}

TEST_F(Door, HardenedRemoteAdminIsReadOnlyPlusReboot) {
  posture = HARDENED;
  EXPECT_FALSE(run("get name", true));
  EXPECT_FALSE(run("ver", true));
  EXPECT_FALSE(run("reboot", true));
  for (const char* c : {"set name x", "password x", "setperm aa 1", "advert", "start ota", "erase"}) {
    EXPECT_TRUE(run(c, true)) << c;
    EXPECT_NE(std::string(reply).find("hardened"), std::string::npos) << c;
  }
  EXPECT_EQ(audit().n, 1);   // all of the above fold into one kind
  EXPECT_EQ(audit().e[0].event, EV_REFUSED);
  EXPECT_EQ(audit().e[0].caller, CALLER_REMOTE_ADMIN);
  EXPECT_EQ(audit().e[0].what, W_REMOTE_WRITE);
  EXPECT_EQ(audit().e[0].count, 6);
}

TEST_F(Door, HardenedDoesNotRestrictTheLocalConsole) {
  posture = HARDENED;
  EXPECT_FALSE(run("set name x", false));
  EXPECT_FALSE(run("password x", false));
}

TEST_F(Door, RemoteCannotLowerAndIsAudited) {
  posture = HARDENED;
  EXPECT_TRUE(run("set posture normal confirm", true));
  EXPECT_EQ(posture, HARDENED);
  EXPECT_EQ(audit().e[audit().n - 1].what, W_POSTURE);
}

// ---- the audit ring

struct Ring : public ::testing::Test {
  AuditRing r;
};

TEST_F(Ring, RepeatsAreFoldedAndMovedToNewest) {
  r.add(10, EV_REFUSED, CALLER_REMOTE_ADMIN, W_REMOTE_WRITE);
  r.add(11, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_BAD_PASSWORD);
  r.add(12, EV_REFUSED, CALLER_REMOTE_ADMIN, W_REMOTE_WRITE);
  ASSERT_EQ(r.n, 2);
  EXPECT_EQ(r.e[0].event, EV_LOGIN_FAILED);
  EXPECT_EQ(r.e[1].event, EV_REFUSED);
  EXPECT_EQ(r.e[1].count, 2);
  EXPECT_EQ(r.e[1].ts, 12u);
}

TEST_F(Ring, CountSaturates) {
  for (int i = 0; i < 300; i++) r.add(i, EV_REFUSED, CALLER_NET, W_CLI);
  EXPECT_EQ(r.n, 1);
  EXPECT_EQ(r.e[0].count, 255);
}

TEST_F(Ring, AFloodOfOneKindCannotEvictAPostureChange) {
  r.add(1, EV_POSTURE_SET, CALLER_CABLE, HARDENED);
  for (int i = 0; i < 1000; i++) r.add(100 + i, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_BAD_PASSWORD);
  ASSERT_EQ(r.n, 2);
  EXPECT_EQ(r.e[0].event, EV_POSTURE_SET);
}

TEST_F(Ring, FullRingDropsTheOldest) {
  for (int i = 0; i < AuditRing::CAPACITY + 5; i++) r.add(i, EV_REFUSED, CALLER_NET, (uint8_t)(100 + i));
  ASSERT_EQ(r.n, AuditRing::CAPACITY);
  EXPECT_EQ(r.e[0].what, 105);
  EXPECT_EQ(r.e[AuditRing::CAPACITY - 1].what, 100 + AuditRing::CAPACITY + 4);
}

TEST_F(Ring, AddOnceIgnoresAKindAlreadyThere) {
  r.addOnce(1, EV_REFUSED, CALLER_BLE, W_BLE_START);
  r.addOnce(2, EV_REFUSED, CALLER_BLE, W_BLE_START);
  ASSERT_EQ(r.n, 1);
  EXPECT_EQ(r.e[0].count, 1);
  EXPECT_EQ(r.e[0].ts, 1u);
}

TEST_F(Ring, FileImageRoundTrips) {
  r.add(5, EV_POSTURE_SET, CALLER_CABLE, HARDENED);
  r.add(6, EV_REFUSED, CALLER_REMOTE_CLI, W_REMOTE_CLI);
  r.add(6, EV_REFUSED, CALLER_REMOTE_CLI, W_REMOTE_CLI);
  uint8_t buf[AuditRing::FILE_SIZE];
  EXPECT_EQ(r.serialize(buf), (size_t)268);
  AuditRing b;
  ASSERT_TRUE(b.deserialize(buf, sizeof(buf)));
  ASSERT_EQ(b.n, 2);
  EXPECT_EQ(b.e[1].count, 2);
  EXPECT_EQ(b.e[0].ts, 5u);
  EXPECT_FALSE(b.dirty());
}

TEST_F(Ring, DamagedFileIsRejectedEverywhere) {
  r.add(5, EV_POSTURE_SET, CALLER_CABLE, HARDENED);
  uint8_t buf[AuditRing::FILE_SIZE];
  r.serialize(buf);
  for (size_t i = 0; i < sizeof(buf); i++) {   // any single flipped byte, anywhere
    uint8_t save = buf[i];
    buf[i] ^= 0x01;
    AuditRing b;
    EXPECT_FALSE(b.deserialize(buf, sizeof(buf))) << "byte " << i;
    EXPECT_EQ(b.n, 0);
    buf[i] = save;
  }
  AuditRing b;
  EXPECT_FALSE(b.deserialize(buf, sizeof(buf) - 1));
  EXPECT_TRUE(b.deserialize(buf, sizeof(buf)));
}

TEST_F(Ring, FlushPolicy) {
  EXPECT_FALSE(r.shouldFlush(0));                          // nothing to write
  r.add(1, EV_REFUSED, CALLER_NET, W_CLI);
  EXPECT_TRUE(r.shouldFlush(5));                           // the first after a boot goes at once
  r.markFlushed(5);
  EXPECT_FALSE(r.shouldFlush(6));
  r.add(2, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_BAD_PASSWORD);
  EXPECT_FALSE(r.shouldFlush(5 + AuditRing::FLUSH_INTERVAL_MS - 1));    // an ordinary event waits for the interval
  EXPECT_TRUE(r.shouldFlush(5 + AuditRing::FLUSH_INTERVAL_MS));
  r.add(3, EV_POSTURE_SET, CALLER_CABLE, HARDENED);
  EXPECT_TRUE(r.shouldFlush(6));                           // a posture change does not wait
  r.markFlushed(6);
  r.add(4, EV_FACTORY_RESET, CALLER_CABLE, W_NONE);
  EXPECT_TRUE(r.shouldFlush(7));                           // nor does a factory reset
}

TEST_F(Ring, FailedWriteIsRetriedAfterTheIntervalNotEveryPass) {
  r.add(1, EV_POSTURE_SET, CALLER_CABLE, HARDENED);
  r.markFlushed(100, false);
  EXPECT_TRUE(r.dirty());
  EXPECT_FALSE(r.shouldFlush(101));
  EXPECT_TRUE(r.shouldFlush(100 + AuditRing::FLUSH_INTERVAL_MS));
}

TEST_F(Ring, FlushTimerSurvivesMillisWrap) {
  r.add(1, EV_REFUSED, CALLER_NET, W_CLI);
  r.markFlushed(0xFFFFFFF0u);
  r.add(2, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_BAD_PASSWORD);
  EXPECT_FALSE(r.shouldFlush(0x10));   // 32 ms later across the wrap
}

TEST_F(Ring, PagesNewestFirstTwoAtATime) {
  for (int i = 0; i < 5; i++) r.add(100 + i, EV_REFUSED, CALLER_NET, (uint8_t)(1 + i));
  char out[160];
  r.formatPage(0, out, 150);
  EXPECT_NE(strstr(out, "> 5 entries"), nullptr);
  EXPECT_NE(strstr(out, "104 refused net factory-reset"), nullptr);   // newest first (what 5)
  EXPECT_NE(strstr(out, "103 refused net set-pin"), nullptr);
  r.formatPage(0, out, 150);
  EXPECT_NE(strstr(out, "... next:2"), nullptr);
  r.formatPage(2, out, 150);
  EXPECT_EQ(strstr(out, "entries"), nullptr);
  EXPECT_NE(strstr(out, "... next:4"), nullptr);
  r.formatPage(4, out, 150);
  EXPECT_EQ(strstr(out, "next"), nullptr);
  r.formatPage(99, out, 150);
  EXPECT_STREQ(out, "");
}

TEST_F(Ring, LongestPageFitsTheReplyBuffer) {
  r.add(4294967295u, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_DEFAULT_PASSWORD);
  for (int i = 0; i < 255; i++) r.add(4294967295u, EV_LOGIN_FAILED, CALLER_REMOTE_ADMIN, W_DEFAULT_PASSWORD);
  r.add(4294967295u, EV_REFUSED, CALLER_REMOTE_ADMIN, W_DEFAULT_PASSWORD);
  r.add(4294967295u, EV_REFUSED, CALLER_REMOTE_CLI, W_DEFAULT_PASSWORD);
  char out[160];
  memset(out, 'x', sizeof(out));
  r.formatPage(0, out, 150);
  EXPECT_LT(strlen(out), 150u);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
