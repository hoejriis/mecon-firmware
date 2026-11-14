#include <gtest/gtest.h>
#include "helpers/SerialTxGate.h"

// A port whose free room is set by the test; each yield is one millisecond of the device's clock and may drain the port.
struct FakePort {
  int room = 0;
  int drain_after = -1;   // yields until the host reads and the whole FIFO frees; -1: never
  int availableForWrite() const { return room; }
};

struct Rig {
  FakePort port;
  uint32_t now = 1000;
  int yields = 0;
  bool room(size_t need, uint32_t wait = SERIAL_TX_WAIT_MS) {
    return serialRoomFor(port, need, [this] { return now; },
                         [this] {
                           now++;
                           if (++yields == port.drain_after) port.room = 256;
                         },
                         wait);
  }
};

TEST(SerialTxGate, RoomAlreadyThereWritesWithoutWaiting) {
  Rig r;
  r.port.room = 256;
  EXPECT_TRUE(r.room(179));
  EXPECT_EQ(r.yields, 0);
}

TEST(SerialTxGate, ExactlyEnoughRoomIsEnough) {
  Rig r;
  r.port.room = 179;
  EXPECT_TRUE(r.room(179));
  EXPECT_EQ(r.yields, 0);
}

TEST(SerialTxGate, AHostThatReadsSoonIsWaitedFor) {
  Rig r;
  r.port.room = 10;
  r.port.drain_after = 5;
  EXPECT_TRUE(r.room(179));
  EXPECT_EQ(r.yields, 5);
}

// The fault: a host holds the port open and never reads. The loop must get back within the bound, not wait with the host.
TEST(SerialTxGate, AHostThatNeverReadsGivesUpAtTheBound) {
  Rig r;
  r.port.room = 100;   // some room, not enough for the frame: never a partial write
  EXPECT_FALSE(r.room(179));
  EXPECT_EQ(r.now - 1000, SERIAL_TX_WAIT_MS);
}

TEST(SerialTxGate, TheBoundHoldsAcrossTheClockWrapping) {
  Rig r;
  r.now = 0xFFFFFFF8u;
  EXPECT_FALSE(r.room(179, 20));
  EXPECT_EQ((uint32_t)(r.now - 0xFFFFFFF8u), 20u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
