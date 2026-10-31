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
// This file does checks 1 to 6b and the job-id and channel-nonce rules (docs/contract/AUTHORITY.md section 7). Check 7,
// the sequence, is the caller's (DepReplay.h), with the member's mark from the marks record.
#include <stddef.h>
#include <stdint.h>
#include "DepAuthority.h"
#include "DepRecord.h"
#include "helpers/iface/IfaceJson.h"

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
  WR_BAD_CHANNEL_NONCE,          // a sensitive operation without the nonce issued for this link
  WR_MALFORMED                   // not a wrapper we can read, or a body without the bound fields
};
const char* wrapErrCode(WrapErr e);   // contract spelling; WR_MALFORMED and WR_NOT_WRAPPED read "unauthenticated"

// What an operation needs (contract section 7.1); see contractOpClass below.
enum OpClass : uint8_t { OPC_PLAIN = 0, OPC_JOB, OPC_SENSITIVE };

struct Verified {
  int member;               // index in the authority set
  char op[33];              // the body's operation
  const char* body;         // the decoded body, in the caller's buffer, NUL terminated
  size_t body_len;
  OpClass cls;
  bool has_job;             // false for a plain read, which carries no job id
  uint32_t job_id;
  uint32_t stamped_revision;
};

// What an operation needs (contract section 7.1). Plain reads are an allowlist; anything else, an operation this
// device does not know included, carries a job id; a sensitive one also carries the link's channel nonce.
typedef OpClass (*OpClassFn)(const char* op, const Iface::JsonDoc& body);
// The contract's classification: hello, status.get, config.schema, config.get, deployment.get, job.get, job.list and
// sub.start/sub.stop of a stream that is not private are plain; identity.key.export, ble.pin.get and sub.start of a
// private stream (screen, location, logs, inventory) are sensitive; everything else needs a job id.
OpClass contractOpClass(const char* op, const Iface::JsonDoc& body);

// Checks 1 to 6b for wire against the stored record and its parsed set. body_buf receives the decoded body (it must
// hold MAX_WRAPPED bytes). own_node_key is this device's node public key (the device binding, check 4b). An operation
// that is not plain without a job id is refused (unauthenticated, as a body without its bound fields). link_nonce is the
// channel nonce this device issued for the link the request arrived on (nullptr when it has none): a sensitive
// operation must carry it. The caller retires the nonce once the request runs (single use).
WrapErr verifyWrapped(const char* wire, size_t len, const Record& rec, const AuthoritySet& set,
                      const uint8_t own_node_key[KEY_LEN], OpClassFn classify, const char* link_nonce, char* body_buf,
                      size_t cap, Verified* out);

}  // namespace Dep
