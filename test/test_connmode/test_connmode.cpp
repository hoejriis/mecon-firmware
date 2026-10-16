// Connectivity mode (src/helpers/connmode/ConnMode.h) and the wording of the status screen (ConnStatus.h):
// exactly one radio stack per mode, who may switch, what is audited, and that the screen text never carries a secret.
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "helpers/connmode/ConnMode.h"
#include "helpers/connmode/ConnStatus.h"

using namespace ConnMode;

class ConnModeTest : public ::testing::Test {
protected:
  void SetUp() override { Posture::audit().clear(); }
  static std::string audit_text() {
    char buf[300];
    Posture::audit().formatPage(0, buf, sizeof(buf));
    return buf;
  }
};

TEST_F(ConnModeTest, ExactlyOneStackStartsForEveryStoredByte) {
  for (int m = 0; m < 256; m++) {
    EXPECT_NE(bleMayStart((uint8_t)m), wifiMayStart((uint8_t)m)) << "mode byte " << m;
  }
  EXPECT_TRUE(bleMayStart(BLE));
  EXPECT_TRUE(wifiMayStart(WIFI));
}

TEST_F(ConnModeTest, ADamagedModeByteStartsBle) {
  EXPECT_EQ(sanitize(0), BLE);
  EXPECT_EQ(sanitize(1), WIFI);
  for (int m = 2; m < 256; m++) EXPECT_EQ(sanitize((uint8_t)m), BLE) << m;
  EXPECT_STREQ(name(7), "ble");
  EXPECT_STREQ(name(WIFI), "wifi");
  EXPECT_EQ(other(BLE), WIFI);
  EXPECT_EQ(other(WIFI), BLE);
}

TEST_F(ConnModeTest, TheDefaultPinClassIsZeroAndTheLiteralDefault) {
  EXPECT_TRUE(pinIsDefault(0));
  EXPECT_TRUE(pinIsDefault(123456));
  EXPECT_FALSE(pinIsDefault(123457));
  EXPECT_FALSE(pinIsDefault(654321));
}

TEST_F(ConnModeTest, OnlyTransportsThatAuthenticateByPossessionMaySwitch) {
  EXPECT_TRUE(callerMaySwitch(Posture::CALLER_CABLE));
  EXPECT_TRUE(callerMaySwitch(Posture::CALLER_BLE));
  EXPECT_TRUE(callerMaySwitch(Posture::CALLER_BUTTON));
  EXPECT_FALSE(callerMaySwitch(Posture::CALLER_NET));
  EXPECT_FALSE(callerMaySwitch(Posture::CALLER_REMOTE_ADMIN));
  EXPECT_FALSE(callerMaySwitch(Posture::CALLER_REMOTE_CLI));
  EXPECT_FALSE(callerMaySwitch(0));     // an unset caller fails closed
  EXPECT_FALSE(callerMaySwitch(200));
}

TEST_F(ConnModeTest, ALocalSwitchChangesTheModeAndIsAudited) {
  for (uint8_t caller : {Posture::CALLER_CABLE, Posture::CALLER_BLE}) {
    Posture::audit().clear();
    uint8_t mode = BLE;
    bool changed = false;
    char reply[160];
    ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode wifi", caller, mode, 100, reply, sizeof(reply), changed));
    EXPECT_TRUE(changed);
    EXPECT_EQ(mode, WIFI);
    EXPECT_STREQ(reply, "OK - connectivity.mode wifi, restarting");
    ASSERT_EQ(Posture::audit().n, 1);
    EXPECT_EQ(Posture::audit().e[0].event, Posture::EV_MODE_SET);
    EXPECT_EQ(Posture::audit().e[0].caller, caller);
    EXPECT_EQ(Posture::audit().e[0].what, WIFI);
  }
}

TEST_F(ConnModeTest, TheButtonSwitchesBothWaysAndIsAuditedAsTheButton) {
  uint8_t mode = BLE;
  EXPECT_TRUE(requestSwitch(mode, WIFI, Posture::CALLER_BUTTON, 5));
  EXPECT_EQ(mode, WIFI);
  EXPECT_TRUE(requestSwitch(mode, BLE, Posture::CALLER_BUTTON, 6));
  EXPECT_EQ(mode, BLE);
  EXPECT_NE(audit_text().find("mode button ble"), std::string::npos) << audit_text();
  EXPECT_NE(audit_text().find("mode button wifi"), std::string::npos) << audit_text();
}

TEST_F(ConnModeTest, ARemoteCallerIsRefusedAndTheRefusalIsAudited) {
  for (uint8_t caller : {Posture::CALLER_REMOTE_CLI, Posture::CALLER_REMOTE_ADMIN, Posture::CALLER_NET}) {
    Posture::audit().clear();
    uint8_t mode = BLE;
    bool changed = false;
    char reply[160];
    ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode wifi", caller, mode, 100, reply, sizeof(reply), changed));
    EXPECT_FALSE(changed);
    EXPECT_EQ(mode, BLE) << "caller " << (int)caller;
    EXPECT_STREQ(reply, "ERR: not permitted");
    ASSERT_EQ(Posture::audit().n, 1);
    EXPECT_EQ(Posture::audit().e[0].event, Posture::EV_REFUSED);
    EXPECT_EQ(Posture::audit().e[0].caller, caller);
    EXPECT_EQ(Posture::audit().e[0].what, Posture::W_CONNMODE);
    // a refused bad argument is still a refusal, not a usage hint a remote caller can probe
    ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode junk", caller, mode, 101, reply, sizeof(reply), changed));
    EXPECT_STREQ(reply, "ERR: not permitted");
  }
}

TEST_F(ConnModeTest, AnyoneMayReadTheMode) {
  uint8_t mode = WIFI;
  bool changed = false;
  char reply[160];
  for (uint8_t caller : {Posture::CALLER_REMOTE_CLI, Posture::CALLER_NET, Posture::CALLER_CABLE}) {
    ASSERT_TRUE(ConnMode::handleCommand("get connectivity.mode", caller, mode, 1, reply, sizeof(reply), changed));
    EXPECT_STREQ(reply, "> wifi");
  }
  EXPECT_EQ(Posture::audit().n, 0);   // a read leaves no trace
}

TEST_F(ConnModeTest, BadInputChangesNothingAndOtherCommandsAreNotClaimed) {
  uint8_t mode = BLE;
  bool changed = false;
  char reply[160];
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_STREQ(reply, "ERR: set connectivity.mode ble | wifi");
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode both", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_STREQ(reply, "ERR: set connectivity.mode ble | wifi");
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode wifi extra", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_EQ(mode, BLE);
  EXPECT_FALSE(changed);
  EXPECT_FALSE(ConnMode::handleCommand("set connectivity.modes wifi", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_FALSE(ConnMode::handleCommand("get connectivity", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_FALSE(ConnMode::handleCommand("set posture hardened", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_EQ(Posture::audit().n, 0);
}

TEST_F(ConnModeTest, SwitchingToTheCurrentModeChangesNothing) {
  uint8_t mode = WIFI;
  bool changed = false;
  char reply[160];
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode wifi", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_FALSE(changed);
  EXPECT_STREQ(reply, "OK - already wifi");
  EXPECT_EQ(Posture::audit().n, 0);
}

TEST_F(ConnModeTest, ADamagedStoredByteIsReplacedNotKeptWhenTheStoredModeIsRequested) {
  uint8_t mode = 9;   // reads as ble
  bool changed = false;
  char reply[160];
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode ble", Posture::CALLER_CABLE, mode, 1, reply, sizeof(reply), changed));
  EXPECT_FALSE(changed);                 // 9 already means ble
  ASSERT_TRUE(ConnMode::handleCommand("set connectivity.mode wifi", Posture::CALLER_CABLE, mode, 2, reply, sizeof(reply), changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ(mode, WIFI);
}

TEST_F(ConnModeTest, AModeChangeIsFlushedAtOnceBecauseTheDeviceRestartsRightAfter) {
  Posture::audit().clear();
  Posture::audit().markFlushed(1000);               // an earlier flush, so a mere event would wait ten minutes
  Posture::audit().add(1001, Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_CLI);
  EXPECT_FALSE(Posture::audit().shouldFlush(2000));
  uint8_t mode = BLE;
  EXPECT_TRUE(requestSwitch(mode, WIFI, Posture::CALLER_CABLE, 1002));
  EXPECT_TRUE(Posture::audit().shouldFlush(2000));
}

TEST_F(ConnModeTest, TheAuditEntryRoundTripsThroughTheFileImage) {
  uint8_t mode = BLE;
  EXPECT_TRUE(requestSwitch(mode, WIFI, Posture::CALLER_BUTTON, 42));
  uint8_t img[Posture::AuditRing::FILE_SIZE];
  Posture::audit().serialize(img);
  Posture::AuditRing back;
  ASSERT_TRUE(back.deserialize(img, sizeof(img)));
  ASSERT_EQ(back.n, 1);
  EXPECT_EQ(back.e[0].event, Posture::EV_MODE_SET);
  EXPECT_EQ(back.e[0].caller, Posture::CALLER_BUTTON);
  EXPECT_EQ(back.e[0].what, WIFI);
}

// ------------------------------------------------------------------ the screen text

static std::string join(const ConnStatus& s, bool* any_warn = nullptr) {
  char lines[LINES][LINE_LEN];
  bool warn[LINES];
  int n = formatLines(s, lines, warn);
  std::string out;
  if (any_warn) *any_warn = false;
  for (int i = 0; i < n; i++) {
    out += lines[i];
    out += "\n";
    if (any_warn && warn[i]) *any_warn = true;
  }
  return out;
}

TEST_F(ConnModeTest, BleModeScreenShowsModePostureAndPinClass) {
  ConnStatus s;
  s.mode = BLE;
  s.ble_pin_default = false;
  bool warn;
  std::string t = join(s, &warn);
  EXPECT_NE(t.find("Mode: BLE, Normal"), std::string::npos) << t;
  EXPECT_NE(t.find("Pin: per-device"), std::string::npos) << t;
  EXPECT_NE(t.find("Phone: not connected"), std::string::npos) << t;
  EXPECT_NE(t.find("Wi-Fi, MQTT: off"), std::string::npos) << t;
  EXPECT_FALSE(warn);
  s.ble_connected = true;
  s.posture = Posture::HARDENED;
  t = join(s);
  EXPECT_NE(t.find("Mode: BLE, Hardened"), std::string::npos) << t;
  EXPECT_NE(t.find("Phone: connected"), std::string::npos) << t;
}

TEST_F(ConnModeTest, ADefaultPinIsShownAsAWarningNotAsADigit) {
  ConnStatus s;
  s.mode = BLE;
  s.ble_pin_default = true;
  bool warn;
  std::string t = join(s, &warn);
  EXPECT_NE(t.find("DEFAULT"), std::string::npos) << t;
  EXPECT_TRUE(warn);
  EXPECT_EQ(t.find("123456"), std::string::npos);
  char home[64];
  formatHome(s, home, sizeof(home));
  EXPECT_NE(std::string(home).find("default PIN"), std::string::npos) << home;
  EXPECT_EQ(std::string(home).find("123456"), std::string::npos);
}

TEST_F(ConnModeTest, WifiModeScreenShowsProfileStateRssiAndIp) {
  ConnStatus s;
  s.mode = WIFI;
  s.wifi_has_profile = true;
  s.wifi_running = true;
  s.wifi_connected = true;
  s.wifi_profile = 1;
  strcpy(s.ssid, "Home");
  s.rssi = -61;
  strcpy(s.ip, "10.0.0.7");
  std::string t = join(s);
  EXPECT_NE(t.find("Mode: Wi-Fi, Normal"), std::string::npos) << t;
  EXPECT_NE(t.find("Wi-Fi p1 Home -61dBm"), std::string::npos) << t;
  EXPECT_NE(t.find("IP: 10.0.0.7"), std::string::npos) << t;
  EXPECT_NE(t.find("MQTT: no profile"), std::string::npos) << t;
  s.mqtt_has_profile = true;
  EXPECT_NE(join(s).find("MQTT: off"), std::string::npos);
  char home[64];
  formatHome(s, home, sizeof(home));
  EXPECT_STREQ(home, "Wi-Fi 10.0.0.7");
}

TEST_F(ConnModeTest, WifiModeScreenStatesWithoutALink) {
  ConnStatus s;
  s.mode = WIFI;
  std::string t = join(s);
  EXPECT_NE(t.find("Wi-Fi: no profile"), std::string::npos) << t;
  EXPECT_NE(t.find("IP: -"), std::string::npos) << t;
  s.wifi_has_profile = true;
  t = join(s);
  EXPECT_NE(t.find("Wi-Fi: off"), std::string::npos) << t;   // configured but not started (Hardened without MQTT)
  s.wifi_running = true;
  s.wifi_state = "scanning";
  t = join(s);
  EXPECT_NE(t.find("Wi-Fi: scanning"), std::string::npos) << t;
  s.wifi_state = "connecting";
  s.wifi_profile = 2;
  t = join(s);
  EXPECT_NE(t.find("Wi-Fi p2 connecting"), std::string::npos) << t;
  char home[64];
  formatHome(s, home, sizeof(home));
  EXPECT_STREQ(home, "Wi-Fi: connecting");
}

TEST_F(ConnModeTest, MqttLineSaysProfileStateAndWhetherTlsIsVerified) {
  ConnStatus s;
  s.mode = WIFI;
  s.mqtt_has_profile = true;
  s.mqtt_running = true;
  s.mqtt_state = "backoff";
  std::string t = join(s);
  EXPECT_NE(t.find("MQTT: backoff"), std::string::npos) << t;
  s.mqtt_connected = true;
  s.mqtt_profile = 0;
  s.mqtt_tls = TLS_VERIFIED;
  bool warn;
  t = join(s, &warn);
  EXPECT_NE(t.find("MQTT0 TLS verified"), std::string::npos) << t;
  EXPECT_FALSE(warn);
  s.mqtt_tls = TLS_UNVERIFIED;
  t = join(s, &warn);
  EXPECT_NE(t.find("MQTT0 TLS UNVERIFIED"), std::string::npos) << t;
  EXPECT_TRUE(warn);
  s.mqtt_tls = TLS_PLAIN;
  t = join(s, &warn);
  EXPECT_NE(t.find("MQTT0 plain"), std::string::npos) << t;
}

TEST_F(ConnModeTest, ALongNetworkNameCannotOverflowALine) {
  ConnStatus s;
  s.mode = WIFI;
  s.wifi_has_profile = s.wifi_running = s.wifi_connected = true;
  s.wifi_profile = 9;
  memset(s.ssid, 'S', 32);
  s.ssid[32] = 0;
  s.rssi = -100;
  memset(s.ip, '9', 19);
  char lines[LINES][LINE_LEN];
  bool warn[LINES];
  formatLines(s, lines, warn);
  for (int i = 0; i < LINES; i++) EXPECT_LT(strlen(lines[i]), (size_t)LINE_LEN);
  EXPECT_NE(std::string(lines[1]).find("SSSS"), std::string::npos);
}

TEST_F(ConnModeTest, TheConfirmationNamesTheModeThatWouldBeEntered) {
  EXPECT_STREQ(switchTargetLabel(BLE), "Wi-Fi");
  EXPECT_STREQ(switchTargetLabel(WIFI), "BLE");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
