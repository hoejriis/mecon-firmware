#include "DepAuthority.h"
#include <new>
#include <stdlib.h>
#include <string.h>
#include "ed_25519.h"
extern "C" {
#include "ge.h"
}
#include "helpers/iface/IfaceJson.h"

namespace Dep {

using namespace Iface;

const char* errCode(Err e) {
  switch (e) {
    case OK: return "ok";
    case DUPLICATE: return "duplicate";
    case BAD_AUTHORITY_SIGNATURE: return "bad_authority_signature";
    case BAD_AUTHORITY_SET: return "bad_authority_set";
    case STALE_REVISION: return "stale_revision";
    case WRONG_DEPLOYMENT: return "wrong_deployment";
    case UNTRUSTED_AUTHORITY_KEY: return "untrusted_authority_key";
  }
  return "failed";
}

bool weakKey(const uint8_t key[KEY_LEN]) {
  ge_p3 a;
  if (ge_frombytes_negate_vartime(&a, key) != 0) return true;   // not on the curve
  ge_p2 p;
  ge_p1p1 t;
  ge_p3_to_p2(&p, &a);
  for (int i = 0; i < 3; i++) {   // 8 * point
    ge_p2_dbl(&t, &p);
    ge_p1p1_to_p2(&p, &t);
  }
  fe d;
  fe_sub(d, p.Y, p.Z);
  return !fe_isnonzero(p.X) && !fe_isnonzero(d);   // the identity: X = 0, Y = Z
}

int AuthoritySet::memberIndex(const uint8_t id[ID_LEN]) const {
  for (int i = 0; i < n_members; i++)
    if (memcmp(members[i].instance_id, id, ID_LEN) == 0) return i;
  return -1;
}

bool AuthoritySet::isRevoked(const uint8_t id[ID_LEN]) const {
  for (int i = 0; i < n_revoked; i++)
    if (memcmp(revoked[i], id, ID_LEN) == 0) return true;
  return false;
}

static int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexToBytes(const char* hex, size_t hex_len, uint8_t* out, size_t n) {
  if (!hex || hex_len != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool base64ToSig(const char* b64, size_t len, uint8_t out[SIG_LEN]) {
  // 64 bytes = 21 full groups (63 bytes) + one byte: 86 characters and "==".
  if (!b64 || len != 88 || b64[86] != '=' || b64[87] != '=') return false;
  uint32_t acc = 0;
  int bits = 0;
  size_t o = 0;
  for (size_t i = 0; i < 86; i++) {
    int v = b64val(b64[i]);
    if (v < 0) return false;
    acc = acc << 6 | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (uint8_t)(acc >> bits);
    }
  }
  // The 4 bits left over must be zero, or two different strings would decode to the same signature.
  return o == SIG_LEN && (acc & ((1u << bits) - 1)) == 0;
}

// Verifies sig over: domain, 0x00, text.
static bool verifyDomain(const char* domain, const char* text, size_t len, const uint8_t sig[SIG_LEN],
                         const uint8_t key[KEY_LEN]) {
  size_t dl = strlen(domain);
  uint8_t* msg = static_cast<uint8_t*>(malloc(dl + 1 + len));
  if (!msg) return false;
  memcpy(msg, domain, dl);
  msg[dl] = 0;
  memcpy(msg + dl + 1, text, len);
  bool ok = ed25519_verify(sig, msg, dl + 1 + len, key) == 1;
  free(msg);
  return ok;
}

static bool getHex(const JsonDoc& d, int obj, const char* key, uint8_t* out, size_t n) {
  char buf[2 * KEY_LEN + 1];
  if (2 * n + 1 > sizeof(buf)) return false;
  int t = jsonFind(d, obj, key);
  return t >= 0 && jsonGetString(d, t, buf, 2 * n + 1, CS_ID, 2 * n) && hexToBytes(buf, strlen(buf), out, n);
}

static bool getU32(const JsonDoc& d, int obj, const char* key, uint32_t* out) {
  int64_t v;
  int t = jsonFind(d, obj, key);
  if (t < 0 || !jsonGetInt(d, t, 0, 0xFFFFFFFFLL, &v)) return false;
  *out = (uint32_t)v;
  return true;
}

typedef JsonBuf<MAX_TOKENS_LARGE> SetDoc;

static Err parseSet(const JsonDoc& d, AuthoritySet* s) {
  uint32_t v;
  if (!getU32(d, 0, "v", &v) || v != 1) return BAD_AUTHORITY_SET;
  if (!getHex(d, 0, "deployment_id", s->deployment_id, ID_LEN)) return BAD_AUTHORITY_SET;
  if (!getU32(d, 0, "authority_epoch", &s->authority_epoch) || s->authority_epoch == 0 || s->authority_epoch > MAX_EPOCH)
    return BAD_AUTHORITY_SET;
  if (!getU32(d, 0, "authority_revision", &s->authority_revision) || s->authority_revision == 0)
    return BAD_AUTHORITY_SET;
  int it = jsonFind(d, 0, "issued_at");
  int64_t ignored;
  if (it >= 0 && !jsonGetInt(d, it, 0, 0xFFFFFFFFLL, &ignored)) return BAD_AUTHORITY_SET;   // informational only

  int ma = jsonFind(d, 0, "members");
  if (!isArray(d, ma) || d.tok[ma].size < 1 || d.tok[ma].size > MAX_MEMBERS) return BAD_AUTHORITY_SET;
  s->n_members = (uint8_t)d.tok[ma].size;
  for (int i = 0; i < s->n_members; i++) {
    int m = jsonElem(d, ma, i);
    Member& mb = s->members[i];
    if (!isObject(d, m) || !getHex(d, m, "instance_id", mb.instance_id, ID_LEN) ||
        !getHex(d, m, "public_key", mb.public_key, KEY_LEN) ||
        !getU32(d, m, "admitted_revision", &mb.admitted_revision))
      return BAD_AUTHORITY_SET;
    // Admitted at a revision this set has not reached yet: malformed, not something to round down.
    if (mb.admitted_revision == 0 || mb.admitted_revision > s->authority_revision) return BAD_AUTHORITY_SET;
    if (weakKey(mb.public_key)) return BAD_AUTHORITY_SET;
    // Distinct ids and distinct keys: the issuer field is not signed, so two members sharing a key would let a
    // command signed by one be presented under the other's name and replay window.
    for (int j = 0; j < i; j++)
      if (memcmp(s->members[j].instance_id, mb.instance_id, ID_LEN) == 0 ||
          memcmp(s->members[j].public_key, mb.public_key, KEY_LEN) == 0)
        return BAD_AUTHORITY_SET;
  }

  s->n_revoked = 0;
  int ra = jsonFind(d, 0, "revoked");
  if (ra >= 0) {
    if (!isArray(d, ra) || d.tok[ra].size > MAX_REVOKED) return BAD_AUTHORITY_SET;
    for (int i = 0; i < d.tok[ra].size; i++) {
      char buf[2 * ID_LEN + 1];
      int e = jsonElem(d, ra, i);
      if (!jsonGetString(d, e, buf, sizeof(buf), CS_ID, 2 * ID_LEN) ||
          !hexToBytes(buf, strlen(buf), s->revoked[i], ID_LEN))
        return BAD_AUTHORITY_SET;
      if (s->memberIndex(s->revoked[i]) >= 0) return BAD_AUTHORITY_SET;
    }
    s->n_revoked = (uint8_t)d.tok[ra].size;
  }
  return OK;
}

Err acceptSet(const char* text, size_t len, const uint8_t sig[SIG_LEN], const SetContext& ctx, AuthoritySet* out) {
  if (!text || len == 0 || len > MAX_SET_TEXT) return BAD_AUTHORITY_SET;
  // 1. The signature first, over the bytes as received.
  if (!verifyDomain("MECON-AUTHSET-v1", text, len, sig, ctx.dak)) return BAD_AUTHORITY_SIGNATURE;

  SetDoc* d = new (std::nothrow) SetDoc();
  if (!d) return BAD_AUTHORITY_SET;
  Err e = jsonParse(*d, text, len) == J_OK ? parseSet(*d, out) : BAD_AUTHORITY_SET;
  delete d;
  if (e != OK) return e;

  // 3. Its Deployment. 4. Its epoch, after any rotation delivered with it.
  if (memcmp(out->deployment_id, ctx.deployment_id, ID_LEN) != 0) return WRONG_DEPLOYMENT;
  if (out->authority_epoch != ctx.authority_epoch) return UNTRUSTED_AUTHORITY_KEY;
  // 5. Revision: higher applies, equal is a duplicate, lower is stale.
  if (ctx.has_stored) {
    if (out->authority_revision == ctx.stored_revision) return DUPLICATE;
    if (out->authority_revision < ctx.stored_revision) return STALE_REVISION;
  }
  return OK;
}

Err applyRotation(const char* text, size_t len, const uint8_t sig[SIG_LEN], const uint8_t deployment_id[ID_LEN],
                  RootState* st) {
  if (!text || len == 0 || len > 1024) return UNTRUSTED_AUTHORITY_KEY;
  if (!verifyDomain("MECON-DAKROT-v1", text, len, sig, st->dak)) return UNTRUSTED_AUTHORITY_KEY;
  RequestDoc* d = new (std::nothrow) RequestDoc();
  if (!d) return UNTRUSTED_AUTHORITY_KEY;
  Err e = UNTRUSTED_AUTHORITY_KEY;
  uint32_t v, from, to;
  uint8_t dep[ID_LEN], key[KEY_LEN];
  if (jsonParse(*d, text, len) == J_OK && getU32(*d, 0, "v", &v) && v == 1 &&
      getHex(*d, 0, "deployment_id", dep, ID_LEN) && getU32(*d, 0, "from_epoch", &from) &&
      getU32(*d, 0, "to_epoch", &to) && getHex(*d, 0, "new_public_key", key, KEY_LEN)) {
    if (memcmp(dep, deployment_id, ID_LEN) != 0) e = WRONG_DEPLOYMENT;
    else if (from == st->authority_epoch && to == from + 1 && to <= MAX_EPOCH && !weakKey(key)) {
      memcpy(st->dak, key, KEY_LEN);
      st->authority_epoch = to;
      e = OK;
    }
  }
  delete d;
  return e;
}

}  // namespace Dep
