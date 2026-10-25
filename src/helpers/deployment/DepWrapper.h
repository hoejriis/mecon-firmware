#pragma once
// A signed request on the device interface: the Deployment authority contract's command envelope (section 6)
// carrying a v1.0 interface request as its body.
//
//   {"mecon_auth":1,"issuer":"<instance id>","sig":"<base64>","body":"<the exact request JSON text>"}
//
// signed over "MECON-DEVMSG-v1" 0x00 "iface" 0x00 <body bytes as received>. One suffix for every operation, so the
// same signed bytes are valid over MQTT, USB and BLE; the operation is inside the body. PROVISIONAL: this binding
// is the one proposed on the trust Issue and changes if its review changes it.
//
// This file does checks 1 to 6. Check 7, replay, needs the job class, which depends on the operation, so the caller
// runs it (DepReplay.h) after the dispatcher has read the body.
#include <stddef.h>
#include <stdint.h>
#include "DepAuthority.h"
#include "DepRecord.h"

namespace Dep {

const size_t MAX_WRAPPED = 4096;   // the interface's reassembly limit (DEVICE_INTERFACE.md section 3)

enum WrapErr : uint8_t {
  WR_OK = 0,
  WR_NOT_WRAPPED,                // no mecon_auth member: an unsigned request (the caller decides if that is allowed)
  WR_UNSUPPORTED_AUTH_VERSION,
  WR_UNKNOWN_ISSUER,
  WR_ISSUER_REVOKED,
  WR_BAD_SIGNATURE,
  WR_WRONG_DEPLOYMENT,
  WR_WRONG_DEVICE,
  WR_AUTHORITY_REVISION_AHEAD,
  WR_STALE_AUTHORITY,
  WR_PAYLOAD_TOO_LARGE,
  WR_MALFORMED                   // not a wrapper we can read, or a body without the bound fields
};
const char* wrapErrCode(WrapErr e);   // contract spelling; WR_MALFORMED and WR_NOT_WRAPPED read "unauthenticated"

struct Verified {
  int member;               // index in the authority set
  char op[33];              // the body's operation
  const char* body;         // the decoded body, in the caller's buffer, NUL terminated
  size_t body_len;
  bool has_job;             // false for a read-only operation, which carries no job id
  uint32_t job_id;
  uint32_t stamped_revision;
};

// Whether an operation changes state: such a request must carry a job id, so that replay protection covers it.
typedef bool (*MutatingFn)(const char* op);

// Checks 1 to 6 for wire against the stored record and its parsed set. body_buf receives the decoded body (it must
// hold MAX_WRAPPED bytes). own_node_key is this device's node public key (the device binding, check 4b). A
// state-changing operation (mutating) without a job id is refused: unauthenticated, as a body without its bound fields.
WrapErr verifyWrapped(const char* wire, size_t len, const Record& rec, const AuthoritySet& set,
                      const uint8_t own_node_key[KEY_LEN], MutatingFn mutating, char* body_buf, size_t cap,
                      Verified* out);

}  // namespace Dep
