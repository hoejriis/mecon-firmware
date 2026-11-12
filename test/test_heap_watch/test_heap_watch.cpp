// What the heap was doing at its lowest (src/helpers/HeapWatch.h): a new low of the largest block or of min_free is put
// down to the named work running then, "idle" outside every name or the background's name while one is set, and the
// reply of `get mecon.heaplow`.
#include <gtest/gtest.h>
#include <string>
#include "helpers/HeapWatch.h"
#include "helpers/cli/MeconCli.h"

namespace {
MemStats mem(uint32_t largest, uint32_t min_free, bool known = true) {
  MemStats m;
  m.free = largest + 10000; m.largest = largest; m.min_free = min_free; m.known = known;
  return m;
}
}  // namespace

TEST(HeapWatch, NothingSeenUntilTheFirstLook) {
  HeapWatch h;
  EXPECT_FALSE(h.largest().seen);
  EXPECT_FALSE(h.minFree().seen);
  char r[160];
  Cli::formatHeapLow(h, r, sizeof(r));
  EXPECT_STREQ(r, "> largest=none min_free=none");
}

TEST(HeapWatch, ANewLowIsPutDownToWhatRanAndAnOlderLowKeepsItsName) {
  HeapWatch h;
  h.observe(mem(30000, 40000), 5);                   // outside every name
  EXPECT_STREQ(h.largest().by, "idle");
  const char* prev = h.enter("request");
  const char* inner = h.enter("sub.start");
  h.observe(mem(25000, 40000), 9);                   // the operation left the block smaller
  h.leave(inner);
  h.observe(mem(25000, 38000), 9);                   // the envelope around it took min_free lower
  h.leave(prev);
  EXPECT_EQ(h.largest().bytes, 25000u);
  EXPECT_STREQ(h.largest().by, "sub.start");
  EXPECT_EQ(h.largest().at_s, 9u);
  EXPECT_EQ(h.minFree().bytes, 38000u);
  EXPECT_STREQ(h.minFree().by, "request");
  prev = h.enter("stream.inventory");
  h.observe(mem(26000, 39000), 20);                  // not lower: the earlier lows and their names stay
  h.leave(prev);
  EXPECT_STREQ(h.largest().by, "sub.start");
  EXPECT_STREQ(h.minFree().by, "request");
  EXPECT_STREQ(h.current(), "idle");
}

TEST(HeapWatch, TheBackgroundNamesWhatRunsOnAnotherTask) {
  HeapWatch h;
  h.setBackground("mqtt.connect");
  h.observe(mem(17000, 21000), 30);                  // the handshake on the worker, seen by the loop's periodic look
  EXPECT_STREQ(h.largest().by, "mqtt.connect");
  const char* prev = h.enter("stream");
  h.observe(mem(16000, 21000), 31);                  // a named piece of work in the loop wins over the background
  h.leave(prev);
  EXPECT_STREQ(h.largest().by, "stream");
  h.setBackground(nullptr);
  h.observe(mem(15000, 20000), 40);
  EXPECT_STREQ(h.largest().by, "idle");
}

TEST(HeapWatch, UnknownOrUntrackedFiguresAreNotLows) {
  HeapWatch h;
  h.observe(mem(30000, 40000, false), 1);            // a platform without a heap API
  EXPECT_FALSE(h.largest().seen);
  h.observe(mem(30000, 0), 2);                       // min_free 0: not tracked there
  EXPECT_TRUE(h.largest().seen);
  EXPECT_FALSE(h.minFree().seen);
}

TEST(HeapWatch, TheReplyNamesBoth) {
  HeapWatch h;
  const char* prev = h.enter("deployment.get");
  h.observe(mem(15348, 19716), 1131);
  h.leave(prev);
  char r[160];
  Cli::formatHeapLow(h, r, sizeof(r));
  EXPECT_STREQ(r, "> largest=15348 by=deployment.get at=1131s min_free=19716 by=deployment.get at=1131s");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
