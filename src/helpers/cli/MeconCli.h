#pragma once

// MECON's own commands of the command-line engine (docs/CLI.md). They are tried at the place where upstream calls
// MainBoard::handleCommand(), before the board's own override, so a board that adds vendor commands keeps them and
// no board class has to change. New names live under `mecon.`.
//
//   get mecon.cli.who             how the engine sees the caller: transport, authentication, posture, row of the table
//   get mecon.cli.check <line>    what the table would say to <line> for this caller, without running it or auditing it
//   get mecon.mem                 heap free now, largest allocatable block, lowest free since boot (where tracked), and the
//                                 heap floor check: heap_low=1|0 and floor=<bytes> (0 = no check on this role or platform)
//   get mecon.version             the build's full identity and the upstream base it carries (helpers/MeconVersion.h)
//   get mecon.boot                millis() when the store was loaded, the mesh started and setup ended (helpers/BootTimes.h)
//
// All are reads. Pure logic: runs in the native unit tests.

#include <stdio.h>
#include <string.h>
#include "CliRegistry.h"
#include "../MemStats.h"
#include "../HeapFloor.h"
#include "../MeconVersion.h"
#include "../BootTimes.h"

namespace Cli {

inline const char* transportName(uint8_t t) {
  switch (t) {
    case T_USB: return "usb";
    case T_BLE: return "ble";
    case T_TCP: return "tcp";
    case T_MQTT: return "mqtt";
    case T_LORA: return "lora";
    case T_BUTTON: return "button";
    case T_NETCON: return "netcon";
  }
  return "none";
}

inline const char* authName(uint8_t a) {
  switch (a) {
    case A_CABLE: return "cable";
    case A_PAIRED_PIN: return "paired-pin";
    case A_PAIRED_DEFAULT_PIN: return "paired-default-pin";
    case A_TLS_VERIFIED: return "tls-verified";
    case A_TLS_UNVERIFIED: return "tls-unverified";
    case A_ADMIN: return "admin";
    case A_ADMIN_DEFAULT_PW: return "admin-default-password";
    case A_GUEST: return "guest";
    case A_CONTACT_FLAG: return "contact-flag";
    case A_BUTTON: return "button";
    case A_NETCON_ADMIN: return "netcon-admin";
    case A_SIGNED: return "signed";
  }
  return "none";
}

inline const char* principalName(uint8_t p) {
  switch (p) {
    case P_CABLE: return "cable";
    case P_PAIRED: return "paired";
    case P_ADMIN: return "remote-admin";
    case P_ADMIN_DEFAULT: return "remote-admin-default-password";
    case P_GUEST: return "guest";
    case P_CONTACT: return "remote-cli";
    case P_MQTT: return "mqtt";
    case P_MQTT_UNVERIFIED: return "mqtt-unverified";
    case P_BUTTON: return "button";
    case P_NETCON_ADMIN: return "ethernet-admin";
    case P_SIGNED: return "signed";
  }
  return "untrusted";
}

inline const char* className(uint8_t c) {
  switch (c) {
    case C_READ: return "read";
    case C_WRITE: return "write";
    case C_SECRET: return "secret";
    case C_POSTURE: return "posture";
    case C_DESTRUCTIVE: return "destructive";
    case C_REBOOT: return "reboot";
    case C_PRIVATE: return "private";
    case C_UPDATE: return "update";
  }
  return "?";
}

inline const char* reasonName(uint8_t r) {
  switch (r) {
    case R_ALLOW: return "allowed";
    case R_NOT_PERMITTED: return "not-permitted";
    case R_UNTRUSTED: return "untrusted-transport";
    case R_REMOTE_LIMITED: return "remote-limited";
    case R_HARDENED: return "hardened";
    case R_CLI_OFF: return "remote-cli-off";
    case R_DEFAULT_PW: return "default-password";
    case R_KEY_HARDENED: return "key-hardened";
    case R_LOCAL_ONLY: return "local-only";
    case R_MQTT_UNVERIFIED: return "broker-not-verified";
    case R_MQTT_READONLY: return "broker-read-only";
    case R_NETCON_OFF: return "ethernet-console-off";
    case R_ENROLLED: return "enrolled-unsigned";
    case R_NO_UNLOCK_SECRET: return "no-unlock-secret";
  }
  return "?";
}

// The reply to `get mecon.mem`. Split out so the native unit tests can feed it a measured heap.
inline void formatMem(const MemStats& m, const HeapFloor& floor, char* reply, size_t max) {
  if (!m.known) snprintf(reply, max, "> heap: not measurable on this platform");
  else {
    int n = snprintf(reply, max, "> heap free=%lu largest=%lu min_free=%lu heap_low=%d floor=%lu", (unsigned long)m.free,
                     (unsigned long)m.largest, (unsigned long)m.min_free, floor.low() ? 1 : 0,
                     (unsigned long)floor.floorBytes());
    if (m.psram && n > 0 && (size_t)n < max) n += snprintf(reply + n, max - n, " psram_free=%lu", (unsigned long)m.psram);
    if (m.loop_stack_free && n > 0 && (size_t)n < max)
      snprintf(reply + n, max - n, " loop_stack_free=%lu", (unsigned long)m.loop_stack_free);
  }
}

// Returns true when `cmd` is one of the MECON commands and `reply` holds the answer.
inline bool handleMecon(const char* cmd, const Context& ctx, uint8_t role, char* reply, size_t max) {
  if (strcmp(cmd, "get mecon.mem") == 0) {
    formatMem(memStats(), heapFloor(), reply, max);
    return true;
  }
  if (strcmp(cmd, "get mecon.boot") == 0) {
    mecon::boot::format(reply, max);
    return true;
  }
  if (strcmp(cmd, "get mecon.version") == 0) {
    snprintf(reply, max, "> %s (describe %s, upstream %s)", mecon::versionFull(), MECON_VERSION, MECON_UPSTREAM_BASE);
    return true;
  }
  if (strcmp(cmd, "get mecon.cli.who") == 0) {
    snprintf(reply, max, "> transport=%s auth=%s remote=%d posture=%s principal=%s%s", transportName(ctx.transport),
             authName(ctx.auth), ctx.remote ? 1 : 0, ctx.posture ? "hardened" : "normal",
             principalName(principalOf(ctx)), ctx.mgmt ? " mgmt=on" : "");
    return true;
  }
  static const char CHECK[] = "get mecon.cli.check ";
  if (memcmp(cmd, CHECK, sizeof(CHECK) - 1) == 0) {
    const char* line = cmd + sizeof(CHECK) - 1;
    while (*line == ' ') line++;
    const Resolved r = resolve(line, role);
    const Verdict v = decide(ctx, r.cls, r.flags);
    snprintf(reply, max, "> class=%s %s", className(r.cls), v.allow ? "allowed" : reasonName(v.reason));
    return true;
  }
  return false;
}

}  // namespace Cli
