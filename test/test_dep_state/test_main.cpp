// Loading the Deployment state from its two copies, and the status.deployment object.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/deployment/DepMarker.h"
#include "helpers/deployment/DepState.h"
#include "../test_deployment_vectors/vectors.h"

using namespace Dep;

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> out(strlen(s) / 2);
  EXPECT_TRUE(hexToBytes(s, strlen(s), out.data(), out.size()));
  return out;
}

static std::string textOf(const char* signed_hex) {
  auto b = hex(signed_hex);
  size_t z = 0;
  while (b[z]) z++;
  return std::string(b.begin() + z + 1, b.end());
}

struct Fx : ::testing::Test {
  std::string set1 = textOf(AUTHORITY_SET_REV1_SIGNED_HEX);

  Record rec(uint32_t prov = 1) {
    Record r;
    memset(&r, 0, sizeof(r));
    r.contract_version = 1;
    hexToBytes(DEPLOYMENT_ID, 32, r.deployment_id, ID_LEN);
    strcpy(r.deployment_name, "Example mesh");
    hexToBytes(PUB_DAK_EPOCH_1, 64, r.dak, KEY_LEN);
    r.authority_epoch = 1;
    r.authority_revision = 1;
    r.provision_revision = prov;
    r.sync_key_generation = 4;
    base64ToSig(AUTHORITY_SET_REV1_SIG, 88, r.set_sig);
    r.set_len = (uint16_t)set1.size();
    r.n_adm = 1;
    hexToBytes(INSTANCE_IDS_A, 32, r.adm[0].instance_id, ID_LEN);
    r.adm[0].admitted_revision = 1;
    return r;
  }
  std::vector<uint8_t> frame(const Record& r, uint32_t gen) {
    std::vector<uint8_t> b(MAX_FRAME);
    size_t n = encodeRecord(r, set1.data(), gen, b.data(), b.size());
    EXPECT_GT(n, 0u);
    b.resize(n);
    return b;
  }
  static Copy of(const std::vector<uint8_t>& b) { Copy c = { b.data(), b.size() }; return c; }
  std::string status(const DepState* s, bool hardened = false) {
    char buf[512];
    Iface::JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    depWriteStatus(w, s ? s->state : LS_ABSENT, s, hardened);
    w.endObject();
    EXPECT_TRUE(w.ok());
    return std::string(buf, w.size());
  }
};

TEST_F(Fx, AbsentIsNoStateAndNoStatusObject) {
  Copy none = { nullptr, 0 };
  DepState* s = (DepState*)1;
  EXPECT_EQ(LS_ABSENT, depLoad(none, none, &s));
  EXPECT_EQ(nullptr, s);
  EXPECT_EQ("{}", status(nullptr));
}

TEST_F(Fx, IntactRecordLoadsWithItsSet) {
  auto a = frame(rec(1), 1), b = frame(rec(2), 2);
  DepState* s = nullptr;
  depLoad(of(a), of(b), &s);
  ASSERT_NE(nullptr, s);
  ASSERT_EQ(LS_OK, s->state);
  EXPECT_EQ(1, s->slot);
  EXPECT_EQ(2u, s->rec.provision_revision);
  EXPECT_EQ(1, s->set.n_members);
  std::string j = status(s, true);
  EXPECT_NE(std::string::npos, j.find("\"deployment_id\":\"" + std::string(DEPLOYMENT_ID) + "\""));
  EXPECT_NE(std::string::npos, j.find("\"authority_revision\":1"));
  EXPECT_NE(std::string::npos, j.find("\"provision_revision\":2"));
  EXPECT_NE(std::string::npos, j.find("\"posture\":\"hardened\""));
  EXPECT_EQ(std::string::npos, j.find("\"state\""));
  depFree(s);
}

static std::vector<uint8_t> marksFrame(uint32_t revision, uint32_t admitted, uint32_t mark, uint32_t gen) {
  Marks m;
  memset(&m, 0, sizeof(m));
  m.revision = revision;
  m.n = 1;
  hexToBytes(INSTANCE_IDS_A, 32, m.m[0].instance_id, ID_LEN);
  m.m[0].admitted_revision = admitted;
  m.m[0].wm.mark = mark;
  std::vector<uint8_t> b(MARKS_FRAME);
  b.resize(encodeMarks(m, gen, b.data(), b.size()));
  return b;
}

TEST_F(Fx, MarksLoadAndReconcileWithTheRecord) {
  auto a = frame(rec(1), 1);
  DepState* s = nullptr;
  ASSERT_EQ(LS_OK, depLoad(of(a), Copy{ nullptr, 0 }, &s));
  auto m = marksFrame(1, 1, 33, 4);
  depLoadMarks(s, of(m), Copy{ nullptr, 0 });
  EXPECT_FALSE(s->marks_lost);
  EXPECT_FALSE(s->marks_dirty);
  EXPECT_EQ(0, s->blocked);
  EXPECT_EQ(33u, s->marks.m[0].wm.mark);
  EXPECT_EQ(0, s->marks_slot);
  EXPECT_EQ(4u, s->marks_generation);
  depFree(s);
}

// Marks lost while the record exists: fail closed, every member blocked, and the blocked state written back.
TEST_F(Fx, LostMarksBlockEveryMember) {
  auto a = frame(rec(1), 1);
  DepState* s = nullptr;
  ASSERT_EQ(LS_OK, depLoad(of(a), Copy{ nullptr, 0 }, &s));
  depLoadMarks(s, Copy{ nullptr, 0 }, Copy{ nullptr, 0 });
  EXPECT_TRUE(s->marks_lost);
  EXPECT_TRUE(s->marks_dirty);
  EXPECT_EQ(1, s->blocked);
  EXPECT_EQ(MARK_BLOCKED, s->marks.m[0].wm.mark);
  EXPECT_NE(std::string::npos, status(s).find("\"blocked_members\":1"));
  std::vector<uint8_t> junk(20, 0x55);   // corrupt is lost too, never a fresh start
  depLoadMarks(s, of(junk), Copy{ nullptr, 0 });
  EXPECT_TRUE(s->marks_lost);
  EXPECT_EQ(MARK_BLOCKED, s->marks.m[0].wm.mark);
  depFree(s);
}

TEST_F(Fx, AStoredSetThatNoLongerVerifiesIsCorrupt) {
  Record r = rec();
  r.set_sig[3] ^= 1;   // a frame with a valid CRC around a set the stored key did not sign
  auto a = frame(r, 1);
  DepState* s = nullptr;
  depLoad(of(a), Copy{ nullptr, 0 }, &s);
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  std::string j = status(s);
  EXPECT_NE(std::string::npos, j.find("\"state\":\"corrupt\""));
  EXPECT_EQ(std::string::npos, j.find("deployment_id"));   // nothing unreadable is reported
  depFree(s);
  // The other copy intact: it wins.
  auto good = frame(rec(), 1);
  depLoad(of(a), of(good), &s);
  ASSERT_EQ(LS_OK, s->state);
  EXPECT_EQ(1, s->slot);
  depFree(s);
}

// A newer copy whose stored set does not verify must lose to the older intact one, even when it is checked right
// after a good copy (whose parsed set must not stand in for its own).
TEST_F(Fx, NewerCopyWithABadSetLosesToTheOlderIntactOne) {
  Record bad = rec(2);
  bad.set_sig[0] ^= 1;
  auto a = frame(rec(1), 1), b = frame(bad, 2);
  DepState* s = nullptr;
  depLoad(of(a), of(b), &s);
  ASSERT_NE(nullptr, s);
  ASSERT_EQ(LS_OK, s->state);
  EXPECT_EQ(0, s->slot);
  EXPECT_EQ(1u, s->rec.provision_revision);
  depFree(s);
}

TEST_F(Fx, RevisionMismatchBetweenRecordAndSetIsCorrupt) {
  Record r = rec();
  r.authority_revision = 5;
  auto a = frame(r, 1);
  DepState* s = nullptr;
  depLoad(of(a), Copy{ nullptr, 0 }, &s);
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  depFree(s);
}

TEST_F(Fx, GarbageIsCorruptNotAbsent) {
  std::vector<uint8_t> junk(40, 0xAB);
  DepState* s = nullptr;
  depLoad(of(junk), Copy{ nullptr, 0 }, &s);
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  depFree(s);
}

// The newer copy damaged, the older intact: the older one loads, and the fallback is flagged (its replay state may
// be behind what the device had accepted).
TEST_F(Fx, FallingBackToTheOlderCopyIsFlagged) {
  auto a = frame(rec(1), 1);
  std::vector<uint8_t> junk(40, 0xAB);
  DepState* s = nullptr;
  ASSERT_EQ(LS_OK, depLoad(of(a), of(junk), &s));
  EXPECT_TRUE(s->other_damaged);
  EXPECT_NE(std::string::npos, status(s).find("\"fallback\":true"));   // reported, so a manager raises its sequence
  depFree(s);
  auto b = frame(rec(2), 2);
  ASSERT_EQ(LS_OK, depLoad(of(a), of(b), &s));
  EXPECT_FALSE(s->other_damaged);
  depFree(s);
  ASSERT_EQ(LS_OK, depLoad(of(a), Copy{ nullptr, 0 }, &s));
  EXPECT_FALSE(s->other_damaged);   // an absent copy is not a damaged one
  depFree(s);
}

TEST(ProvisionedMarker, NoCopyAndTheMarkerPresentIsCorruptNeverAbsent) {
  EXPECT_EQ(LS_CORRUPT, withMarker(LS_ABSENT, MK_PRESENT)) << "both copies gone: first use must stay closed";
  EXPECT_EQ(LS_ABSENT, withMarker(LS_ABSENT, MK_ABSENT)) << "a device that was never provisioned";
  EXPECT_EQ(LS_ABSENT, withMarker(LS_ABSENT, MK_UNSUPPORTED)) << "nothing to check (the unsigned first provisioning is refused instead)";
  for (MarkerState m : {MK_ABSENT, MK_PRESENT, MK_UNSUPPORTED}) {
    EXPECT_EQ(LS_OK, withMarker(LS_OK, m));
    EXPECT_EQ(LS_CORRUPT, withMarker(LS_CORRUPT, m));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
