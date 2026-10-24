#include "IfaceDispatch.h"
#include <string.h>

namespace Iface {

const OpEntry* Dispatcher::find(const char* name) const {
  for (size_t i = 0; i < _n; i++)
    if (strcmp(_t[i].name, name) == 0) return &_t[i];
  return nullptr;
}

int Dispatcher::handle(const char* msg, size_t len, char* out, size_t cap, ResultCache* cache) {
  JsonDoc doc;
  Request req;
  Status st = parseRequest(doc, msg, len, req);
  const char* id = req.has_id ? req.id : "";
  const char* err = nullptr;
  int n = -1;

  if (st == ST_OK && cache) {
    const char* data; size_t dl;
    ResultCache::Lookup l = cache->find(req.id, &data, &dl);
    if (l == ResultCache::HIT) {
      if (dl >= cap) return -1;
      memcpy(out, data, dl); out[dl] = 0;
      return (int)dl;
    }
    if (l == ResultCache::OVERSIZE) return writeResult(out, cap, id, ST_BUSY, nullptr, "result-not-kept");
  }

  if (st == ST_OK) {
    const OpEntry* op = find(req.op);
    if (!op) st = ST_UNKNOWN_OP;
    else if (!_auth || !_auth(_auth_user, *op, req)) st = ST_REFUSED;
    else {
      JsonWriter w(out, cap);
      OpContext ctx = { &req, &doc, &w, nullptr, _handler_user };
      w.beginObject();
      w.key("v"); w.num(INTERFACE_MAJOR);
      w.key("id"); w.str(req.id);
      w.key("status"); w.str("ok");
      w.key("result"); w.beginObject();
      st = op->handler(ctx);
      w.endObject();
      w.endObject();
      if (st == ST_OK && w.ok() && w.balanced()) n = (int)w.size();
      else {
        // an overflowing or unbalanced result is never sent: the handler ran, so it failed
        if (st == ST_OK || !w.ok()) { st = ST_FAILED; err = "result-too-large"; }
        else err = ctx.error;
      }
      if (st == ST_FAILED && !err) err = ctx.error ? ctx.error : "failed";
    }
  }
  if (n < 0) n = writeResult(out, cap, id, st, nullptr, err);
  if (n > 0 && cache && req.has_id && (st == ST_OK || st == ST_FAILED || st == ST_REFUSED))
    cache->store(req.id, out, (size_t)n);
  return n;
}

}  // namespace Iface
