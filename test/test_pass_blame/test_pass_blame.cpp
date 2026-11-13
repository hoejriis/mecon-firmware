// Which section of the longest loop pass took its time (src/helpers/PassBlame.h): the longest pass since boot is kept
// with its longest section, time outside every section is "unscoped", and the reply of `get mecon.slowpass`.
#include <gtest/gtest.h>
#include "helpers/PassBlame.h"
#include "helpers/cli/MeconCli.h"

TEST(PassBlame, NothingSeenUntilTheFirstPassEnds) {
  PassBlame p;
  EXPECT_FALSE(p.slowest().seen);
  char r[160];
  Cli::formatSlowPass(p, r, sizeof(r));
  EXPECT_STREQ(r, "> none");
}

TEST(PassBlame, TheLongestPassIsKeptWithItsLongestSection) {
  PassBlame p;
  p.begin(1000);
  p.section("mesh", 40);
  p.section("screen", 120);
  p.section("mqtt", 15);
  p.end(1200, 1);
  EXPECT_EQ(p.slowest().pass_ms, 200u);
  EXPECT_STREQ(p.slowest().by, "screen");
  EXPECT_EQ(p.slowest().section_ms, 120u);
  EXPECT_EQ(p.slowest().at_s, 1u);
  char r[160];
  Cli::formatSlowPass(p, r, sizeof(r));
  EXPECT_STREQ(r, "> pass=200ms by=screen section=120ms at=1s");
}

TEST(PassBlame, AShorterPassLaterDoesNotReplaceIt) {
  PassBlame p;
  p.begin(0); p.section("mqtt", 900); p.end(950, 3);
  p.begin(2000); p.section("mesh", 300); p.end(2400, 5);   // longer section than nothing, but a shorter pass
  EXPECT_EQ(p.slowest().pass_ms, 950u);
  EXPECT_STREQ(p.slowest().by, "mqtt");
  p.begin(3000); p.section("mesh", 1000); p.end(4100, 9);   // a longer pass replaces it
  EXPECT_EQ(p.slowest().pass_ms, 1100u);
  EXPECT_STREQ(p.slowest().by, "mesh");
  EXPECT_EQ(p.slowest().at_s, 9u);
}

TEST(PassBlame, TimeOutsideEverySectionIsUnscoped) {
  PassBlame p;
  p.begin(100);
  p.end(400, 2);
  EXPECT_STREQ(p.slowest().by, "unscoped");
  EXPECT_EQ(p.slowest().section_ms, 300u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
