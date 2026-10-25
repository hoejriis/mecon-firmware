// Signed interface requests: checks 1 to 6b, the job-id and channel-nonce rules (docs/contract/AUTHORITY.md
// section 7), against the public iface vector and wrappers this test signs with the vectors' seeds.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "ed_25519.h"
#include "helpers/deployment/DepWrapper.h"
#include "../test_deployment_vectors/vectors.h"

using namespace Dep;

static std::vector<uint8_t> hex(const char* s) {
  std::vector<uint8_t> out(strlen(s) / 2);
  EXPECT_TRUE(hexToBytes(s, strlen(s), out.data(), out.size()));
  return out;
}

static std::string b64(const uint8_t* p, size_t n) {
  static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    o += A[v >> 18 & 63];
    o += A[v >> 12 & 63];
    o += i + 1 < n ? A[v >> 6 & 63] : '=';
    o += i + 2 < n ? A[v & 63] : '=';
  }
  return o;
}

static std::string escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

// A wrapper around body signed with seed, naming issuer. A body that does not name an issuer gets the wrapper's
// (check 6b binds the two); a test of a mismatch names one itself.
static std::string wrap(std::string body, const char* seed_hex, const char* issuer, int version = 1) {
  if (body.find("\"issuer\"") == std::string::npos && !body.empty() && body[0] == '{')
    body = std::string("{\"issuer\":\"") + issuer + "\"," + body.substr(1);
  uint8_t pub[32], prv[64], sig[64];
  ed25519_create_keypair(pub, prv, hex(seed_hex).data());
  std::string m = std::string("MECON-DEVMSG-v1") + '\0' + "iface" + '\0' + body;
  ed25519_sign(sig, (const uint8_t*)m.data(), m.size(), pub, prv);
  return "{\"mecon_auth\":" + std::to_string(version) + ",\"issuer\":\"" + issuer + "\",\"sig\":\"" + b64(sig, 64) +
         "\",\"body\":\"" + escape(body) + "\"}";
}

struct Fx : ::testing::Test {
  Record rec;
  AuthoritySet set;
  std::vector<uint8_t> node = hex(PUB_DEVICE_NODE);
  char body[MAX_WRAPPED];
  Verified v;

  void SetUp() override {
    memset(&rec, 0, sizeof(rec));
    hexToBytes(DEPLOYMENT_ID, 32, rec.deployment_id, ID_LEN);
    rec.authority_revision = 2;
    memset(&set, 0, sizeof(set));
    memcpy(set.deployment_id, rec.deployment_id, ID_LEN);
    set.authority_revision = 2;
    set.n_members = 2;
    hexToBytes(INSTANCE_IDS_A, 32, set.members[0].instance_id, ID_LEN);
    hexToBytes(PUB_INSTANCE_A, 64, set.members[0].public_key, KEY_LEN);
    set.members[0].admitted_revision = 1;
    hexToBytes(INSTANCE_IDS_B, 32, set.members[1].instance_id, ID_LEN);
    hexToBytes(PUB_INSTANCE_B, 64, set.members[1].public_key, KEY_LEN);
    set.members[1].admitted_revision = 2;
  }

  const char* nonce = "n0nce-for-this-link";
  WrapErr check(const std::string& wire) {
    return verifyWrapped(wire.data(), wire.size(), rec, set, node.data(), contractOpClass, nonce, body, sizeof(body), &v);
  }
  std::string opBody(const char* op, const char* args, const char* extra) {
    return std::string("{\"v\":1,\"id\":\"r-9\",\"op\":\"") + op + "\",\"args\":" + args + ",\"deployment_id\":\"" +
           DEPLOYMENT_ID + "\",\"authority_revision\":2,\"issued_at\":1,\"target_public_key\":\"" + PUB_DEVICE_NODE +
           "\"" + extra + "}";
  }
  WrapErr checkOp(const char* op, const char* args, const char* extra) {
    return check(wrap(opBody(op, args, extra), SEED_INSTANCE_B, INSTANCE_IDS_B));
  }

  std::string bodyText(uint32_t rev = 2, const char* target = PUB_DEVICE_NODE, const char* job = ",\"job_id\":5") {
    return std::string("{\"v\":1,\"id\":\"r-9\",\"op\":\"config.apply\",\"args\":{\"values\":{\"name\":\"x\"}},") +
           "\"deployment_id\":\"" + DEPLOYMENT_ID + "\",\"authority_revision\":" + std::to_string(rev) +
           ",\"issued_at\":1,\"target_public_key\":\"" + target + "\"" + job + "}";
  }
};

TEST_F(Fx, PublicVectorVerifies) {
  ASSERT_EQ(WR_OK, check(IFACE_REQUEST_WIRE));
  EXPECT_EQ(1, v.member);
  EXPECT_TRUE(v.has_job);
  EXPECT_EQ(1u, v.job_id);
  EXPECT_EQ(2u, v.stamped_revision);
  EXPECT_NE(nullptr, strstr(v.body, "\"op\":\"config.apply\""));
}

TEST_F(Fx, BareAndUnknownVersions) {
  EXPECT_EQ(WR_NOT_WRAPPED, check("{\"v\":1,\"id\":\"r-1\",\"op\":\"hello\"}"));
  EXPECT_EQ(WR_UNSUPPORTED_AUTH_VERSION, check(wrap(bodyText(), SEED_INSTANCE_B, INSTANCE_IDS_B, 2)));
  EXPECT_EQ(WR_MALFORMED, check("not json"));
}

TEST_F(Fx, IssuerMembership) {
  EXPECT_EQ(WR_OK, check(wrap(bodyText(), SEED_INSTANCE_B, INSTANCE_IDS_B)));
  const char* stranger = "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f";
  EXPECT_EQ(WR_UNKNOWN_ISSUER, check(wrap(bodyText(), SEED_INSTANCE_B, stranger)));
  set.n_revoked = 1;
  hexToBytes(stranger, 32, set.revoked[0], ID_LEN);
  EXPECT_EQ(WR_ISSUER_REVOKED, check(wrap(bodyText(), SEED_INSTANCE_B, stranger)));
}

TEST_F(Fx, SignatureBindsIssuerAndBody) {
  // a's key, b's name.
  EXPECT_EQ(WR_BAD_SIGNATURE, check(wrap(bodyText(), SEED_INSTANCE_A, INSTANCE_IDS_B)));
  // A changed body under the original signature.
  std::string w = wrap(bodyText(), SEED_INSTANCE_B, INSTANCE_IDS_B);
  w.replace(w.find("\\\"name\\\":\\\"x\\\""), 14, "\\\"name\\\":\\\"y\\\"");
  EXPECT_EQ(WR_BAD_SIGNATURE, check(w));
}

TEST_F(Fx, DeploymentAndDeviceBinding) {
  std::string w = wrap(bodyText(), SEED_INSTANCE_B, INSTANCE_IDS_B);
  rec.deployment_id[0] ^= 1;
  EXPECT_EQ(WR_WRONG_DEPLOYMENT, check(w));
  rec.deployment_id[0] ^= 1;
  EXPECT_EQ(WR_WRONG_DEVICE, check(wrap(bodyText(2, PUB_INSTANCE_A), SEED_INSTANCE_B, INSTANCE_IDS_B)));
  // An unbound body (no target_public_key) is refused, signed or not.
  std::string unbound = bodyText();
  size_t at = unbound.find(",\"target_public_key\"");
  unbound.erase(at, unbound.find("\"", at + 22) + 1 - at);
  ASSERT_EQ(std::string::npos, unbound.find("target_public_key"));
  EXPECT_EQ(WR_MALFORMED, check(wrap(unbound, SEED_INSTANCE_B, INSTANCE_IDS_B)));
}

TEST_F(Fx, RevisionChecks) {
  rec.authority_revision = 1;
  EXPECT_EQ(WR_AUTHORITY_REVISION_AHEAD, check(wrap(bodyText(2), SEED_INSTANCE_B, INSTANCE_IDS_B)));
  rec.authority_revision = 3;
  // b was admitted at 2: a request it stamped at 1 predates its admission.
  EXPECT_EQ(WR_STALE_AUTHORITY, check(wrap(bodyText(1), SEED_INSTANCE_B, INSTANCE_IDS_B)));
  // a stamping an older revision than the device's is fine while a is a member (a partitioned Backend).
  EXPECT_EQ(WR_OK, check(wrap(bodyText(1), SEED_INSTANCE_A, INSTANCE_IDS_A)));
}

TEST_F(Fx, JobId) {
  // config.apply changes state: without a job id nothing guards it against replay, so it is refused.
  EXPECT_EQ(WR_MALFORMED, check(wrap(bodyText(2, PUB_DEVICE_NODE, ""), SEED_INSTANCE_B, INSTANCE_IDS_B)));
  // A read needs none.
  std::string read = bodyText(2, PUB_DEVICE_NODE, "");
  read.replace(read.find("config.apply"), 12, "status.get");
  ASSERT_EQ(WR_OK, check(wrap(read, SEED_INSTANCE_B, INSTANCE_IDS_B)));
  EXPECT_FALSE(v.has_job);
  EXPECT_STREQ("status.get", v.op);
  EXPECT_EQ(WR_MALFORMED, check(wrap(bodyText(2, PUB_DEVICE_NODE, ",\"job_id\":0"), SEED_INSTANCE_B, INSTANCE_IDS_B)));
}

TEST_F(Fx, TheBodyNamesTheSignedIssuer) {
  std::string b = bodyText();
  std::string named = std::string("{\"issuer\":\"") + INSTANCE_IDS_A + "\"," + b.substr(1);
  EXPECT_EQ(WR_BAD_SIGNATURE, check(wrap(named, SEED_INSTANCE_B, INSTANCE_IDS_B)));   // signed by b, body says a
  std::string bare = b;   // no issuer in the body at all: unbound, refused
  uint8_t pub[32], prv[64], sig[64];
  ed25519_create_keypair(pub, prv, hex(SEED_INSTANCE_B).data());
  std::string m = std::string("MECON-DEVMSG-v1") + '\0' + "iface" + '\0' + bare;
  ed25519_sign(sig, (const uint8_t*)m.data(), m.size(), pub, prv);
  std::string w = std::string("{\"mecon_auth\":1,\"issuer\":\"") + INSTANCE_IDS_B + "\",\"sig\":\"" + b64(sig, 64) +
                  "\",\"body\":\"" + escape(bare) + "\"}";
  EXPECT_EQ(WR_MALFORMED, check(w));
}

// Plain reads are an allowlist; anything else, an unknown operation included, needs a job id (contract section 7.1).
TEST_F(Fx, PlainReadsAreAnAllowlist) {
  EXPECT_EQ(WR_OK, checkOp("hello", "{}", ""));
  EXPECT_EQ(OPC_PLAIN, v.cls);
  EXPECT_EQ(WR_OK, checkOp("config.get", "{\"keys\":[\"name\"]}", ""));
  EXPECT_EQ(WR_OK, checkOp("sub.start", "{\"stream\":\"health\"}", ""));
  EXPECT_EQ(WR_MALFORMED, checkOp("device.reboot", "{}", ""));
  EXPECT_EQ(WR_MALFORMED, checkOp("some.future.op", "{}", ""));
  EXPECT_EQ(WR_OK, checkOp("some.future.op", "{}", ",\"job_id\":7"));
  EXPECT_EQ(OPC_JOB, v.cls);
  EXPECT_EQ(WR_MALFORMED, checkOp("sub.stop", "{\"stream\":\"screen\"}", ""));   // private stream: a job
}

// A sensitive reply goes only to the link the device issued the nonce on.
TEST_F(Fx, SensitiveOperationsNeedThisLinksNonce) {
  const char* job = ",\"job_id\":8";
  EXPECT_EQ(WR_MALFORMED, checkOp("identity.key.export", "{}", ""));                       // no job id
  EXPECT_EQ(WR_BAD_CHANNEL_NONCE, checkOp("identity.key.export", "{}", job));              // no nonce
  EXPECT_EQ(WR_BAD_CHANNEL_NONCE, checkOp("ble.pin.get", "{}", ",\"job_id\":8,\"channel_nonce\":\"other-link\""));
  EXPECT_EQ(WR_OK, checkOp("ble.pin.get", "{}", ",\"job_id\":8,\"channel_nonce\":\"n0nce-for-this-link\""));
  EXPECT_EQ(OPC_SENSITIVE, v.cls);
  EXPECT_EQ(WR_BAD_CHANNEL_NONCE, checkOp("sub.start", "{\"stream\":\"screen\"}", job));
  EXPECT_EQ(WR_BAD_CHANNEL_NONCE, checkOp("sub.start", "{}", job));                        // unreadable stream: strict
  nonce = nullptr;   // a link with no nonce issued: no sensitive operation at all
  EXPECT_EQ(WR_BAD_CHANNEL_NONCE,
            checkOp("ble.pin.get", "{}", ",\"job_id\":8,\"channel_nonce\":\"n0nce-for-this-link\""));
}

TEST_F(Fx, Oversize) {
  std::string big(MAX_WRAPPED + 1, ' ');
  EXPECT_EQ(WR_PAYLOAD_TOO_LARGE, check(big));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
