// The firmware version string (src/helpers/MeconVersion.h): what a tag build, a development build, a dirty tree and a
// build without git each report, and that the short form always fits the Companion device-info frame's 20-byte field.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include "helpers/MeconVersion.h"

namespace {

std::string fullOf(const char* d) {
  char f[96];
  mecon::formatVersion(d, "v1.18.0", f, sizeof(f), NULL, 0);
  return f;
}
std::string shortOf(const char* d) {
  char s[MECON_VERSION_SHORT_MAX + 1];
  mecon::formatVersion(d, "v1.18.0", NULL, 0, s, sizeof(s));
  return s;
}

TEST(MeconVersion, CleanTagBuildIsExactlyTheTagAfterTheUpstreamBase) {
  EXPECT_EQ(fullOf("v0.1.2"), "v1.18.0-mecon.0.1.2");
  EXPECT_EQ(shortOf("v0.1.2"), "v1.18.0-mecon.0.1.2");
}

TEST(MeconVersion, DevelopmentBuildCarriesCommitsAheadAndTheHash) {
  EXPECT_EQ(fullOf("v0.1.2-5-gabc1234"), "v1.18.0-mecon.0.1.2+5.gabc1234");
  EXPECT_EQ(shortOf("v0.1.2-5-gabc1234"), "v1.18.0-mecon.0.1.2");
}

TEST(MeconVersion, DirtyTreeIsMarkedOnATagAndOnADevelopmentBuild) {
  EXPECT_EQ(fullOf("v0.1.2-dirty"), "v1.18.0-mecon.0.1.2+dirty");
  EXPECT_EQ(fullOf("v0.1.2-5-gabc1234-dirty"), "v1.18.0-mecon.0.1.2+5.gabc1234.dirty");
  EXPECT_EQ(shortOf("v0.1.2-5-gabc1234-dirty"), "v1.18.0-mecon.0.1.2");
}

TEST(MeconVersion, NoGitOrNoTagIsUnknownNotAnEmptyOrGarbageString) {
  EXPECT_EQ(fullOf("unknown"), "v1.18.0-mecon.unknown");
  EXPECT_EQ(fullOf(""), "v1.18.0-mecon.unknown");
  EXPECT_EQ(fullOf(NULL), "v1.18.0-mecon.unknown");
  EXPECT_EQ(fullOf("abc1234"), "v1.18.0-mecon.unknown");   // a bare hash is not a version
  EXPECT_EQ(shortOf("unknown"), "v1.18.0-mecon.unk");   // "unknown" would be cut mid-word in the frame field
}

TEST(MeconVersion, PrereleaseTagKeepsItsHyphenAndStillSplitsTheDescribeSuffix) {
  EXPECT_EQ(fullOf("v0.2.0-rc1"), "v1.18.0-mecon.0.2.0-rc1");
  EXPECT_EQ(fullOf("v0.2.0-rc1-3-gdeadbee"), "v1.18.0-mecon.0.2.0-rc1+3.gdeadbee");
}

TEST(MeconVersion, ShortFormNeverExceedsTheFrameField) {
  EXPECT_EQ(strlen("v1.18.0-mecon.0.1.2"), 19u);   // the usual release tag exactly fills the field
  EXPECT_EQ(shortOf("v0.12.34").size(), (size_t)MECON_VERSION_SHORT_MAX);   // cut, still starts with the upstream base
  EXPECT_EQ(shortOf("v0.12.34").compare(0, 14, "v1.18.0-mecon."), 0);
  EXPECT_LE(shortOf("v10.200.3000-rc1-1-gabc").size(), (size_t)MECON_VERSION_SHORT_MAX);
}

TEST(MeconVersion, FullFormIsAlwaysTerminatedInASmallBuffer) {
  char f[12];
  mecon::formatVersion("v0.1.2-5-gabc1234", "v1.18.0", f, sizeof(f), NULL, 0);
  EXPECT_EQ(strlen(f), 11u);
}

TEST(MeconVersion, RunningBuildStartsWithTheUpstreamBase) {
  EXPECT_EQ(strncmp(mecon::versionFull(), "v1.18.0-mecon.", 14), 0);
  EXPECT_EQ(strncmp(mecon::versionShort(), "v1.18.0-mecon.", 14), 0);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
