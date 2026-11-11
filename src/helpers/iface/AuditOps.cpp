#include "AuditOps.h"

#include "IfaceJson.h"
#include "Streams.h"
#include "../Posture.h"
#include "../cli/CliPolicy.h"

namespace Iface {

Status opAuditList(OpContext& c, bool (*allow)(void* user, uint8_t cls), void* user) {
  if (!allow || !allow(user, Cli::C_POSTURE)) { c.error = "not-permitted"; return ST_REFUSED; }
  const JsonDoc& d = *c.doc;
  int64_t cursor = 0;
  const int ct = c.req->args >= 0 ? jsonFind(d, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(d, ct, 0, Posture::AuditRing::CAPACITY, &cursor)) return ST_INVALID;
  int64_t limit = AUDIT_PAGE;
  const int lt = c.req->args >= 0 ? jsonFind(d, c.req->args, "limit") : -1;
  if (lt >= 0 && !jsonGetInt(d, lt, 1, AUDIT_PAGE, &limit)) return ST_INVALID;
  const Posture::AuditRing& r = Posture::audit();
  const unsigned total = r.n;
  JsonWriter& w = *c.out;
  w.key("total"); w.unum(total);
  w.key("entries"); w.beginArray();
  unsigned i = (unsigned)cursor;
  for (; i < total && i < (unsigned)(cursor + limit); i++) {
    const Posture::AuditEntry& a = r.e[total - 1 - i];   // e[n-1] is the newest
    w.beginObject();   // the logs stream's names, as formatLogData writes them
    w.key("ts"); if (a.ts >= 1704067200) w.unum(a.ts); else w.null();
    w.key("event"); w.str(Posture::AuditRing::eventName(a.event));
    w.key("caller"); w.str(Posture::AuditRing::callerName(a.caller));
    w.key("what"); w.str(Posture::AuditRing::whatName(a.event, a.what));
    w.key("count"); w.unum(a.count);
    w.endObject();
  }
  w.endArray();
  w.key("next");
  if (i < total) w.unum(i); else w.null();
  return ST_OK;
}

}  // namespace Iface
