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
    case WR_BAD_CHANNEL_NONCE: return "bad_channel_nonce";
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

static bool streamIsPrivate(const JsonDoc& d) {
  static const char* const PRIVATE[] = { "screen", "location", "logs", "inventory.entries" };
  char name[33];
  int ta = jsonFind(d, 0, "args");
  int ts = ta >= 0 ? jsonFind(d, ta, "stream") : -1;
  if (ts < 0 || !jsonGetString(d, ts, name, sizeof(name), CS_NAME, 1)) return true;   // unreadable: the strict side
  for (const char* p : PRIVATE)
    if (strcmp(name, p) == 0) return true;
  return false;
}

OpClass contractOpClass(const char* op, const JsonDoc& body) {
  static const char* const PLAIN[] = { "hello", "status.get", "config.schema", "config.get", "deployment.get",
                                       "job.get", "job.list" };
  for (const char* p : PLAIN)
    if (strcmp(op, p) == 0) return OPC_PLAIN;
  if (strcmp(op, "sub.start") == 0 || strcmp(op, "sub.stop") == 0)
    return streamIsPrivate(body) ? (strcmp(op, "sub.start") == 0 ? OPC_SENSITIVE : OPC_JOB) : OPC_PLAIN;
  // deployment.set_unlock carries the local unlock secret: sensitive, so it also needs the link's channel nonce.
  if (strcmp(op, "identity.key.export") == 0 || strcmp(op, "ble.pin.get") == 0 || strcmp(op, "deployment.set_unlock") == 0)
    return OPC_SENSITIVE;
  return OPC_JOB;
}

WrapErr verifyWrapped(const char* wire, size_t len, const Record& rec, const AuthoritySet& set,
                      const uint8_t own_node_key[KEY_LEN], OpClassFn classify, const char* link_nonce, char* body_buf,
                      size_t cap, Verified* out) {
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
    uint8_t dep[ID_LEN], target[KEY_LEN], bound_issuer[ID_LEN];
    // 4. This Deployment. 4b. This device.
    if (!hexField(*d, "deployment_id", dep, ID_LEN)) goto done;
    if (memcmp(dep, rec.deployment_id, ID_LEN) != 0) { e = WR_WRONG_DEPLOYMENT; goto done; }
    if (!hexField(*d, "target_public_key", target, KEY_LEN)) goto done;   // strict: an unbound body is refused
    if (memcmp(target, own_node_key, KEY_LEN) != 0) { e = WR_WRONG_DEVICE; goto done; }
    // 5. Not a set this device has not seen. 6. Not from before the issuer's (re)admission.
    if (!u32Field(*d, "authority_revision", &out->stamped_revision)) goto done;
    if (out->stamped_revision > rec.authority_revision) { e = WR_AUTHORITY_REVISION_AHEAD; goto done; }
    if (out->stamped_revision < set.members[member].admitted_revision) { e = WR_STALE_AUTHORITY; goto done; }
    // 6b. The issuer the signature was checked against is the one the body names.
    if (!hexField(*d, "issuer", bound_issuer, ID_LEN)) goto done;
    if (memcmp(bound_issuer, issuer, ID_LEN) != 0) { e = WR_BAD_SIGNATURE; goto done; }
    out->has_job = jsonFind(*d, 0, "job_id") >= 0;
    if (out->has_job && (!u32Field(*d, "job_id", &out->job_id) || out->job_id == 0)) goto done;
    // The operation, and a job id on anything that is not a plain read: without one nothing guards it against replay.
    int to = jsonFind(*d, 0, "op");
    if (to < 0 || !jsonGetString(*d, to, out->op, sizeof(out->op), CS_NAME, 1)) goto done;
    out->cls = classify ? classify(out->op, *d) : OPC_JOB;
    if (out->cls != OPC_PLAIN && !out->has_job) goto done;
    // A sensitive reply goes only to the link the device issued the nonce on.
    if (out->cls == OPC_SENSITIVE) {
      char nonce[65];
      int tn = jsonFind(*d, 0, "channel_nonce");
      if (!link_nonce || tn < 0 || !jsonGetString(*d, tn, nonce, sizeof(nonce), CS_ID, 1) ||
          strcmp(nonce, link_nonce) != 0) {
        e = WR_BAD_CHANNEL_NONCE;
        goto done;
      }
    }
  }
  out->member = member;
  e = WR_OK;
done:
  delete d;
  return e;
}

}  // namespace Dep
