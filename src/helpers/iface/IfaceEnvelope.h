#pragma once
// Request parsing and result/event serialization for the three envelope shapes (DEVICE_INTERFACE.md section 3).
#include "IfaceJson.h"

namespace Iface {

const int INTERFACE_MAJOR = 1;
const size_t ID_MAX = 32;
const size_t OP_MAX = 32;

enum Status {
  ST_OK = 0,
  ST_REFUSED,              // not authorized; nothing ran
  ST_INVALID,              // malformed or bad arguments; nothing ran
  ST_UNKNOWN_OP,
  ST_UNSUPPORTED_VERSION,
  ST_BUSY,                 // a bound was reached; retry later
  ST_FAILED                // ran and failed; "error" says why
};
const char* statusName(Status s);

// What a result answers, named inside it before it is signed (AUTHORITY.md section 3.4): the operation, and the job id of a
// signed request. Empty op: the request was not understood far enough to name one.
struct Answering {
  char op[OP_MAX + 1];
  bool has_job;
  uint32_t job;
  void clear() { op[0] = 0; has_job = false; job = 0; }
};

struct Request {
  char id[ID_MAX + 1];
  char op[OP_MAX + 1];
  bool has_id;     // false: the id could not be trusted, so a reply cannot echo it
  int args;        // token index of the "args" object, -1 when absent
};

// ST_OK when req is filled in and the request may be dispatched. Anything else is the status to answer with;
// req.has_id says whether the answer can carry the id. Checks in order: JSON, id, v, auth, op, args.
Status parseRequest(JsonDoc& doc, const char* msg, size_t len, Request& req);

// Writers return the byte count, or -1 when buf is too small (never a truncated message).
// result_json / data_json are complete JSON objects produced by the caller, or null for {}.
int writeResult(char* buf, size_t cap, const char* id, Status st, const char* result_json, const char* error);
int writeEvent(char* buf, size_t cap, const char* ev, uint32_t seq, uint32_t epoch, bool ts_known, int64_t ts,
               const char* data_json);

}  // namespace Iface
