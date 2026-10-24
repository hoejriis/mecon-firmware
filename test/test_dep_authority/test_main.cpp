// Authority sets and DAK rotation (Deployment authority contract v1, sections 4 and 9.3), against the public
// vectors and against sets this test signs itself with the vectors' test seeds.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "ed_25519.h"
#include "helpers/deployment/DepAuthority.h"
#include "../test_deployment_vectors/vectors.h"

using namespace Dep;

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> out(strlen(s) / 2);
  EXPECT_TRUE(hexToBytes(s, strlen(s), out.data(), out.size()));
  return out;
}

// The signed text is the signed bytes after "<domain>\0".
static std::string textOf(const char* signed_hex) {
  auto b = hex(signed_hex);
  size_t z = 0;
  while (b[z]) z++;
  return std::string(b.begin() + z + 1, b.end());
}

static std::vector<uint8_t> sigOf(const char* b64) {
  std::vector<uint8_t> s(SIG_LEN);
  EXPECT_TRUE(base64ToSig(b64, strlen(b64), s.data()));
  return s;
}

static std::vector<uint8_t> signWith(const char* seed_hex, const char* domain, const std::string& text) {
  uint8_t pub[32], prv[64];
  ed25519_create_keypair(pub, prv, hex(seed_hex).data());
  std::string m = std::string(domain) + '\0' + text;
  std::vector<uint8_t> sig(64);
  ed25519_sign(sig.data(), (const uint8_t*)m.data(), m.size(), pub, prv);
  return sig;
}

struct Fx : ::testing::Test {
  std::vector<uint8_t> dak1 = hex(PUB_DAK_EPOCH_1), dak2 = hex(PUB_DAK_EPOCH_2), dep = hex(DEPLOYMENT_ID);
  AuthoritySet out;
  SetContext first(const std::vector<uint8_t>& dak, uint32_t epoch) {
    SetContext c = { dak.data(), dep.data(), epoch, false, 0 };
    return c;
  }
  Err accept(const std::string& text, const std::vector<uint8_t>& sig, const SetContext& c) {
    return acceptSet(text.data(), text.size(), sig.data(), c, &out);
  }
  // A set with the given body fields, signed by DAK 1.
  std::string setText(const std::string& members, uint32_t rev = 1, uint32_t epoch = 1,
                      const std::string& revoked = "[]", const char* dep_hex = DEPLOYMENT_ID) {
    return std::string("{\"v\":1,\"deployment_id\":\"") + dep_hex + "\",\"authority_epoch\":" +
           std::to_string(epoch) + ",\"authority_revision\":" + std::to_string(rev) +
           ",\"issued_at\":1,\"members\":" + members + ",\"revoked\":" + revoked + "}";
  }
  std::string member(const char* id, const char* pub, uint32_t admitted) {
    return std::string("{\"instance_id\":\"") + id + "\",\"public_key\":\"" + pub +
           "\",\"admitted_revision\":" + std::to_string(admitted) + "}";
  }
  Err signedSet(const std::string& text, const SetContext& c) {
    return accept(text, signWith(SEED_DAK_EPOCH_1, "MECON-AUTHSET-v1", text), c);
  }
};

TEST_F(Fx, Rev1VectorIsAcceptedOnAFirstProvisioning) {
  ASSERT_EQ(OK, accept(textOf(AUTHORITY_SET_REV1_SIGNED_HEX), sigOf(AUTHORITY_SET_REV1_SIG), first(dak1, 1)));
  EXPECT_EQ(1u, out.authority_revision);
  EXPECT_EQ(1, out.n_members);
  EXPECT_EQ(hex(PUB_INSTANCE_A), std::vector<uint8_t>(out.members[0].public_key, out.members[0].public_key + 32));
}

TEST_F(Fx, RotationThenRev2) {
  RootState st;
  memcpy(st.dak, dak1.data(), 32);
  st.authority_epoch = 1;
  std::string rot = textOf(ROTATION_1_TO_2_SIGNED_HEX);
  ASSERT_EQ(OK, applyRotation(rot.data(), rot.size(), sigOf(ROTATION_1_TO_2_SIG).data(), dep.data(), &st));
  EXPECT_EQ(2u, st.authority_epoch);
  EXPECT_EQ(0, memcmp(st.dak, dak2.data(), 32));
  // The same statement again: from_epoch no longer matches, and the new key did not sign it.
  EXPECT_EQ(UNTRUSTED_AUTHORITY_KEY,
            applyRotation(rot.data(), rot.size(), sigOf(ROTATION_1_TO_2_SIG).data(), dep.data(), &st));

  std::vector<uint8_t> root(st.dak, st.dak + 32);
  SetContext c = { root.data(), dep.data(), 2, true, 1 };
  ASSERT_EQ(OK, accept(textOf(AUTHORITY_SET_REV2_SIGNED_HEX), sigOf(AUTHORITY_SET_REV2_SIG), c));
  EXPECT_EQ(2, out.n_members);
  EXPECT_EQ(2u, out.members[1].admitted_revision);
}

TEST_F(Fx, Rev2WithoutTheRotationIsABadSignature) {
  SetContext c = { dak1.data(), dep.data(), 1, true, 1 };
  EXPECT_EQ(BAD_AUTHORITY_SIGNATURE,
            accept(textOf(AUTHORITY_SET_REV2_SIGNED_HEX), sigOf(AUTHORITY_SET_REV2_SIG), c));
}

TEST_F(Fx, OneChangedByteBreaksTheSignature) {
  std::string t = textOf(AUTHORITY_SET_REV1_SIGNED_HEX);
  t[t.find("\"authority_revision\":1") + 21] = '2';
  EXPECT_EQ(BAD_AUTHORITY_SIGNATURE, accept(t, sigOf(AUTHORITY_SET_REV1_SIG), first(dak1, 1)));
}

TEST_F(Fx, RevisionRules) {
  std::string t = textOf(AUTHORITY_SET_REV1_SIGNED_HEX);
  auto sig = sigOf(AUTHORITY_SET_REV1_SIG);
  SetContext same = { dak1.data(), dep.data(), 1, true, 1 };
  EXPECT_EQ(DUPLICATE, accept(t, sig, same));
  SetContext newer = { dak1.data(), dep.data(), 1, true, 2 };
  EXPECT_EQ(STALE_REVISION, accept(t, sig, newer));
}

TEST_F(Fx, WrongEpochOrDeploymentIsABadSet) {
  std::string t = textOf(AUTHORITY_SET_REV1_SIGNED_HEX);
  auto sig = sigOf(AUTHORITY_SET_REV1_SIG);
  EXPECT_EQ(BAD_AUTHORITY_SET, accept(t, sig, first(dak1, 2)));
  std::vector<uint8_t> other = dep;
  other[0] ^= 1;
  SetContext c = { dak1.data(), other.data(), 1, false, 0 };
  EXPECT_EQ(BAD_AUTHORITY_SET, accept(t, sig, c));
}

TEST_F(Fx, MembershipShapeRules) {
  auto a = [&](uint32_t adm) { return member("0123456789abcdef0123456789abcdef", PUB_INSTANCE_A, adm); };
  auto b = member("fedcba9876543210fedcba9876543210", PUB_INSTANCE_B, 1);
  EXPECT_EQ(OK, signedSet(setText("[" + a(1) + "," + b + "]"), first(dak1, 1)));
  EXPECT_EQ(BAD_AUTHORITY_SET, signedSet(setText("[]"), first(dak1, 1)));                    // no members
  EXPECT_EQ(BAD_AUTHORITY_SET, signedSet(setText("[" + a(1) + "," + a(1) + "]"), first(dak1, 1)));  // duplicate
  EXPECT_EQ(BAD_AUTHORITY_SET, signedSet(setText("[" + a(2) + "]"), first(dak1, 1)));        // admitted ahead
  EXPECT_EQ(BAD_AUTHORITY_SET, signedSet(setText("[" + a(1) + "]", 1, 1,
                                                 "[\"0123456789abcdef0123456789abcdef\"]"),
                                         first(dak1, 1)));                                 // member and revoked
  std::string nine;
  for (int i = 0; i < 9; i++) {
    char id[33];
    snprintf(id, sizeof(id), "%032x", i + 1);
    nine += (i ? "," : "") + member(id, PUB_INSTANCE_A, 1);
  }
  EXPECT_EQ(BAD_AUTHORITY_SET, signedSet(setText("[" + nine + "]"), first(dak1, 1)));        // over 8
}

TEST_F(Fx, AFullSetParses) {
  std::string members, revoked;
  for (int i = 0; i < MAX_MEMBERS; i++) {
    char id[33];
    snprintf(id, sizeof(id), "%032x", i + 1);
    members += (i ? "," : "") + member(id, PUB_INSTANCE_A, 1);
  }
  for (int i = 0; i < MAX_REVOKED; i++) {
    char id[33];
    snprintf(id, sizeof(id), "%032x", 1000 + i);
    revoked += std::string(i ? "," : "") + "\"" + id + "\"";
  }
  ASSERT_EQ(OK, signedSet(setText("[" + members + "]", 1, 1, "[" + revoked + "]"), first(dak1, 1)));
  EXPECT_EQ(MAX_MEMBERS, out.n_members);
  EXPECT_EQ(MAX_REVOKED, out.n_revoked);
  uint8_t id[16];
  ASSERT_TRUE(hexToBytes("000000000000000000000000000003e8", 32, id, 16));
  EXPECT_TRUE(out.isRevoked(id));
}

TEST_F(Fx, Base64IsStrict) {
  uint8_t s[64];
  std::string good = AUTHORITY_SET_REV1_SIG;
  EXPECT_TRUE(base64ToSig(good.data(), good.size(), s));
  EXPECT_FALSE(base64ToSig(good.data(), good.size() - 1, s));
  std::string pad = good;
  pad[87] = 'A';
  EXPECT_FALSE(base64ToSig(pad.data(), pad.size(), s));
  // Non-zero leftover bits: a second spelling of the same signature must not decode.
  std::string alt = good;
  int v = (int)(strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", alt[85]) -
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
  alt[85] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[v ^ 1];
  EXPECT_FALSE(base64ToSig(alt.data(), alt.size(), s));
}

TEST_F(Fx, RotationToAnotherDeploymentIsRefused) {
  std::string rot = "{\"v\":1,\"deployment_id\":\"00000000000000000000000000000000\",\"from_epoch\":1,"
                    "\"to_epoch\":2,\"new_public_key\":\"" + std::string(PUB_DAK_EPOCH_2) + "\",\"issued_at\":1}";
  auto sig = signWith(SEED_DAK_EPOCH_1, "MECON-DAKROT-v1", rot);
  RootState st;
  memcpy(st.dak, dak1.data(), 32);
  st.authority_epoch = 1;
  EXPECT_EQ(WRONG_DEPLOYMENT, applyRotation(rot.data(), rot.size(), sig.data(), dep.data(), &st));
  EXPECT_EQ(1u, st.authority_epoch);
  // An epoch skip, correctly signed.
  std::string skip = "{\"v\":1,\"deployment_id\":\"" + std::string(DEPLOYMENT_ID) +
                     "\",\"from_epoch\":1,\"to_epoch\":3,\"new_public_key\":\"" + PUB_DAK_EPOCH_2 + "\"}";
  auto sig2 = signWith(SEED_DAK_EPOCH_1, "MECON-DAKROT-v1", skip);
  EXPECT_EQ(UNTRUSTED_AUTHORITY_KEY, applyRotation(skip.data(), skip.size(), sig2.data(), dep.data(), &st));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
