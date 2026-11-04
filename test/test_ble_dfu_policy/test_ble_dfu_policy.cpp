#include <gtest/gtest.h>
#include "helpers/BleDfuPolicy.h"

using BleDfuPolicy::mayRegister;

TEST(BleDfuPolicy, ADeviceInterfaceBuildNeverRegistersIt) {
  for (uint32_t pin : {0u, 123456u, 654321u, 100000u})
    for (bool chosen : {false, true}) EXPECT_FALSE(mayRegister(true, pin, chosen)) << pin;
}

TEST(BleDfuPolicy, OtherBuildsOnlyWithAPinTheUserStored) {
  EXPECT_FALSE(mayRegister(false, 0, true));        // build default: no PIN to speak of
  EXPECT_FALSE(mayRegister(false, 123456, true));   // the published PIN: anyone in range could pair and flash
  EXPECT_FALSE(mayRegister(false, 654321, false));  // a random session PIN, shown on the screen: not the owner's secret
  EXPECT_TRUE(mayRegister(false, 654321, true));    // positive control: a PIN the user chose and stored
  EXPECT_TRUE(mayRegister(false, 100000, true));
}

TEST(BleDfuPolicy, StartOtaIsOffUnlessTheBuildOptsIn) {
  EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(false, false, false));   // a stock-like ESP32 build: off by default
  EXPECT_TRUE(BleDfuPolicy::otaStartAllowed(false, false, true));     // positive control: opted in
}

TEST(BleDfuPolicy, StartOtaIsRefusedOnInterfaceAndNrf52BuildsEvenOptedIn) {
  for (bool opted : {false, true}) {
    EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(true, false, opted));   // an interface build, any board
    EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(true, true, opted));
    EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(false, true, opted));   // nRF52: BLE DFU with no PIN to protect it
  }
}

TEST(BleDfuPolicy, AnIdleUpdaterStopsAfterTheIdlePeriodUnlessAnUploadRuns) {
  const uint32_t idle = BleDfuPolicy::OTA_IDLE_MS;
  EXPECT_FALSE(BleDfuPolicy::otaIdleStop(1000 + idle - 1, 1000, false));
  EXPECT_TRUE(BleDfuPolicy::otaIdleStop(1000 + idle, 1000, false));
  EXPECT_FALSE(BleDfuPolicy::otaIdleStop(1000 + idle, 1000, true));   // never during an upload
  EXPECT_TRUE(BleDfuPolicy::otaIdleStop(idle - 1, 0xFFFFFFFFu, false));   // across the millis() wrap
  EXPECT_FALSE(BleDfuPolicy::otaIdleStop(5, 0xFFFFFFFFu, false));
}

TEST(BleDfuPolicy, ASecondStartReportsTheRunningSession) {
  EXPECT_TRUE(BleDfuPolicy::otaStartNewSession(false));
  EXPECT_FALSE(BleDfuPolicy::otaStartNewSession(true));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
