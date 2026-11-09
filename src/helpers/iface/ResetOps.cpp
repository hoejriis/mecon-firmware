#include "ResetOps.h"
#include <string.h>

namespace Iface {

namespace {

const char* const NAMES[RS_COUNT] = {"network", "contacts", "channels", "radio", "bonds", "deployment", "identity", "factory"};

}  // namespace

Status opDeviceReset(OpContext& c, ResetHost& h) {
  char name[16];
  const int t = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "scope") : -1;
  if (!jsonGetString(*c.doc, t, name, sizeof(name), CS_NAME, 1)) { c.error = "bad-scope"; return ST_INVALID; }
  int s = -1;
  for (int i = 0; i < RS_COUNT; i++) if (strcmp(name, NAMES[i]) == 0) s = i;
  if (s < 0) { c.error = "bad-scope"; return ST_INVALID; }
  // AUTHORITY section 11 owns these two, with their own operations and rules: one place decides them.
  if (s == RS_DEPLOYMENT) { c.error = "use-deployment.reset"; return ST_INVALID; }
  if (s == RS_IDENTITY) { c.error = "use-identity.rotate"; return ST_INVALID; }
  if (s == RS_BONDS) { c.error = "not-built"; return ST_FAILED; }   // each BLE stack's bond store, with a board check each
  if (!(h.scopes & (1u << s))) { c.error = "not-on-this-device"; return ST_FAILED; }
  if (!h.reset(h.user, (ResetScope)s, h.caller)) { c.error = "io-error"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  w.key("scope"); w.str(NAMES[s]);
  w.key("reboot"); w.boolean(true);
  h.reboot_soon(h.user);
  return ST_OK;
}

}  // namespace Iface
