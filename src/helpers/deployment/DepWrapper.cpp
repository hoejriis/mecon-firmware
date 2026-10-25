#include "DepWrapper.h"
#include <new>
#include <stdlib.h>
#include <string.h>
#include "ed_25519.h"
#include "helpers/iface/IfaceJson.h"

namespace Dep {

using namespace Iface;

const char* wrapErrCode(WrapErr e) {
  switch (e) {
    case WR_OK: return "ok";
    case WR_NOT_WRAPPED: return "unauthenticated";
    case WR_UNSUPPORTED_AUTH_VERSION: return "unsupported_auth_version";
    case WR_UNKNOWN_ISSUER: return "unknown_issuer";
    case WR_ISSUER_REVOKED: return "issuer_revoked";
    case WR_BAD_SIGNATURE: return "bad_signature";
    case WR_WRONG_DEPLOYMENT: return "wrong_deployment";
    case WR_WRONG_DEVICE: return "wrong_device";
    case WR_AUTHORITY_REVISION_AHEAD: return "authority_revision_ahead";
    case WR_STALE_AUTHORITY: return "stale_authority";
    case WR_PAYLOAD_TOO_LARGE: return "payload_too_large";
    case WR_MALFORMED: return "unauthenticated";
  }
  return "failed";
}

static const char DOMAIN[] = "MECON-DEVMSG-v1";
static const char SUFFIX[] = "iface";

static bool verifyBody(const char* body, size_t len, const uint8_t sig[SIG_LEN], const uint8_t key[KEY_LEN]) {
  size_t dl = sizeof(DOMAIN) - 1, sl = sizeof(SUFFIX) - 1;
  size_t n = dl + 1 + sl + 1 + len;
  uint8_t* msg = static_cast<uint8_t*>(malloc(n));
  if (!msg) return false;
  memcpy(msg, DOMAIN, dl);
  msg[dl] = 0;
  memcpy(msg + dl + 1, SUFFIX, sl);
  msg[dl + 1 + sl] = 0;
  memcpy(msg + dl + sl + 2, body, len);
  bool ok = ed25519_verify(sig, msg, n, key) == 1;
  free(msg);
  return ok;
}

static bool hexField(const JsonDoc& d, const char* key, uint8_t* out, size_t n) {
  char buf[2 * KEY_LEN + 1];
  int t = jsonFind(d, 0, key);
  return t >= 0 && 2 * n + 1 <= sizeof(buf) && jsonGetString(d, t, buf, 2 * n + 1, CS_ID, 2 * n) &&
         hexToBytes(buf, 2 * n, out, n);
}

static bool u32Field(const JsonDoc& d, const char* key, uint32_t* out) {
  int64_t v;
  int t = jsonFind(d, 0, key);
  if (t < 0 || !jsonGetInt(d, t, 0, 0xFFFFFFFFLL, &v)) return false;
  *out = (uint32_t)v;
  return true;
}

WrapErr verifyWrapped(const char* wire, size_t len, const Record& rec, const AuthoritySet& set,
                      const uint8_t own_node_key[KEY_LEN], MutatingFn mutating, char* body_buf, size_t cap,
                      Verified* out) {
  if (len > MAX_WRAPPED) return WR_PAYLOAD_TOO_LARGE;
  RequestDoc* d = new (std::nothrow) RequestDoc();
  if (!d) return WR_MALFORMED;
  WrapErr e = WR_MALFORMED;
  uint8_t issuer[ID_LEN], sig[SIG_LEN];
  char sig_b64[89];
  int t_auth, t_body, member;
  int64_t version;

  if (jsonParse(*d, wire, len) != J_OK) goto done;
  // 1. A wrapper, of a version this device speaks.
  t_auth = jsonFind(*d, 0, "mecon_auth");
  if (t_auth < 0) { e = WR_NOT_WRAPPED; goto done; }
  if (!jsonGetInt(*d, t_auth, 0, 0xFFFFFFFFLL, &version)) goto done;
  if (version != 1) { e = WR_UNSUPPORTED_AUTH_VERSION; goto done; }
  if (!hexField(*d, "issuer", issuer, ID_LEN)) goto done;
  {
    int ts = jsonFind(*d, 0, "sig");
    if (ts < 0 || !jsonGetString(*d, ts, sig_b64, sizeof(sig_b64), CS_ANY, 88) || !base64ToSig(sig_b64, 88, sig))
      goto done;
  }
  t_body = jsonFind(*d, 0, "body");
  if (t_body < 0 || !jsonGetString(*d, t_body, body_buf, cap, CS_ANY, 2)) goto done;
  // 2. The issuer is a current member.
  member = set.memberIndex(issuer);
  if (member < 0) { e = set.isRevoked(issuer) ? WR_ISSUER_REVOKED : WR_UNKNOWN_ISSUER; goto done; }
  // 3. Its signature over the body exactly as received.
  out->body = body_buf;
  out->body_len = strlen(body_buf);
  if (!verifyBody(out->body, out->body_len, sig, set.members[member].public_key)) { e = WR_BAD_SIGNATURE; goto done; }

  // The body: parsed only now that it is known to be signed.
  if (jsonParse(*d, out->body, out->body_len) != J_OK) goto done;
  {
    uint8_t dep[ID_LEN], target[KEY_LEN];
    // 4. This Deployment. 4b. This device.
    if (!hexField(*d, "deployment_id", dep, ID_LEN)) goto done;
    if (memcmp(dep, rec.deployment_id, ID_LEN) != 0) { e = WR_WRONG_DEPLOYMENT; goto done; }
    if (!hexField(*d, "target_public_key", target, KEY_LEN)) goto done;   // strict: an unbound body is refused
    if (memcmp(target, own_node_key, KEY_LEN) != 0) { e = WR_WRONG_DEVICE; goto done; }
    // 5. Not a set this device has not seen. 6. Not from before the issuer's (re)admission.
    if (!u32Field(*d, "authority_revision", &out->stamped_revision)) goto done;
    if (out->stamped_revision > rec.authority_revision) { e = WR_AUTHORITY_REVISION_AHEAD; goto done; }
    if (out->stamped_revision < set.members[member].admitted_revision) { e = WR_STALE_AUTHORITY; goto done; }
    out->has_job = jsonFind(*d, 0, "job_id") >= 0;
    if (out->has_job && (!u32Field(*d, "job_id", &out->job_id) || out->job_id == 0)) goto done;
    // The operation, and a job id whenever it changes state: without one nothing guards it against replay.
    int to = jsonFind(*d, 0, "op");
    if (to < 0 || !jsonGetString(*d, to, out->op, sizeof(out->op), CS_NAME, 1)) goto done;
    if (!out->has_job && (!mutating || mutating(out->op))) goto done;
  }
  out->member = member;
  e = WR_OK;
done:
  delete d;
  return e;
}

}  // namespace Dep
