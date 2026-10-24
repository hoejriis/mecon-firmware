// The Deployment record's two-copy encoding: round trip, frame checks, copy selection, a power cut at every byte
// of a write, and replay state across an authority set change.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/deployment/DepRecord.h"

using namespace Dep;

static const char* SET_TEXT = "{\"v\":1,\"authority_revision\":3}";   // opaque here: the record stores bytes

static Record sample(uint32_t prov = 1) {
  Record r;
  memset(&r, 0, sizeof(r));
  r.contract_version = 1;
  for (int i = 0; i < ID_LEN; i++) r.deployment_id[i] = (uint8_t)(0xA0 + i);
  strcpy(r.deployment_name, "Example mesh");
  for (int i = 0; i < KEY_LEN; i++) r.dak[i] = (uint8_t)i;
  r.authority_epoch = 2;
  r.provision_revision = prov;
  r.sync_key_generation = 4;
  r.has_usb_auth = true;
  memset(r.usb_auth_hash, 0x5A, 32);
  memset(r.set_sig, 0x33, SIG_LEN);
  r.set_len = (uint16_t)strlen(SET_TEXT);
  r.n_replay = 2;
  for (int m = 0; m < 2; m++) {
    memset(r.replay[m].instance_id, 0x10 + m, ID_LEN);
    r.replay[m].wm[JC_CMD] = { 100u + m, 0x00F0F0u };
    r.replay[m].wm[JC_SETTINGS] = { 7u, 1u };
  }
  return r;
}

static std::vector<uint8_t> frame(const Record& r, uint32_t gen) {
  std::vector<uint8_t> b(MAX_FRAME);
  size_t n = encodeRecord(r, SET_TEXT, gen, b.data(), b.size());
  EXPECT_GT(n, 0u);
  b.resize(n);
  return b;
}

static Copy copyOf(const std::vector<uint8_t>& b) { Copy c = { b.data(), b.size() }; return c; }
static const Copy NONE = { nullptr, 0 };

TEST(DepRecord, RoundTrip) {
  Record r = sample();
  auto b = frame(r, 9);
  Record d;
  const char* t;
  uint32_t g;
  ASSERT_TRUE(decodeRecord(b.data(), b.size(), &d, &t, &g));
  EXPECT_EQ(9u, g);
  EXPECT_EQ(std::string(SET_TEXT), std::string(t, d.set_len));
  EXPECT_STREQ("Example mesh", d.deployment_name);
  EXPECT_EQ(0, memcmp(r.deployment_id, d.deployment_id, ID_LEN));
  EXPECT_EQ(0, memcmp(r.dak, d.dak, KEY_LEN));
  EXPECT_EQ(2u, d.authority_epoch);
  EXPECT_EQ(4u, d.sync_key_generation);
  EXPECT_TRUE(d.has_usb_auth);
  EXPECT_EQ(0, memcmp(r.usb_auth_hash, d.usb_auth_hash, 32));
  EXPECT_EQ(0, memcmp(r.set_sig, d.set_sig, SIG_LEN));
  ASSERT_EQ(2, d.n_replay);
  EXPECT_EQ(101u, d.replay[1].wm[JC_CMD].mark);
  EXPECT_EQ(0x00F0F0u, d.replay[1].wm[JC_CMD].mask);
}

TEST(DepRecord, EveryFlippedByteIsRejected) {
  auto b = frame(sample(), 1);
  for (size_t i = 0; i < b.size(); i++) {
    auto x = b;
    x[i] ^= 0x01;
    Record d;
    const char* t;
    uint32_t g;
    EXPECT_FALSE(decodeRecord(x.data(), x.size(), &d, &t, &g)) << "byte " << i;
  }
}

TEST(DepRecord, TruncatedOrTrailingIsRejected) {
  auto b = frame(sample(), 1);
  Record d;
  const char* t;
  uint32_t g;
  for (size_t n = 0; n < b.size(); n++) EXPECT_FALSE(decodeRecord(b.data(), n, &d, &t, &g)) << n;
  auto x = b;
  x.push_back(0);
  EXPECT_FALSE(decodeRecord(x.data(), x.size(), &d, &t, &g));
}

// Recomputes the trailing CRC, so a test can present a structurally wrong frame that the CRC alone would pass.
static void reseal(std::vector<uint8_t>& b) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i + 4 < b.size(); i++) {
    crc ^= b[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  crc = ~crc;
  size_t n = b.size();
  b[n - 4] = (uint8_t)crc; b[n - 3] = (uint8_t)(crc >> 8); b[n - 2] = (uint8_t)(crc >> 16); b[n - 1] = (uint8_t)(crc >> 24);
}

TEST(DepRecord, ValidCrcDoesNotExcuseAWrongStructure) {
  Record d;
  const char* t;
  uint32_t g;
  auto good = frame(sample(), 1);
  // Another magic.
  auto m = good;
  m[3] = '2';
  reseal(m);
  EXPECT_FALSE(decodeRecord(m.data(), m.size(), &d, &t, &g));
  // A length field that disagrees with the frame.
  auto l = good;
  l[8] = (uint8_t)(l[8] - 1);
  reseal(l);
  EXPECT_FALSE(decodeRecord(l.data(), l.size(), &d, &t, &g));
  // Spare bytes inside the payload, after the last field, with the length field covering them.
  auto s = good;
  s.insert(s.end() - 4, 0x00);
  size_t payload = (size_t)s[8] | (size_t)s[9] << 8;
  payload++;
  s[8] = (uint8_t)payload; s[9] = (uint8_t)(payload >> 8);
  reseal(s);
  EXPECT_FALSE(decodeRecord(s.data(), s.size(), &d, &t, &g));
  // The control: the untouched frame, resealed, still decodes.
  auto c = good;
  reseal(c);
  EXPECT_TRUE(decodeRecord(c.data(), c.size(), &d, &t, &g));
}

TEST(DepRecord, EncodeRefusesOutOfRangeFields) {
  uint8_t buf[MAX_FRAME];
  Record r = sample();
  r.deployment_name[0] = 0;
  EXPECT_EQ(0u, encodeRecord(r, SET_TEXT, 1, buf, sizeof(buf)));
  r = sample();
  memset(r.deployment_name, 'x', MAX_NAME + 1);   // 65 bytes, no terminator inside the field
  EXPECT_EQ(0u, encodeRecord(r, SET_TEXT, 1, buf, sizeof(buf)));
  r = sample();
  EXPECT_EQ(0u, encodeRecord(r, SET_TEXT, 1, buf, 20));   // does not fit
}

TEST(DepRecord, SelectionAbsentOkCorrupt) {
  Loaded l;
  loadRecord(NONE, NONE, &l);
  EXPECT_EQ(LS_ABSENT, l.state);

  auto good = frame(sample(), 3);
  auto bad = good;
  bad[12] ^= 0xFF;
  loadRecord(copyOf(bad), copyOf(good), &l);
  ASSERT_EQ(LS_OK, l.state);
  EXPECT_EQ(1, l.slot);

  loadRecord(copyOf(bad), NONE, &l);
  EXPECT_EQ(LS_CORRUPT, l.state);
  loadRecord(copyOf(bad), copyOf(bad), &l);
  EXPECT_EQ(LS_CORRUPT, l.state);
  // An existing empty file is something, and nothing intact: corrupt, not absent.
  std::vector<uint8_t> empty;
  Copy e = { empty.data() ? empty.data() : (const uint8_t*)"", 0 };
  loadRecord(e, NONE, &l);
  EXPECT_EQ(LS_CORRUPT, l.state);
}

TEST(DepRecord, HighestIntactGenerationWins) {
  auto a = frame(sample(1), 4), b = frame(sample(2), 5);
  Loaded l;
  loadRecord(copyOf(a), copyOf(b), &l);
  ASSERT_EQ(LS_OK, l.state);
  EXPECT_EQ(1, l.slot);
  EXPECT_EQ(2u, l.rec.provision_revision);
  loadRecord(copyOf(b), copyOf(a), &l);
  EXPECT_EQ(0, l.slot);
  EXPECT_EQ(5u, l.generation);
}

TEST(DepRecord, NextWriteNeverTouchesTheWinner) {
  Loaded l;
  int slot;
  uint32_t gen;
  loadRecord(NONE, NONE, &l);
  nextWrite(l, &slot, &gen);
  EXPECT_EQ(0, slot);
  EXPECT_EQ(1u, gen);
  auto a = frame(sample(), 1);
  loadRecord(copyOf(a), NONE, &l);
  nextWrite(l, &slot, &gen);
  EXPECT_EQ(1, slot);
  EXPECT_EQ(2u, gen);
}

// A power cut while writing the next copy: whatever prefix reached flash, the previous record still loads.
TEST(DepRecord, PowerCutAtEveryByteKeepsTheOldRecord) {
  auto old_frame = frame(sample(1), 7);
  Loaded l;
  loadRecord(copyOf(old_frame), NONE, &l);
  int slot;
  uint32_t gen;
  nextWrite(l, &slot, &gen);
  ASSERT_EQ(1, slot);
  auto new_frame = frame(sample(2), gen);
  for (size_t n = 0; n <= new_frame.size(); n++) {
    std::vector<uint8_t> partial(new_frame.begin(), new_frame.begin() + n);
    Copy b = { partial.empty() ? (const uint8_t*)"" : partial.data(), partial.size() };
    Loaded r;
    loadRecord(copyOf(old_frame), b, &r);
    ASSERT_EQ(LS_OK, r.state) << n;
    EXPECT_EQ(n == new_frame.size() ? 2u : 1u, r.rec.provision_revision) << n;
  }
}

TEST(DepRecord, ReplayFollowsMembership) {
  Record r = sample();   // members 0x10.. and 0x11..
  AuthoritySet s;
  memset(&s, 0, sizeof(s));
  s.n_members = 2;
  memset(s.members[0].instance_id, 0x11, ID_LEN);   // kept
  memset(s.members[1].instance_id, 0x22, ID_LEN);   // added
  reconcileReplay(&r, s);
  ASSERT_EQ(2, r.n_replay);
  EXPECT_EQ(101u, r.replay[0].wm[JC_CMD].mark);     // 0x11's state carried
  EXPECT_EQ(0u, r.replay[1].wm[JC_CMD].mark);       // a new member starts empty
  EXPECT_EQ(0x22, r.replay[1].instance_id[0]);
  // 0x10 was dropped: nothing of it remains.
  for (int i = 0; i < r.n_replay; i++) EXPECT_NE(0x10, r.replay[i].instance_id[0]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
