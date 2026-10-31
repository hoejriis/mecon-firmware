#include <gtest/gtest.h>
#include <string.h>
#include "helpers/DisplayPrivacy.h"

using namespace DisplayPrivacy;

static const uint8_t NORMAL = Posture::NORMAL, HARDENED = Posture::HARDENED, DAMAGED = 7;

TEST(DisplayPrivacy, MessagesAndNamesShowOnlyInNormal) {
  EXPECT_TRUE(showsMessageText(NORMAL));
  EXPECT_TRUE(showsNames(NORMAL));
  for (uint8_t p : {HARDENED, DAMAGED}) {   // a damaged byte is Hardened
    EXPECT_FALSE(showsMessageText(p)) << (int)p;
    EXPECT_FALSE(showsNames(p)) << (int)p;
  }
}

TEST(DisplayPrivacy, PinShowsOnlyWhileBleAdvertises) {
  const uint32_t chosen = 654321;
  EXPECT_TRUE(showsPairingPin(NORMAL, chosen, true, false));    // advertising, nobody connected
  EXPECT_FALSE(showsPairingPin(NORMAL, chosen, true, true));    // connected: nothing to pair
  EXPECT_FALSE(showsPairingPin(NORMAL, chosen, false, false));  // advertising stopped or BLE off
  EXPECT_FALSE(showsPairingPin(NORMAL, 0, true, false));        // build default: never drawn
}

TEST(DisplayPrivacy, HardenedShowsAChosenPinButNeverTheDefault) {
  EXPECT_TRUE(showsPairingPin(HARDENED, 654321, true, false));
  EXPECT_FALSE(showsPairingPin(HARDENED, 123456, true, false));  // BLE does not start with it in Hardened
  EXPECT_FALSE(showsPairingPin(DAMAGED, 123456, true, false));
  EXPECT_TRUE(showsPairingPin(NORMAL, 123456, true, false));     // Normal: the default PIN is the published one
}

TEST(DisplayPrivacy, BadgeOnlyWhenHardened) {
  EXPECT_EQ(postureBadge(NORMAL), nullptr);
  ASSERT_NE(postureBadge(HARDENED), nullptr);
  EXPECT_STREQ(postureBadge(HARDENED), "[H] ");
  EXPECT_NE(postureBadge(DAMAGED), nullptr);
}

TEST(DisplayPrivacy, WatchFiresFirstAndOnEveryChange) {
  Watch w;
  EXPECT_TRUE(w.changed(NORMAL));     // first draw
  EXPECT_FALSE(w.changed(NORMAL));
  EXPECT_TRUE(w.changed(HARDENED));   // Normal -> Hardened: redraw, e-ink full refresh
  EXPECT_FALSE(w.changed(HARDENED));
  EXPECT_TRUE(w.changed(NORMAL));
}

TEST(DisplayPrivacy, PlaceholdersRevealNothing) {
  EXPECT_GT(strlen(HIDDEN_TEXT), 0u);
  EXPECT_STREQ(HIDDEN_NAME, "-");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
