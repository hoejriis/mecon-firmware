#pragma once
// Operation table and dispatcher. Authorization is injected and runs before any handler, so the privilege
// engine (src/helpers/cli) can decide with the caller's real context. DEVICE_INTERFACE.md sections 4 and 6.
#include "IfaceCache.h"
#include "IfaceEnvelope.h"

namespace Iface {

enum Priv { PRIV_READ, PRIV_WRITE, PRIV_ADMIN };

struct OpContext {
  const Request* req;
  const JsonDoc* doc;     // for reading req->args
  JsonWriter* out;        // positioned inside the "result" object: write members only
  const char* error;      // a handler returning ST_FAILED sets a short reason here
  void* user;
};
typedef Status (*OpHandler)(OpContext& ctx);

struct OpEntry {
  const char* name;
  OpHandler handler;
  Priv priv;
  bool mutating;   // changes state: its ok or failed result is kept, so a retried request does not run it twice
};

// True to allow. A null callback refuses everything: an unwired dispatcher fails closed.
typedef bool (*AuthFn)(void* user, const OpEntry& op, const Request& req);

// Optional: the error code a refusal carries (the contract's spelling), or nullptr for none. Asked only after the
// AuthFn refused, so it can say why.
typedef const char* (*DenyFn)(void* user);

class Dispatcher {
public:
  Dispatcher(const OpEntry* table, size_t n, AuthFn auth, void* auth_user, void* handler_user = nullptr)
    : _t(table), _n(n), _auth(auth), _auth_user(auth_user), _handler_user(handler_user) {}

  void setDenyReason(DenyFn f) { _deny = f; }
  // Receives the operation name of each request that parses, for the result to name (Provenance).
  void setAnswering(Answering* a) { _ans = a; }

  // Handles one complete request message and writes the response envelope into out. Returns its length, or -1
  // when even an error response does not fit out (the caller then answers busy through its own channel).
  // With a cache, a duplicate id returns the stored response without running the handler, after rechecking the
  // caller's current authority; a duplicate whose response is no longer kept gets failed / result-expired. Only the ok and failed responses of mutating operations are kept:
  // a read, or a request that did not run, is simply evaluated again.
  int handle(const char* msg, size_t len, char* out, size_t cap, ResultCache* cache);

private:
  int handleWith(JsonDoc& doc, const char* msg, size_t len, char* out, size_t cap, ResultCache* cache);
  const OpEntry* find(const char* name) const;
  const OpEntry* _t; size_t _n; AuthFn _auth; void* _auth_user; void* _handler_user; DenyFn _deny = nullptr; Answering* _ans = nullptr;
};

}  // namespace Iface
