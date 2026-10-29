#pragma once
// hello's signed device_id members (AUTHORITY.md section 3.5), shared by the Companion and the Repeater/Room console.
#include "DevIdSig.h"
#include "IfaceDispatch.h"
#include "IfaceJson.h"

namespace Iface {

// Writes device_id_sig, and for args.nonce also "nonce" (echoed) and device_id_sig_nonce. INVALID, with nothing
// written, for a nonce that is not 1 to 32 of [0-9A-Za-z._-]. Writes nothing while the device has no device_id.
template <class Id>
Status writeDeviceIdSig(OpContext& c, const Id& id, const char* device_id_hex) {
  char nonce[DEVID_NONCE_MAX + 1] = {0};
  const int t = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "nonce") : -1;
  if (t >= 0 && !jsonGetString(*c.doc, t, nonce, sizeof(nonce), CS_NAME, 1)) return ST_INVALID;
  char sig[89], sig_n[89];
  if (!signDeviceId(sig, id, device_id_hex, "")) return ST_OK;
  if (nonce[0] && !signDeviceId(sig_n, id, device_id_hex, nonce)) return ST_INVALID;
  c.out->key("device_id_sig"); c.out->str(sig);
  if (nonce[0]) { c.out->key("nonce"); c.out->str(nonce); c.out->key("device_id_sig_nonce"); c.out->str(sig_n); }
  return ST_OK;
}

}  // namespace Iface
