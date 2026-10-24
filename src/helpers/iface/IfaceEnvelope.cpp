#include "IfaceEnvelope.h"

namespace Iface {

const char* statusName(Status s) {
  switch (s) {
    case ST_OK: return "ok";
    case ST_REFUSED: return "refused";
    case ST_INVALID: return "invalid";
    case ST_UNKNOWN_OP: return "unknown-op";
    case ST_UNSUPPORTED_VERSION: return "unsupported-version";
    case ST_BUSY: return "busy";
    default: return "failed";
  }
}

Status parseRequest(JsonDoc& doc, const char* msg, size_t len, Request& req) {
  req.id[0] = 0; req.op[0] = 0; req.has_id = false; req.args = -1;
  if (jsonParse(doc, msg, len) != J_OK) return ST_INVALID;
  if (!jsonGetString(doc, jsonFind(doc, 0, "id"), req.id, sizeof(req.id), CS_ID, 1)) { req.id[0] = 0; return ST_INVALID; }
  req.has_id = true;
  int64_t v;
  if (!jsonGetInt(doc, jsonFind(doc, 0, "v"), 0, 1000000, &v)) return ST_INVALID;
  if (v != INTERFACE_MAJOR) return ST_UNSUPPORTED_VERSION;
  // v1.0 implements no signed authority, so a request claiming some is refused, not run as if unsigned
  if (jsonFind(doc, 0, "auth") >= 0) return ST_REFUSED;
  if (!jsonGetString(doc, jsonFind(doc, 0, "op"), req.op, sizeof(req.op), CS_NAME, 1)) return ST_INVALID;
  int a = jsonFind(doc, 0, "args");
  if (a >= 0 && !isObject(doc, a)) return ST_INVALID;
  req.args = a;
  return ST_OK;
}

static int finish(const JsonWriter& w) { return w.ok() && w.balanced() ? (int)w.size() : -1; }

int writeResult(char* buf, size_t cap, const char* id, Status st, const char* result_json, const char* error) {
  JsonWriter w(buf, cap);
  w.beginObject();
  w.key("v"); w.num(INTERFACE_MAJOR);
  w.key("id"); w.str(id);
  w.key("status"); w.str(statusName(st));
  if (error) { w.key("error"); w.str(error); }
  if (st == ST_OK) { w.key("result"); w.raw(result_json ? result_json : "{}"); }
  w.endObject();
  return finish(w);
}

int writeEvent(char* buf, size_t cap, const char* ev, uint32_t seq, uint32_t epoch, bool ts_known, int64_t ts,
               const char* data_json) {
  JsonWriter w(buf, cap);
  w.beginObject();
  w.key("v"); w.num(INTERFACE_MAJOR);
  w.key("ev"); w.str(ev);
  w.key("seq"); w.unum(seq);
  w.key("epoch"); w.unum(epoch);
  w.key("ts"); if (ts_known) w.num(ts); else w.null();
  w.key("data"); w.raw(data_json ? data_json : "{}");
  w.endObject();
  return finish(w);
}

}  // namespace Iface
