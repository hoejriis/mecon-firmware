// The gate that decides which channel messages notify the Companion's screen and buzzer, and how the favourite mark
// is kept in the channel file's reserved record bytes (src/helpers/ChannelFavourite.h, docs/UI.md).
#include <gtest/gtest.h>
#include <string.h>
#include <vector>
#include <helpers/ChannelDetails.h>
#include <helpers/ChannelFavourite.h>

namespace {

using Chan = ChannelDetails;   // the real record: its default decides the state of every new slot

const int CAP = 40;

// A table with BaseChatMesh's getChannel: false for a slot outside it, else a copy.
struct FakeMesh {
  Chan t[CAP];
  int cap = CAP;
  bool getChannel(int idx, Chan& dest) { if (idx < 0 || idx >= cap) return false; dest = t[idx]; return true; }
};

// The channels file as DataStore writes and reads it: per record 4 reserved bytes, then the name; the secret is left out.
std::vector<uint8_t> save(const Chan* t, int n) {
  std::vector<uint8_t> f;
  for (int i = 0; i < n; i++) {
    uint8_t res[4];
    ChannelFav::encode(t[i].favourite != 0, res);
    f.insert(f.end(), res, res + 4);
    f.insert(f.end(), t[i].name, t[i].name + 32);
  }
  return f;
}
void load(const std::vector<uint8_t>& f, Chan* t, int n) {
  for (int i = 0; i < n && (size_t)(i + 1) * 36 <= f.size(); i++) {
    t[i].favourite = ChannelFav::decode(&f[i * 36]);
    memcpy(t[i].name, &f[i * 36 + 4], 32);
  }
}

}  // namespace

TEST(ChannelFavourite, EveryNewSlotIsOff) {
  FakeMesh m;
  for (int i = 0; i < CAP; i++) EXPECT_FALSE(ChannelFav::notifies<Chan>(m, i)) << i;
}

TEST(ChannelFavourite, OnlyAMarkedSlotNotifies) {
  FakeMesh m;
  m.t[0].favourite = 1;
  m.t[7].favourite = 1;
  EXPECT_TRUE(ChannelFav::notifies<Chan>(m, 0));
  EXPECT_TRUE(ChannelFav::notifies<Chan>(m, 7));
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, 1));
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, 39));
}

TEST(ChannelFavourite, ASlotOutsideTheTableNeverNotifies) {
  FakeMesh m;
  for (int i = 0; i < CAP; i++) m.t[i].favourite = 1;
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, -1));
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, CAP));
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, 127));   // no stored channel decrypted the message
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, (uint8_t)-1));   // findChannelIdx's "not found" as the unsigned slot
  m.cap = 0;
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, 0));     // a build without a channel table
}

TEST(ChannelFavourite, AClearedSlotNoLongerNotifies) {
  FakeMesh m;
  m.t[3].favourite = 1;
  ASSERT_TRUE(ChannelFav::notifies<Chan>(m, 3));
  m.t[3] = Chan();   // a removed or replaced slot is a fresh record
  EXPECT_FALSE(ChannelFav::notifies<Chan>(m, 3));
}

TEST(ChannelFavourite, TheMarkSurvivesASaveAndLoad) {
  Chan a[CAP];
  for (int i = 0; i < CAP; i++) snprintf(a[i].name, 32, "ch%d", i);
  a[0].favourite = 1;
  a[5].favourite = 1;
  a[39].favourite = 1;
  Chan b[CAP];
  load(save(a, CAP), b, CAP);
  for (int i = 0; i < CAP; i++) EXPECT_EQ(a[i].favourite, b[i].favourite) << i;
  EXPECT_STREQ("ch5", b[5].name);
}

TEST(ChannelFavourite, AnOldFileWithZeroedReservedBytesLoadsAsAllOff) {
  std::vector<uint8_t> old(CAP * 36, 0);   // as an earlier firmware wrote it: reserved bytes zero
  for (int i = 0; i < CAP; i++) old[i * 36 + 4] = 'a' + (i % 26);
  Chan t[CAP];
  for (int i = 0; i < CAP; i++) t[i].favourite = 1;   // stale RAM must not leak through
  load(old, t, CAP);
  for (int i = 0; i < CAP; i++) EXPECT_FALSE(t[i].favourite) << i;
}

TEST(ChannelFavourite, OnlyTheMarkValueTurnsItOn) {
  uint8_t r[4] = {0, 0, 0, 0};
  EXPECT_EQ(0, ChannelFav::decode(r));
  r[0] = ChannelFav::MARK;
  EXPECT_EQ(1, ChannelFav::decode(r));
  r[0] = 0xFF;   // not a value any firmware wrote
  EXPECT_EQ(0, ChannelFav::decode(r));
  uint8_t r2[4] = {9, 9, 9, 9};
  ChannelFav::encode(false, r2);
  EXPECT_EQ(0, r2[0] | r2[1] | r2[2] | r2[3]);
  ChannelFav::encode(true, r2);
  EXPECT_EQ(ChannelFav::MARK, r2[0]);
  EXPECT_EQ(0, r2[1] | r2[2] | r2[3]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
