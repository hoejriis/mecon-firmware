#include <gtest/gtest.h>
#include "helpers/BleDfuPolicy.h"

using BleDfuPolicy::mayRegister;

TEST(BleDfuPolicy, ADeviceInterfaceBuildNeverRegistersIt) {
  for (uint32_t pin : {0u, 123456u, 654321u, 100000u}) EXPECT_FALSE(mayRegister(true, pin)) << pin;
}

TEST(BleDfuPolicy, OtherBuildsOnlyWithAChosenPin) {
  EXPECT_FALSE(mayRegister(false, 0));        // build default: no PIN to speak of
  EXPECT_FALSE(mayRegister(false, 123456));   // the published PIN: anyone in range could pair and flash
  EXPECT_TRUE(mayRegister(false, 654321));    // positive control: a PIN the user chose
  EXPECT_TRUE(mayRegister(false, 100000));
}

TEST(BleDfuPolicy, StartOtaIsRefusedOnInterfaceAndNrf52Builds) {
  EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(true, false));    // an interface build, any board
  EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(true, true));
  EXPECT_FALSE(BleDfuPolicy::otaStartAllowed(false, true));    // nRF52: BLE DFU with no PIN to protect it
  EXPECT_TRUE(BleDfuPolicy::otaStartAllowed(false, false));    // positive control: a stock-like ESP32 build keeps it
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
