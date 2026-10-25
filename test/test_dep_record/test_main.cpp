// The Deployment record's two-copy encoding: round trip, frame checks, copy selection, a power cut at every byte
// of a write, and replay state across an authority set change.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <tuple>
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
  r.authority_revision = 3;
  r.provision_revision = prov;
  r.sync_key_generation = 4;
  r.has_unlock = true;
  memset(r.unlock_secret, 0x5A, UNLOCK_LEN);
  memset(r.set_sig, 0x33, SIG_LEN);
  r.set_len = (uint16_t)strlen(SET_TEXT);
  r.n_roles = 2;
  r.roles[0] = { 0, ROLE_PRIMARY };
  r.roles[1] = { 3, ROLE_LOCAL };
  r.n_adm = 2;
  for (int m = 0; m < 2; m++) {
    memset(r.adm[m].instance_id, 0x10 + m, ID_LEN);
    r.adm[m].admitted_revision = 1;
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
  EXPECT_TRUE(d.has_unlock);
  EXPECT_EQ(0, memcmp(r.unlock_secret, d.unlock_secret, UNLOCK_LEN));
  EXPECT_EQ(0, memcmp(r.set_sig, d.set_sig, SIG_LEN));
  ASSERT_EQ(2, d.n_roles);
  EXPECT_EQ(3, d.roles[1].profile);
  EXPECT_EQ(ROLE_LOCAL, d.roles[1].role);
  ASSERT_EQ(2, d.n_adm);
  EXPECT_EQ(0x11, d.adm[1].instance_id[0]);
  EXPECT_EQ(1u, d.adm[1].admitted_revision);
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

static AuthoritySet setOf(uint32_t revision, std::initializer_list<std::pair<uint8_t, uint32_t>> members) {
  AuthoritySet s;
  memset(&s, 0, sizeof(s));
  s.authority_revision = revision;
  for (auto& m : members) {
    memset(s.members[s.n_members].instance_id, m.first, ID_LEN);
    s.members[s.n_members].admitted_revision = m.second;
    s.n_members++;
  }
  return s;
}

static Marks marksOf(uint32_t revision, std::initializer_list<std::tuple<uint8_t, uint32_t, uint32_t>> members) {
  Marks m;
  memset(&m, 0, sizeof(m));
  m.revision = revision;
  for (auto& x : members) {
    memset(m.m[m.n].instance_id, std::get<0>(x), ID_LEN);
    m.m[m.n].admitted_revision = std::get<1>(x);
    m.m[m.n].wm.mark = std::get<2>(x);
    m.n++;
  }
  return m;
}

// The older copy is deleted first, so a cut between the two deletions never brings the older record back.
TEST(DepRecord, RemovalDeletesTheOlderCopyFirst) {
  int o[2];
  removalOrder(1, o);
  EXPECT_EQ(0, o[0]);
  EXPECT_EQ(1, o[1]);
  removalOrder(0, o);
  EXPECT_EQ(1, o[0]);
  EXPECT_EQ(0, o[1]);
  removalOrder(-1, o);
  EXPECT_EQ(0, o[0]);
  EXPECT_EQ(1, o[1]);
}

TEST(DepRecord, AdmissionsFollowTheSet) {
  Record r = sample();
  setAdmissions(&r, setOf(4, { { 0x11, 1 }, { 0x22, 4 } }));
  ASSERT_EQ(2, r.n_adm);
  EXPECT_EQ(0x22, r.adm[1].instance_id[0]);
  EXPECT_EQ(4u, r.adm[1].admitted_revision);
}

TEST(DepMarks, RoundTripAndDamage) {
  Marks m = marksOf(3, { { 0x10, 1, 100 }, { 0x11, 2, MARK_BLOCKED } });
  std::vector<uint8_t> b(MARKS_FRAME);
  size_t n = encodeMarks(m, 9, b.data(), b.size());
  ASSERT_GT(n, 0u);
  b.resize(n);
  Marks d;
  uint32_t g;
  ASSERT_TRUE(decodeMarks(b.data(), b.size(), &d, &g));
  EXPECT_EQ(9u, g);
  EXPECT_EQ(3u, d.revision);
  ASSERT_EQ(2, d.n);
  EXPECT_EQ(100u, d.m[0].wm.mark);
  EXPECT_EQ(MARK_BLOCKED, d.m[1].wm.mark);
  for (size_t i = 0; i < b.size(); i++) {   // any flipped byte is caught
    std::vector<uint8_t> x = b;
    x[i] ^= 0x01;
    EXPECT_FALSE(decodeMarks(x.data(), x.size(), &d, &g)) << i;
  }
  EXPECT_FALSE(decodeMarks(b.data(), b.size() - 1, &d, &g));
}

TEST(DepMarks, NewestIntactCopyWinsAndDamageIsNotAbsence) {
  Marks a = marksOf(3, { { 0x10, 1, 5 } }), bm = marksOf(3, { { 0x10, 1, 6 } });
  std::vector<uint8_t> fa(MARKS_FRAME), fb(MARKS_FRAME);
  fa.resize(encodeMarks(a, 1, fa.data(), fa.size()));
  fb.resize(encodeMarks(bm, 2, fb.data(), fb.size()));
  LoadedMarks L;
  loadMarks({ fa.data(), fa.size() }, { fb.data(), fb.size() }, &L);
  ASSERT_EQ(LS_OK, L.state);
  EXPECT_EQ(6u, L.marks.m[0].wm.mark);
  int slot;
  uint32_t gen;
  nextMarksWrite(L, &slot, &gen);
  EXPECT_EQ(0, slot);
  EXPECT_EQ(3u, gen);
  fb[12] ^= 1;   // the newer copy damaged: the older one stands
  loadMarks({ fa.data(), fa.size() }, { fb.data(), fb.size() }, &L);
  ASSERT_EQ(LS_OK, L.state);
  EXPECT_EQ(5u, L.marks.m[0].wm.mark);
  fa[12] ^= 1;   // both damaged: corrupt, never absent
  loadMarks({ fa.data(), fa.size() }, { fb.data(), fb.size() }, &L);
  EXPECT_EQ(LS_CORRUPT, L.state);
  loadMarks({ nullptr, 0 }, { nullptr, 0 }, &L);
  EXPECT_EQ(LS_ABSENT, L.state);
}

TEST(DepMarks, ReconcileKeepsDropsAndStartsMembers) {
  Marks stored = marksOf(3, { { 0x10, 1, 100 }, { 0x11, 1, 101 } });
  Marks out;
  EXPECT_EQ(0, reconcileMarks(&stored, setOf(4, { { 0x11, 1 }, { 0x22, 4 } }), &out));
  ASSERT_EQ(2, out.n);
  EXPECT_EQ(4u, out.revision);
  EXPECT_EQ(101u, out.m[0].wm.mark);   // the same admission keeps its mark
  EXPECT_EQ(0u, out.m[1].wm.mark);     // admitted after the stored marks' revision: a new member
  EXPECT_LT(out.indexOf(stored.m[0].instance_id), 0);   // 0x10 removed: dropped
}

// A re-admitted member starts at 0: check 6 refuses everything it signed before.
TEST(DepMarks, ReAdmissionStartsTheSequenceAgain) {
  Marks stored = marksOf(3, { { 0x11, 1, 500 } });
  Marks out;
  reconcileMarks(&stored, setOf(5, { { 0x11, 5 } }), &out);
  EXPECT_EQ(0u, out.m[0].wm.mark);
  EXPECT_EQ(5u, out.m[0].admitted_revision);
}

// The marks lead the record (it fell back to an older copy): never reopen a sequence.
TEST(DepMarks, AFallenBackRecordNeverReopensASequence) {
  Marks stored = marksOf(6, { { 0x11, 5, 40 } });          // written under revision 6, 0x11 re-admitted at 5
  Marks out;
  int blocked = reconcileMarks(&stored, setOf(4, { { 0x11, 1 }, { 0x12, 2 } }), &out);
  EXPECT_EQ(40u, out.m[0].wm.mark);                        // a lower admission in the set does not reset it
  EXPECT_EQ(5u, out.m[0].admitted_revision);
  EXPECT_EQ(MARK_BLOCKED, out.m[1].wm.mark);               // 0x12 is not in marks newer than its admission: lost
  EXPECT_EQ(1, blocked);
}

// Marks lost while the record exists: every member is blocked until a re-admission.
TEST(DepMarks, LostMarksBlockEveryMemberUntilReAdmitted) {
  Marks out;
  EXPECT_EQ(2, reconcileMarks(nullptr, setOf(4, { { 0x11, 1 }, { 0x12, 3 } }), &out));
  EXPECT_EQ(MARK_BLOCKED, out.m[0].wm.mark);
  EXPECT_EQ(MARK_BLOCKED, out.m[1].wm.mark);
  Marks after;
  EXPECT_EQ(1, reconcileMarks(&out, setOf(5, { { 0x11, 5 }, { 0x12, 3 } }), &after));
  EXPECT_EQ(0u, after.m[0].wm.mark);                       // re-admitted: usable again
  EXPECT_EQ(MARK_BLOCKED, after.m[1].wm.mark);             // not re-admitted: still blocked
}

TEST(DepRecord, MembershipRules) {
  Record r = sample();   // stored revision 3; members 0x10, 0x11 admitted at 1
  r.authority_revision = 3;
  AuthoritySet s;
  memset(&s, 0, sizeof(s));
  s.n_members = 2;
  memset(s.members[0].instance_id, 0x10, ID_LEN);
  s.members[0].admitted_revision = 1;
  memset(s.members[1].instance_id, 0x33, ID_LEN);   // new to the device
  s.members[1].admitted_revision = 4;               // admitted after the stored revision: fine
  EXPECT_EQ(OK, checkMembership(r, s));
  s.members[1].admitted_revision = 3;               // claims an admission the device should already have seen
  EXPECT_EQ(BAD_AUTHORITY_SET, checkMembership(r, s));
  s.members[1].admitted_revision = 4;
  s.members[0].admitted_revision = 0;               // an admission moved back
  EXPECT_EQ(BAD_AUTHORITY_SET, checkMembership(r, s));
  s.members[0].admitted_revision = 2;               // moved forward: a re-admission, allowed
  EXPECT_EQ(OK, checkMembership(r, s));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
