#pragma once

// The command-line engine (docs/CLI.md): one gate in front of every command, whatever carried it.
//
//   adapter (USB, BLE, LoRa, MQTT)  ->  Context (transport, authentication, remote, posture)
//   gate(line, context, role)       ->  registry says which class; the table says allow or refuse
//   refused                         ->  an explicit reply, and an audit entry (the command class and who asked)
//   allowed                         ->  the role's own handler runs the command, unchanged
//
// The adapters only frame bytes and build the context. The handlers never decide who may run what.
// Replies and audit entries of a refusal are exactly those the per-path checks gave before they were moved here.

#include <stdio.h>
#include <string.h>
#include "CliRegistry.h"
#include "MeconCli.h"
#include "../AuditLog.h"

namespace Cli {

struct Outcome {
  bool allowed = true;
  uint8_t reason = R_ALLOW;
  uint8_t cls = C_WRITE;
  bool canned = false;   // the engine answered an allowed command itself (a write-only secret)
};

inline const char* refusalText(uint8_t reason, const Resolved& r) {
  switch (reason) {
    case R_REMOTE_LIMITED: return "ERR: not permitted remotely";
    case R_HARDENED: return "ERR: not permitted in hardened posture";
    case R_CLI_OFF: return "ERR: remote CLI is off in hardened posture";
    case R_DEFAULT_PW: return "ERR: remote admin is off until the admin password is set on the local console";
    case R_KEY_HARDENED: return r.msg_hardened ? r.msg_hardened : "ERR: not permitted in hardened posture";
    case R_LOCAL_ONLY: return r.msg_local ? r.msg_local : "ERR: not permitted";
    case R_MQTT_UNVERIFIED: return "ERR: management needs a TLS-verified broker";
    case R_MQTT_READONLY: return "ERR: management is off for this broker profile";
    case R_NETCON_OFF: return "ERR: the Ethernet console is off in hardened posture";
    default: return "ERR: not permitted";
  }
}

// The audit "what" of a refusal: the over-the-air write limits and the Hardened remote CLI keep the kinds they have
// always had; a refused command that names its own kind (key export, posture, audit ...) records that kind.
inline uint8_t auditWhat(uint8_t reason, const Resolved& r) {
  switch (reason) {
    case R_REMOTE_LIMITED:
    case R_HARDENED:
    case R_MQTT_READONLY: return Posture::W_REMOTE_WRITE;
    case R_CLI_OFF: return Posture::W_REMOTE_CLI;
    case R_NETCON_OFF: return Posture::W_CLI;
    case R_DEFAULT_PW: return Posture::W_DEFAULT_PASSWORD;
    case R_MQTT_UNVERIFIED: return Posture::W_MQTT_SESSION;
    case R_LOCAL_ONLY: return r.what != Posture::W_NONE ? r.what : Posture::W_REMOTE_WRITE;
    default: return r.what != Posture::W_NONE ? r.what : Posture::W_CLI;
  }
}

inline bool refusalAudited(const Verdict& v, const Resolved& r) {
  return !(v.reason == R_LOCAL_ONLY && (r.flags & F_QUIET));   // "set prv.key" from the air is explained, not logged
}

// Run the gate for a command line. Returns true when the engine answered (a refusal, or a write-only secret) and
// `reply` holds the answer; false when the command may run and the caller's handler takes it from here.
inline bool gate(const char* cmd, const Context& ctx, uint8_t role, uint32_t now, char* reply, size_t max,
                 Outcome* out = nullptr) {
  const Resolved r = resolve(cmd, role);
  const Verdict v = decide(ctx, r.cls, r.flags);
  if (out) { out->allowed = v.allow; out->reason = v.reason; out->cls = r.cls; out->canned = false; }
  if (!v.allow) {
    if (refusalAudited(v, r)) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));
    snprintf(reply, max, "%s", refusalText(v.reason, r));
    return true;
  }
  if (r.canned) {
    if (out) out->canned = true;
    snprintf(reply, max, "%s", r.canned);
    return true;
  }
  return false;
}

// Decide for a Companion frame that is not a command line. Returns true when it may run; a refusal is audited and
// the caller answers with ERR_CODE_NOT_PERMITTED.
inline bool allowFrame(const Context& ctx, uint8_t frame, uint32_t now, Outcome* out = nullptr) {
  const Resolved r = resolveFrame(frame);
  const Verdict v = decide(ctx, r.cls, r.flags);
  if (out) { out->allowed = v.allow; out->reason = v.reason; out->cls = r.cls; out->canned = false; }
  if (!v.allow) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));
  return v.allow;
}

}  // namespace Cli
