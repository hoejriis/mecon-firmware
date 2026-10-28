#pragma once

// Standalone security postures (Normal / Hardened) for Companion, Repeater and Room server.
//
// The posture is one byte kept with the prefs (atomic save). Any value other than NORMAL is treated as
// HARDENED, so a damaged byte can only make the device stricter; USB factory reset is the way back.
//
//                                   Normal                          Hardened
//   Management over USB / UART      yes                             yes
//   BLE                             non-default PIN only            same; with a default PIN BLE does not start
//   TCP companion server (Wi-Fi,    messaging only                  not started
//     Ethernet)
//   Remote admin over RF            refused while the admin         read-only plus reboot
//     (repeater / room)             password is the stock default
//   Remote CLI on a Companion       reads plus reboot               off
//   Private key export / import     trusted local transports        refused on every transport, USB included
//   Factory reset                   trusted local transports        USB / UART only; returns to Normal
//
// In both postures a stock default admin password (or an empty one) refuses remote admin login and remote
// admin commands until the password has been set from the local console.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "AuditLog.h"
#include "cli/CliEngine.h"
#include "cli/CliCaller.h"

// Who may do what is decided by one table (cli/CliPolicy.h, docs/CLI.md). The functions below keep their names and
// results for the code and the tests that call them, and each asks the table; none holds a rule of its own.

namespace Posture {

enum Level : uint8_t { NORMAL = 0, HARDENED = 1 };

inline bool isHardened(uint8_t posture) { return posture != NORMAL; }   // fail closed on a damaged value

// ---- start-up decisions
// BLE with the static default PIN (0 means "build default", 123456 is the published one) authenticates nobody.
inline bool bleMayStart(uint8_t posture, uint32_t active_pin) {
  return !isHardened(posture) || (active_pin != 0 && active_pin != 123456);
}
inline bool tcpServerMayStart(uint8_t posture) { return !isHardened(posture); }

// ---- per-command decisions
inline bool keyTransferAllowed(uint8_t posture) {
  return Cli::decide(Cli::contextForCaller(CALLER_CABLE, posture), Cli::C_SECRET).allow;
}
// Hardened: only a cable may factory-reset, so a paired phone cannot strip the posture.
inline bool factoryResetAllowed(uint8_t posture, uint8_t caller) {
  return Cli::decide(Cli::contextForCaller(caller, posture), Cli::C_DESTRUCTIVE).allow;
}

// The admin password is "set" once it is neither empty nor the stock value. `build_default` is the build's
// ADMIN_PASSWORD (it can differ from the stock "password"); both count as default.
inline bool isDefaultAdminPassword(const char* pw, const char* build_default) {
  return pw[0] == 0 || strcmp(pw, "password") == 0 || (build_default && strcmp(pw, build_default) == 0);
}

// What a remote admin may run in Hardened: reads and reboot. Anything else changes state or transmits.
inline bool isRemoteReadOnly(const char* c) {
  const uint8_t cls = Cli::classify(c, Cli::ROLE_REPEATER);
  return cls == Cli::C_READ || cls == Cli::C_REBOOT;
}

// ---- posture / audit commands (shared by all roles)
// Handles "get posture", "get audit [n]" and "set posture ...". Returns false when `cmd` is none of them.
// `caller` is who is asking; trusted local callers are CALLER_CABLE and CALLER_BLE, the rest are remote or
// unauthenticated. `changed` is set when the stored posture changed and the prefs must be saved.
inline bool handleCommand(const char* cmd, uint8_t caller, uint8_t& posture, uint32_t now, char* reply, size_t max,
                          bool& changed) {
  const Cli::Context ctx = Cli::contextForCaller(caller, posture);
  if (strcmp(cmd, "get posture") == 0) {
    snprintf(reply, max, "> %s", isHardened(posture) ? "hardened" : "normal");
    return true;
  }
  if (strcmp(cmd, "get audit") == 0 || memcmp(cmd, "get audit ", 10) == 0) {
    if (!Cli::decide(ctx, Cli::C_POSTURE).allow) {
      recordRefusal(now, caller, W_AUDIT);
      snprintf(reply, max, "ERR: not permitted");
      return true;
    }
    unsigned start = 0;
    for (const char* p = cmd + 9; *p; p++) {
      if (*p >= '0' && *p <= '9') start = start * 10 + (*p - '0');
      if (start > 1000) break;
    }
    audit().formatPage(start, reply, max);
    return true;
  }
  if (memcmp(cmd, "set posture", 11) == 0 && (cmd[11] == 0 || cmd[11] == ' ')) {
    const char* arg = cmd + 11;
    while (*arg == ' ') arg++;
    bool to_hardened = strcmp(arg, "hardened") == 0;
    bool to_normal = strcmp(arg, "normal") == 0 || strcmp(arg, "normal confirm") == 0;
    if (!to_hardened && !to_normal) {
      snprintf(reply, max, "ERR: set posture hardened | set posture normal confirm");
      return true;
    }
    // raising: any trusted local transport; lowering: a cable only. Remote callers can do neither.
    bool allowed = Cli::decide(ctx, Cli::C_POSTURE, to_hardened ? 0 : Cli::F_CABLE_ONLY).allow;
    if (!allowed) {
      recordRefusal(now, caller, W_POSTURE);
      snprintf(reply, max, "ERR: not permitted");
      return true;
    }
    if (to_normal && strcmp(arg, "normal confirm") != 0) {
      snprintf(reply, max, "ERR: lowering the posture needs: set posture normal confirm");
      return true;
    }
    uint8_t want = to_hardened ? HARDENED : NORMAL;
    if (isHardened(posture) == (want == HARDENED)) {
      snprintf(reply, max, "OK - already %s", to_hardened ? "hardened" : "normal");
      return true;
    }
    posture = want;
    changed = true;
    audit().add(now, EV_POSTURE_SET, caller, want);
    snprintf(reply, max, "OK - posture %s", to_hardened ? "hardened" : "normal");
    return true;
  }
  return false;
}

// The front door of the Repeater and Room server command handler. Returns true when the command was answered
// here (refused by the gate, or a posture / audit command). `remote` is a caller over the air (sender_timestamp != 0);
// the local caller is the UART / USB console. `changed` is set when the prefs must be saved.
inline bool adminFrontDoor(const char* command, bool remote, uint8_t& posture, const char* admin_pw,
                           const char* build_default, uint32_t now, char* reply, size_t max, bool& changed) {
  uint8_t caller = remote ? CALLER_REMOTE_ADMIN : CALLER_CABLE;
  // a remote session that predates the rule, or none, is still "default password": the table refuses it
  // withServerCaller: once the device holds a Deployment record the context says so, and a verified signed line on the
  // console runs as the signed principal (cli/CliCaller.h).
  const Cli::Context ctx = Cli::withServerCaller(
      remote ? Cli::Context(Cli::T_LORA, isDefaultAdminPassword(admin_pw, build_default) ? Cli::A_ADMIN_DEFAULT_PW : Cli::A_ADMIN, posture)
             : Cli::Context(Cli::T_USB, Cli::A_CABLE, posture));
  if (Cli::gate(command, ctx, Cli::ROLE_REPEATER, now, reply, max)) return true;
  if (handleCommand(command, caller, posture, now, reply, max, changed)) return true;
  return false;
}

}  // namespace Posture
