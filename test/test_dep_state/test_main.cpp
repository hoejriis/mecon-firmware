// Loading the Deployment state from its two copies, and the status.deployment object.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
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
    r.n_replay = 1;
    hexToBytes(INSTANCE_IDS_A, 32, r.replay[0].instance_id, ID_LEN);
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
    depWriteStatus(w, s, hardened);
    w.endObject();
    EXPECT_TRUE(w.ok());
    return std::string(buf, w.size());
  }
};

TEST_F(Fx, AbsentIsNoStateAndNoStatusObject) {
  Copy none = { nullptr, 0 };
  EXPECT_EQ(nullptr, depLoad(none, none));
  EXPECT_EQ("{}", status(nullptr));
}

TEST_F(Fx, IntactRecordLoadsWithItsSet) {
  auto a = frame(rec(1), 1), b = frame(rec(2), 2);
  DepState* s = depLoad(of(a), of(b));
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

TEST_F(Fx, AStoredSetThatNoLongerVerifiesIsCorrupt) {
  Record r = rec();
  r.set_sig[3] ^= 1;   // a frame with a valid CRC around a set the stored key did not sign
  auto a = frame(r, 1);
  DepState* s = depLoad(of(a), Copy{ nullptr, 0 });
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  std::string j = status(s);
  EXPECT_NE(std::string::npos, j.find("\"state\":\"corrupt\""));
  EXPECT_EQ(std::string::npos, j.find("deployment_id"));   // nothing unreadable is reported
  depFree(s);
  // The other copy intact: it wins.
  auto good = frame(rec(), 1);
  s = depLoad(of(a), of(good));
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
  DepState* s = depLoad(of(a), of(b));
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
  DepState* s = depLoad(of(a), Copy{ nullptr, 0 });
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  depFree(s);
}

TEST_F(Fx, GarbageIsCorruptNotAbsent) {
  std::vector<uint8_t> junk(40, 0xAB);
  DepState* s = depLoad(of(junk), Copy{ nullptr, 0 });
  ASSERT_NE(nullptr, s);
  EXPECT_EQ(LS_CORRUPT, s->state);
  depFree(s);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
