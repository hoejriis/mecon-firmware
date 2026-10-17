#pragma once

// The Ethernet TCP console of the repeater and room server (docs/CLI.md): a plain-text line protocol on the network,
// so it is not the cable. Its lines used to reach the handler with sender timestamp 0, which the handler reads as the
// local console, and any host that could open the port ran `get prv.key` and `erase`.
//
// A connection starts as the *untrusted* caller of the table (transport T_NETCON, no authentication): every line is
// refused and audited, except `login`. `login <admin password>` turns the connection into the Ethernet-admin row,
// which is the remote admin of the table (reads, writes and reboot; the private key, `erase`, `poweroff`, posture
// and audit changes stay on the cable). The login needs
//   - Normal posture (Hardened turns the Ethernet console off entirely, as it does the Companion's remote CLI), and
//   - an admin password that has been set (the stock default and an empty one open nothing).
// A wrong password is audited (`login-fail`, caller `net`; the attempt itself is never stored or echoed). Five wrong
// attempts lock logins for a minute, across connections. A session ends with the connection (the transport calls
// `endSession()`), with `logout`, or as soon as the posture is no longer Normal or the password is the default again.
//
// Pure logic: runs in the native unit tests, and the same code is compiled into the RAK4631 `_ethernet` builds and
// the simulated board's Ethernet console stand-in.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CliEngine.h"
#include "../Posture.h"

namespace Cli {

struct NetConsole {
  static const uint8_t MAX_FAILS = 5;
  static const uint32_t LOCKOUT_SECS = 60;

  bool authed = false;
  uint8_t fails = 0;
  uint32_t locked_until = 0;

  void endSession() { authed = false; }   // the failure count and the lockout outlive a connection on purpose
};

inline bool netConsolePasswordMatches(const char* given, const char* want) {
  size_t gl = strlen(given), wl = strlen(want);
  uint8_t diff = gl == wl ? 0 : 1;
  for (size_t i = 0; i < gl; i++) diff |= (uint8_t)(given[i] ^ want[i % (wl ? wl : 1)]);   // no early exit
  return diff == 0 && wl > 0;
}

// One line from the Ethernet console. Returns true when the engine answered it (`reply` holds the answer: a login
// result, or a refusal that has been audited); false when the line may run, and the caller hands it to the role's
// handler as a remote admin (a non-zero sender timestamp), never as the local console.
inline bool netConsoleLine(NetConsole& nc, const char* line, const char* admin_pw, const char* build_default,
                           uint8_t posture, uint32_t now, char* reply, size_t max, uint8_t role = ROLE_REPEATER) {
  while (*line == ' ') line++;
  if (nc.authed && (posture != 0 || Posture::isDefaultAdminPassword(admin_pw, build_default))) nc.authed = false;

  if (strcmp(line, "logout") == 0) {
    nc.authed = false;
    snprintf(reply, max, "OK - logged out");
    return true;
  }
  if (memcmp(line, "login ", 6) == 0 || strcmp(line, "login") == 0) {
    const char* given = line[5] == ' ' ? line + 6 : "";
    if (posture != 0) {
      Posture::recordRefusal(now, Posture::CALLER_NET, Posture::W_CLI);
      snprintf(reply, max, "%s", refusalText(R_NETCON_OFF, Resolved()));
      return true;
    }
    if (nc.locked_until != 0 && now < nc.locked_until) {
      Posture::audit().add(now, Posture::EV_LOGIN_FAILED, Posture::CALLER_NET, Posture::W_BAD_PASSWORD);
      snprintf(reply, max, "ERR: too many failed logins, try again later");
      return true;
    }
    const bool is_default = Posture::isDefaultAdminPassword(admin_pw, build_default);
    const bool match = netConsolePasswordMatches(given, admin_pw);
    if (is_default || !match) {
      Posture::audit().add(now, Posture::EV_LOGIN_FAILED, Posture::CALLER_NET,
                           is_default ? Posture::W_DEFAULT_PASSWORD : Posture::W_BAD_PASSWORD);
      if (++nc.fails >= NetConsole::MAX_FAILS) { nc.fails = 0; nc.locked_until = now + NetConsole::LOCKOUT_SECS; }
      if (is_default) snprintf(reply, max, "ERR: the Ethernet console is off until the admin password is set on the local console");
      else snprintf(reply, max, "ERR: wrong password");
      return true;
    }
    nc.fails = 0;
    nc.locked_until = 0;
    nc.authed = true;
    snprintf(reply, max, "OK - logged in");
    return true;
  }

  const Context ctx(T_NETCON, nc.authed ? A_NETCON_ADMIN : A_NONE, posture);
  return gate(line, ctx, role, now, reply, max);
}

// The whole path of one Ethernet console line, shared by the repeater and room server mains: gate it, and run what is
// allowed through the role's handler as a remote admin (a non-zero sender timestamp: the handler's "local console only"
// branches, which key off timestamp 0, stay closed). `own` is the transport's own command (`eth.status`), tried
// after the gate so an unauthenticated peer does not get it either.
template <class MeshT>
inline void netConsoleRun(NetConsole& nc, MeshT& mesh, const char* build_default, uint32_t now, char* line, char* reply,
                          size_t max, bool (*own)(const char*, char*) = nullptr, uint8_t role = ROLE_REPEATER) {
  auto* prefs = mesh.getNodePrefs();
  if (netConsoleLine(nc, line, prefs->password, build_default, prefs->posture, now, reply, max, role)) return;
  if (own && own(line, reply)) return;
  mesh.handleCommand(now != 0 ? now : 1, line, reply);
}

}  // namespace Cli
