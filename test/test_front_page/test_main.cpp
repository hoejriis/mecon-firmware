#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "helpers/ui/FrontPage.h"

using namespace FrontPage;

static std::vector<std::string> show(const State& st) {
  Row r[ROWS];
  rows(st, r);
  return std::vector<std::string>(r, r + ROWS);
}

TEST(FrontPage, ARepeaterOnWifiAndMqtt) {
  State st;
  st.role = "Repeater"; st.rx_seen = true; st.rx_age_s = 10;
  st.mode = MODE_WIFI; st.wifi_connected = true; st.ssid = "HomeNetwork";
  st.mqtt_connected = true; st.broker = "mqtt.example.net";
  EXPECT_EQ(show(st), (std::vector<std::string>{"Normal Repeater", "Last LoRa: 10 sec ago", "Mode: WiFi", "HomeNetwork",
                                                "mqtt.example.net"}));
}

TEST(FrontPage, WifiChosenButNotJoinedSaysSoAndNamesNoNetwork) {
  State st;
  st.role = "Repeater"; st.rx_seen = true; st.rx_age_s = 150;
  st.mode = MODE_WIFI; st.ssid = "HomeNetwork";   // the profile is known, but the link is down
  EXPECT_EQ(show(st), (std::vector<std::string>{"Normal Repeater", "Last LoRa: 2 min ago", "Mode: WiFi", "WiFi not connected",
                                                "MQTT not connected"}));
}

TEST(FrontPage, AHardenedCompanionOnBleThatHeardNothing) {
  State st;
  st.hardened = true; st.role = "Companion";
  st.mode = MODE_BLE; st.ble_connected = true;
  EXPECT_EQ(show(st), (std::vector<std::string>{"Hardened Companion", "Last LoRa: never", "Mode: BLE", "Connected via BLE",
                                                "MQTT not connected"}));
}

TEST(FrontPage, TheCableAndNoLink) {
  State st;
  st.role = "Repeater"; st.mode = MODE_USB; st.usb_connected = true;
  EXPECT_EQ(show(st)[3], "Connected via USB");
  st.usb_connected = false;
  EXPECT_EQ(show(st)[3], "USB not connected");
  st.mode = MODE_BLE;
  EXPECT_EQ(show(st)[3], "BLE not connected");
}

TEST(FrontPage, ABrokerIsShownOnlyForTheDevicesOwnSession) {
  State st;
  st.role = "Companion"; st.mode = MODE_WIFI; st.wifi_connected = true; st.ssid = "Net";
  st.broker = "mqtt.example.net";   // configured, not connected
  EXPECT_EQ(show(st)[4], "MQTT not connected");
}

TEST(FrontPage, AgeBoundaries) {
  char a[24];
  age(0, a, sizeof(a));           EXPECT_STREQ(a, "0 sec ago");
  age(59, a, sizeof(a));          EXPECT_STREQ(a, "59 sec ago");
  age(60, a, sizeof(a));          EXPECT_STREQ(a, "1 min ago");
  age(3599, a, sizeof(a));        EXPECT_STREQ(a, "59 min ago");
  age(3600, a, sizeof(a));        EXPECT_STREQ(a, "1 h ago");
  age(48 * 3600 - 1, a, sizeof(a)); EXPECT_STREQ(a, "47 h ago");
  age(48 * 3600, a, sizeof(a));   EXPECT_STREQ(a, "2 days ago");
}

TEST(FrontPage, EveryRowFitsAndLongTextIsClipped) {
  Row r;
  clip(r, "123456789012345678901");   // exactly COLS
  EXPECT_STREQ(r, "123456789012345678901");
  clip(r, "1234567890123456789012");  // one over
  EXPECT_STREQ(r, "1234567890123456789..");
  // a two-byte character straddling the cut is dropped whole, never split
  clip(r, "123456789012345678\xc3\xa5xyz");
  EXPECT_STREQ(r, "123456789012345678..");
  State st;
  st.role = "Companion"; st.rx_seen = true; st.rx_age_s = 4000000000u;
  st.mode = MODE_WIFI; st.wifi_connected = true; st.ssid = "AVeryLongNetworkNameIndeed";
  st.mqtt_connected = true; st.broker = "broker.with.a.very.long.name.example.net";
  for (const std::string& s : show(st)) EXPECT_LE(s.size(), (size_t)COLS) << s;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
