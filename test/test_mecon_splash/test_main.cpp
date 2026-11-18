#include <gtest/gtest.h>
#include "helpers/ui/MeconSplash.h"

TEST(MeconSplash, AReleaseVersionSplitsIntoBoth) {
  char url[22], m[22], c[22];
  MeconSplash::lines("v1.18.0-mecon.0.3.7", url, m, c);
  EXPECT_STREQ(url, "https://mecon.cloud/");
  EXPECT_STREQ(m, "MECON 0.3.7");
  EXPECT_STREQ(c, "MeshCore 1.18.0");
}

TEST(MeconSplash, ADevelopmentTagIsShownAsBuiltAndCutToTheLine) {
  char url[22], m[22], c[22];
  MeconSplash::lines("v1.18.0-mecon.0.3.7-12-gdeadbeef", url, m, c);
  EXPECT_STREQ(m, "MECON 0.3.7-12-gdeadb");   // 21 columns
  EXPECT_STREQ(c, "MeshCore 1.18.0");
}

TEST(MeconSplash, AVersionWithoutTheMeconTagNamesOnlyMeshCore) {
  char url[22], m[22], c[22];
  MeconSplash::lines("v1.18.0", url, m, c);
  EXPECT_STREQ(m, "MECON ");
  EXPECT_STREQ(c, "MeshCore 1.18.0");
}

TEST(MeconSplash, TheMarkIsThirtyTwoSquareAndNotBlank) {
  int lit = 0;
  for (uint8_t b : MeconSplash::mark_bits) lit += __builtin_popcount(b);
  EXPECT_EQ(sizeof(MeconSplash::mark_bits), 128u);
  EXPECT_GT(lit, 150);
  EXPECT_LT(lit, 600);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
