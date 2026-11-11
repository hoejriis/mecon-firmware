#include "KeyExportOps.h"
#include <string.h>
#include "KeyWrap.h"
#include "Wipe.h"
#include "../../Identity.h"
#include "../../MeshCore.h"

namespace Iface {

namespace {

void hexOf(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

void base64Of(char* out, const uint8_t* b, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < n ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
    out[o++] = A[v >> 18 & 63];
    out[o++] = A[v >> 12 & 63];
    out[o++] = i + 1 < n ? A[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? A[v & 63] : '=';
  }
  out[o] = 0;
}

}  // namespace

Status opKeyExport(OpContext& c, KeyExportHost& h) {
  if (!h.build_allows) { c.error = "disabled"; return ST_FAILED; }
  if (h.hardened) { c.error = "hardened"; return ST_REFUSED; }
  bool clear = false, wrapped = false;
  if (h.record == Dep::LS_ABSENT)
    clear = h.caller == Cli::P_CABLE || (h.phone_allowed && h.caller == Cli::P_PAIRED);
  else if (h.record == Dep::LS_CORRUPT)
    clear = h.caller == Cli::P_CABLE;
  else
    wrapped = h.caller == Cli::P_SIGNED && h.member_key && h.member_id;
  if (!clear && !wrapped) {
    c.error = h.record == Dep::LS_OK ? "signed-only" : "not-over-this-link";
    return ST_REFUSED;
  }
  uint8_t key[PRV_KEY_SIZE];
  // writeTo only reads the key; it is not declared const
  if (const_cast<mesh::LocalIdentity*>(h.id)->writeTo(key, PRV_KEY_SIZE) != PRV_KEY_SIZE) { c.error = "no-key"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  if (clear) {
    char hex[2 * PRV_KEY_SIZE + 1];
    hexOf(hex, key, PRV_KEY_SIZE);
    w.key("key"); w.str(hex);
    wipe(hex, sizeof(hex));
  } else {
    uint8_t wrap[PRV_KEY_SIZE + KX_TAG];
    wrapKeyForMember(*h.id, h.member_key, h.job_id, key, PRV_KEY_SIZE, wrap);
    char b64[4 * ((sizeof(wrap) + 2) / 3) + 1];
    base64Of(b64, wrap, sizeof(wrap));
    char id[33];
    hexOf(id, h.member_id, 16);
    w.key("wrapped"); w.str(b64);
    w.key("for"); w.str(id);
    w.key("alg"); w.str("mecon-kx-v1");
    wipe(wrap, sizeof(wrap));
  }
  wipe(key, sizeof(key));
  return ST_OK;
}

}  // namespace Iface
