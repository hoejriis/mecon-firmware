// Without the LoRa update path (UPD_NO_LORA: an nRF52 build) the four update operations are not carried over LoRa
// (docs/contract/LORA_UPDATES.md section 8); the rest of the list is as before. This file leaves the path out of its own copy
// of the header, which the other tests have in.
#undef UPD_LORA
#define UPD_NO_LORA 1
#include <gtest/gtest.h>
#include "helpers/iface/LoraLink.h"

using namespace Iface;

TEST(LoraOpsWithoutUpdates, TheUpdateOperationsAreUnknownOverLora) {
  for (const char* op : {"update.manifest", "update.manifest.part", "update.offer", "update.status"}) {
    EXPECT_FALSE(loraOpAllowed(op)) << op;
    EXPECT_FALSE(loraChannelOpAllowed(op)) << op;
  }
  EXPECT_TRUE(loraOpAllowed("job.get"));
  EXPECT_TRUE(loraOpAllowed("config.apply"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
