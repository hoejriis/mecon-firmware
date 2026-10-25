// deployment_provision checks (Deployment authority contract v1, section 5) against the public vectors.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "ed_25519.h"
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

// A set signed by DAK 1 at epoch 1, members as "<id>:<pub>:<admitted>" entries.
static std::string setOf(uint32_t rev, const std::vector<std::string>& members, uint32_t issued = 1) {
  std::string m;
  for (size_t i = 0; i < members.size(); i++) {
    std::string e = members[i];
    size_t a = e.find(':'), b = e.rfind(':');
    m += std::string(i ? "," : "") + "{\"instance_id\":\"" + e.substr(0, a) + "\",\"public_key\":\"" +
         e.substr(a + 1, b - a - 1) + "\",\"admitted_revision\":" + e.substr(b + 1) + "}";
  }
  return std::string("{\"v\":1,\"deployment_id\":\"") + DEPLOYMENT_ID + "\",\"authority_epoch\":1,\"authority_revision\":" +
         std::to_string(rev) + ",\"issued_at\":" + std::to_string(issued) + ",\"members\":[" + m + "],\"revoked\":[]}";
}

static std::vector<uint8_t> signDak1(const std::string& text) {
  uint8_t pub[32], prv[64];
  std::vector<uint8_t> seed(32);
  hexToBytes(SEED_DAK_EPOCH_1, 64, seed.data(), 32);
  ed25519_create_keypair(pub, prv, seed.data());
  std::string m = std::string("MECON-AUTHSET-v1") + '\0' + text;
  std::vector<uint8_t> sig(64);
  ed25519_sign(sig.data(), (const uint8_t*)m.data(), m.size(), pub, prv);
  return sig;
}

static std::string mem(const char* id, const char* pub, int adm) {
  return std::string(id) + ":" + pub + ":" + std::to_string(adm);
}

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
  ASSERT_EQ(1, rec.n_adm);
  EXPECT_EQ(1u, rec.adm[0].admitted_revision);
  EXPECT_FALSE(rec.has_unlock);
}

TEST_F(Fx, AnUnlockSecretComesOnlyWithTheProvisioning) {
  ProvisionPayload p = first();
  p.has_unlock = true;
  memset(p.unlock_secret, 0x42, UNLOCK_LEN);
  ASSERT_EQ(P_OK, run(p));
  EXPECT_TRUE(rec.has_unlock);
  EXPECT_EQ(0x42, rec.unlock_secret[UNLOCK_LEN - 1]);
}

TEST_F(Fx, AFirstProvisioningEpochMustBeAbleToRotate) {
  ProvisionPayload p = first();
  p.authority_epoch = 0;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(p));
  p.authority_epoch = MAX_EPOCH + 1;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(p));
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

// A provisioning carries no rotations (contract section 6.1): a moved key arrives through authority_update.
TEST_F(Fx, ARotationInAProvisioningIsRefused) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(rotatedRev2()));
  ProvisionPayload f = first();          // nor with a first provisioning
  f.n_rotations = 1;
  f.rotations[0] = rotatedRev2().rotations[0];
  cur.state = LS_ABSENT;
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(f));
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
  ASSERT_EQ(2, rec.n_adm);
  EXPECT_EQ(2u, rec.adm[1].admitted_revision);
  // Without the rotation the new set does not verify against the pinned key.
  EXPECT_EQ(P_BAD_AUTHORITY_SIGNATURE, checkAuthorityUpdate(cur, nullptr, 0, set2.data(), set2.size(), sig2, &rec, &set));
  // The set already held: a duplicate.
  uint8_t sig1[SIG_LEN];
  base64ToSig(AUTHORITY_SET_REV1_SIG, 88, sig1);
  EXPECT_EQ(P_DUPLICATE, checkAuthorityUpdate(cur, nullptr, 0, set1.data(), set1.size(), sig1, &rec, &set));
  // A different set at the stored revision: the key signed two sets for one revision. Refused, never acknowledged.
  std::string other = setOf(1, {mem(INSTANCE_IDS_B, PUB_INSTANCE_B, 1)});
  auto so = signDak1(other);
  EXPECT_EQ(P_BAD_AUTHORITY_SET, checkAuthorityUpdate(cur, nullptr, 0, other.data(), other.size(), so.data(), &rec, &set));
  // Nothing pinned: refused.
  Loaded none;
  memset(&none, 0, sizeof(none));
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, checkAuthorityUpdate(none, nullptr, 0, set1.data(), set1.size(), sig1, &rec, &set));
}

// Contract sections 4 and 7: a member new to the device carries the revision of its admission, never an older one.
TEST_F(Fx, AStaleAdmissionIsRefusedOnUpdate) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));   // stored: revision 1, member a admitted at 1
  commit(p);
  Record before = rec;
  std::string t = setOf(2, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 1), mem(INSTANCE_IDS_B, PUB_INSTANCE_B, 1)});
  auto sig = signDak1(t);
  EXPECT_EQ(P_BAD_AUTHORITY_SET, checkAuthorityUpdate(cur, nullptr, 0, t.data(), t.size(), sig.data(), &rec, &set));
  std::string ok = setOf(2, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 1), mem(INSTANCE_IDS_B, PUB_INSTANCE_B, 2)});
  auto sig2 = signDak1(ok);
  EXPECT_EQ(P_OK, checkAuthorityUpdate(cur, nullptr, 0, ok.data(), ok.size(), sig2.data(), &rec, &set));
  (void)before;
}

TEST_F(Fx, AnAdmissionMovedBackIsRefused) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  std::string t2 = setOf(2, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 2)});   // a re-admitted at 2
  auto s2 = signDak1(t2);
  ASSERT_EQ(P_OK, checkAuthorityUpdate(cur, nullptr, 0, t2.data(), t2.size(), s2.data(), &rec, &set));
  stored_text = t2;
  cur.rec = rec;
  cur.set_text = stored_text.data();
  std::string t3 = setOf(3, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 1)});   // and now back to 1
  auto s3 = signDak1(t3);
  EXPECT_EQ(P_BAD_AUTHORITY_SET, checkAuthorityUpdate(cur, nullptr, 0, t3.data(), t3.size(), s3.data(), &rec, &set));
}

TEST_F(Fx, ReAdmissionThroughAnUpdateRaisesTheAdmission) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  std::string t = setOf(2, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 2)});
  auto sig = signDak1(t);
  ASSERT_EQ(P_OK, checkAuthorityUpdate(cur, nullptr, 0, t.data(), t.size(), sig.data(), &rec, &set));
  EXPECT_EQ(2u, rec.adm[0].admitted_revision);   // the marks record resets its sequence (test_dep_record)
}

// A second provisioning naming the stored revision with a different set text: two sets claiming one revision.
TEST_F(Fx, ADifferentSetAtTheStoredRevisionIsRefused) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  std::string other = setOf(1, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 1)}, 999);   // revision 1, other bytes
  auto sig = signDak1(other);
  ProvisionPayload q = first();
  q.provision_revision = 2;
  q.set_text = other.data();
  q.set_len = other.size();
  memcpy(q.set_sig, sig.data(), SIG_LEN);
  EXPECT_EQ(P_BAD_AUTHORITY_SET, run(q));
  ProvisionPayload same = first();   // the identical set at a higher provision_revision is fine
  same.provision_revision = 2;
  EXPECT_EQ(P_OK, run(same));
}

TEST_F(Fx, AStaleAdmissionIsRefusedOnProvisioning) {
  ProvisionPayload p = first();
  ASSERT_EQ(P_OK, run(p));
  commit(p);
  std::string t = setOf(2, {mem(INSTANCE_IDS_A, PUB_INSTANCE_A, 1), mem(INSTANCE_IDS_B, PUB_INSTANCE_B, 1)});
  auto sig = signDak1(t);
  ProvisionPayload q = first();
  q.provision_revision = 2;
  q.set_text = t.data();
  q.set_len = t.size();
  memcpy(q.set_sig, sig.data(), SIG_LEN);
  EXPECT_EQ(P_BAD_AUTHORITY_SET, run(q));
}

TEST_F(Fx, AWeakKeyIsNeverPinned) {
  ProvisionPayload p = first();
  memset(p.dak, 0, KEY_LEN);
  p.dak[0] = 1;   // the identity point
  EXPECT_EQ(P_UNTRUSTED_AUTHORITY_KEY, run(p));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
