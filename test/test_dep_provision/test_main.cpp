// deployment_provision checks (Deployment authority contract v1, section 5) against the public vectors.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/deployment/DepProvision.h"
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

static bool profiles(void*, uint8_t i) { return i < 4; }

struct Fx : ::testing::Test {
  std::string set1 = textOf(AUTHORITY_SET_REV1_SIGNED_HEX), set2 = textOf(AUTHORITY_SET_REV2_SIGNED_HEX);
  std::string rot = textOf(ROTATION_1_TO_2_SIGNED_HEX);
  std::vector<uint8_t> node = hex(PUB_DEVICE_NODE);
  Loaded cur;
  Record rec;
  AuthoritySet set;
  std::string stored_text;   // keeps the stored set text alive for cur.set_text

  void SetUp() override { memset(&cur, 0, sizeof(cur)); cur.state = LS_ABSENT; }

  ProvisionPayload first() {
    ProvisionPayload p;
    memset(&p, 0, sizeof(p));
    p.contract_version = 1;
    hexToBytes(DEPLOYMENT_ID, 32, p.deployment_id, ID_LEN);
    p.name = "Example mesh";
    p.name_len = strlen(p.name);
    memcpy(p.node_public_key, node.data(), KEY_LEN);
    p.provision_revision = 1;
    hexToBytes(PUB_DAK_EPOCH_1, 64, p.dak, KEY_LEN);
    p.authority_epoch = 1;
    p.set_text = set1.data();
    p.set_len = set1.size();
    base64ToSig(AUTHORITY_SET_REV1_SIG, 88, p.set_sig);
    p.sync_key_generation = 4;
    p.n_roles = 1;
    p.roles[0] = { 0, ROLE_PRIMARY };
    return p;
  }

  ProvErr run(const ProvisionPayload& p) {
    ProvisionContext c = { &cur, node.data(), profiles, nullptr };
    return checkProvision(p, c, &rec, &set);
  }

  // Makes rec the stored record, as a successful commit followed by a load would.
  void commit(const ProvisionPayload& p) {
    stored_text.assign(p.set_text, p.set_len);
    cur.state = LS_OK;
    cur.slot = 0;
    cur.generation = 1;
    cur.rec = rec;
    cur.set_text = stored_text.data();
  }

  ProvisionPayload rotatedRev2() {
    ProvisionPayload p = first();
    p.provision_revision = 2;
    hexToBytes(PUB_DAK_EPOCH_2, 64, p.dak, KEY_LEN);
    p.authority_epoch = 2;
    p.set_text = set2.data();
    p.set_len = set2.size();
    base64ToSig(AUTHORITY_SET_REV2_SIG, 88, p.set_sig);
    p.n_rotations = 1;
    p.rotations[0].text = rot.data();
    p.rotations[0].len = rot.size();
    base64ToSig(ROTATION_1_TO_2_SIG, 88, p.rotations[0].sig);
    return p;
  }
};

TEST_F(Fx, FirstProvisioningPinsTheKey) {
  ASSERT_EQ(P_OK, run(first()));
  EXPECT_EQ(0, memcmp(rec.dak, hex(PUB_DAK_EPOCH_1).data(), KEY_LEN));
  EXPECT_EQ(1u, rec.authority_revision);
  EXPECT_EQ(1u, rec.provision_revision);
  EXPECT_STREQ("Example mesh", rec.deployment_name);
  ASSERT_EQ(1, rec.n_replay);
  EXPECT_EQ(0u, rec.replay[0].wm[JC_CMD].mark);
}

TEST_F(Fx, ContractVersionAndDevice) {
  ProvisionPayload p = first();
  p.contract_version = 2;
  EXPECT_EQ(P_UNSUPPORTED_CONTRACT_VERSION, run(p));
  p = first();
  p.node_public_key[0] ^= 1;
  EXPECT_EQ(P_WRONG_DEVICE, run(p));
}

TEST_F(Fx, ResendRules) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  EXPECT_EQ(P_DUPLICATE, run(p));                 // the lost-result re-send
  ProvisionPayload renamed = first();
  renamed.name = "Other";
  renamed.name_len = 5;
  EXPECT_EQ(P_STALE_REVISION, run(renamed));      // same revision, different content
  renamed.provision_revision = 2;
  EXPECT_EQ(P_OK, run(renamed));                  // a rename at a higher revision
  EXPECT_STREQ("Other", rec.deployment_name);
  ProvisionPayload lower = first();
  lower.provision_revision = 0;
  EXPECT_EQ(P_STALE_REVISION, run(lower));
}

TEST_F(Fx, RotationCarriesReplayStateForKeptMembers) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  rec.replay[0].wm[JC_CMD] = { 42, 7 };           // instance a has run commands
  commit(p);
  ASSERT_EQ(P_OK, run(rotatedRev2()));
  EXPECT_EQ(0, memcmp(rec.dak, hex(PUB_DAK_EPOCH_2).data(), KEY_LEN));
  EXPECT_EQ(2u, rec.authority_epoch);
  EXPECT_EQ(2u, rec.authority_revision);
  ASSERT_EQ(2, rec.n_replay);
  EXPECT_EQ(42u, rec.replay[0].wm[JC_CMD].mark);  // a kept
  EXPECT_EQ(0u, rec.replay[1].wm[JC_CMD].mark);   // b new
}

TEST_F(Fx, NewKeyWithoutItsRotationIsUntrusted) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  ProvisionPayload q = rotatedRev2();
  q.n_rotations = 0;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(q));
  q = rotatedRev2();
  q.rotations[0].sig[5] ^= 1;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(q));
}

// The chain stops at the first bad statement, even when the key it would have produced is not the one claimed:
// a broken rotation sent beside an unchanged key is refused, not skipped.
TEST_F(Fx, ABadRotationIsNeverSkipped) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  ProvisionPayload q = first();
  q.provision_revision = 2;
  q.n_rotations = 1;
  q.rotations[0].text = rot.data();
  q.rotations[0].len = rot.size();
  base64ToSig(ROTATION_1_TO_2_SIG, 88, q.rotations[0].sig);
  q.rotations[0].sig[0] ^= 1;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(q));
}

TEST_F(Fx, FirstProvisioningNeedsARevision) {
  ProvisionPayload p = first();
  p.provision_revision = 0;
  EXPECT_EQ(P_STALE_REVISION, run(p));
}

TEST_F(Fx, AnotherDeploymentIsRefused) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  ProvisionPayload q = first();
  q.provision_revision = 2;
  q.deployment_id[0] ^= 1;
  EXPECT_EQ(P_WRONG_DEPLOYMENT, run(q));
}

TEST_F(Fx, ForgedSetSignature) {
  ProvisionPayload p = first();
  p.set_sig[0] ^= 1;
  EXPECT_EQ(P_BAD_AUTHORITY_SIGNATURE, run(p));
  // A set signed by another key than the payload's DAK.
  p = first();
  hexToBytes(PUB_DAK_EPOCH_2, 64, p.dak, KEY_LEN);
  EXPECT_EQ(P_BAD_AUTHORITY_SIGNATURE, run(p));
}

TEST_F(Fx, SyncGenerationNeverGoesBack) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  ProvisionPayload q = first();
  q.provision_revision = 2;
  q.sync_key_generation = 3;
  EXPECT_EQ(P_SYNC_KEY_GENERATION_BEHIND, run(q));
}

TEST_F(Fx, BrokerRoles) {
  ProvisionPayload p = first();
  p.roles[0] = { 9, ROLE_PRIMARY };                // no such profile
  EXPECT_EQ(P_BAD_BROKER_ROLES, run(p));
  p = first();
  p.roles[0].role = 3;
  EXPECT_EQ(P_BAD_BROKER_ROLES, run(p));
  p = first();
  p.n_roles = 2;
  p.roles[1] = { 0, ROLE_LOCAL };                  // the same profile twice
  EXPECT_EQ(P_BAD_BROKER_ROLES, run(p));
}

TEST_F(Fx, DeploymentName) {
  ProvisionPayload p = first();
  p.name_len = 0;
  EXPECT_EQ(P_BAD_DEPLOYMENT_NAME, run(p));
  std::string long_name(65, 'x');
  p = first();
  p.name = long_name.data();
  p.name_len = long_name.size();
  EXPECT_EQ(P_BAD_DEPLOYMENT_NAME, run(p));
  p.name_len = 64;
  EXPECT_EQ(P_OK, run(p));
}

TEST_F(Fx, MigratingIssuerMustBeAMember) {
  ProvisionPayload p = first();
  p.has_migrating_issuer = true;
  hexToBytes(INSTANCE_IDS_B, 32, p.migrating_issuer, ID_LEN);   // b is not in revision 1
  EXPECT_EQ(P_BAD_AUTHORITY_SET, run(p));
  hexToBytes(INSTANCE_IDS_A, 32, p.migrating_issuer, ID_LEN);
  EXPECT_EQ(P_OK, run(p));
}

// authority_update: rotation plus a newer set onto a provisioned device.
TEST_F(Fx, AuthorityUpdateRotatesAndAdmits) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  rec.replay[0].wm[JC_SETTINGS] = { 9, 1 };
  commit(p);
  Rotation r;
  r.text = rot.data();
  r.len = rot.size();
  base64ToSig(ROTATION_1_TO_2_SIG, 88, r.sig);
  uint8_t sig2[SIG_LEN];
  base64ToSig(AUTHORITY_SET_REV2_SIG, 88, sig2);
  ASSERT_EQ(P_OK, checkAuthorityUpdate(cur, &r, 1, set2.data(), set2.size(), sig2, &rec, &set));
  EXPECT_EQ(2u, rec.authority_epoch);
  EXPECT_EQ(2u, rec.authority_revision);
  EXPECT_EQ(1u, rec.provision_revision);          // an update is not a provisioning
  ASSERT_EQ(2, rec.n_replay);
  EXPECT_EQ(9u, rec.replay[0].wm[JC_SETTINGS].mark);
  // Without the rotation the new set does not verify against the pinned key.
  EXPECT_EQ(P_BAD_AUTHORITY_SIGNATURE, checkAuthorityUpdate(cur, nullptr, 0, set2.data(), set2.size(), sig2, &rec, &set));
  // The set already held: a duplicate.
  uint8_t sig1[SIG_LEN];
  base64ToSig(AUTHORITY_SET_REV1_SIG, 88, sig1);
  EXPECT_EQ(P_DUPLICATE, checkAuthorityUpdate(cur, nullptr, 0, set1.data(), set1.size(), sig1, &rec, &set));
  // Nothing pinned: refused.
  Loaded none;
  memset(&none, 0, sizeof(none));
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, checkAuthorityUpdate(none, nullptr, 0, set1.data(), set1.size(), sig1, &rec, &set));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
