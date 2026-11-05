#pragma once

// What the Repeater and Room server's command handlers need to know about the caller that the context they build does
// not carry on its own (docs/contract/AUTHORITY.md section 9). The Companion gets this from one handler that owns the
// device interface; the servers build their contexts in four places (the front door in Posture.h, CommonCLI's own gate
// and its `mecon.` handler, and the Ethernet console), so the two facts live here, written by the one place that owns
// the Deployment record (Iface::DeployAuth) and read by every place that builds a context.
//
//   enrolled     the device holds a Deployment record, healthy or corrupt: an unsigned caller changes nothing.
//   signed_line  the console line being run is the inner request of a signed interface request that passed every check
//                (AUTHORITY.md section 7): the cable's line then runs as the signed principal. Set for the duration of
//                that one request, never otherwise.
//   net_line     the console line being run came over the broker: its handlers see a timestamp of 0, and the table sees
//                this session's context, never the cable's. Set for the duration of that one request or line.
//
// The default of both is false, which is a standalone device. Pure logic: runs in the native unit tests.

#include "CliPolicy.h"

namespace Cli {

struct ServerCaller {
  bool enrolled = false;
  bool signed_line = false;
  const Context* net_line = nullptr;
  // Called once after the file system has been formatted (the `erase` of the local console): the Deployment record's
  // owner clears the provisioned marker and its state in memory (AUTHORITY.md section 11).
  void (*on_format)(void* user) = nullptr;
  void* on_format_user = nullptr;
  // Whether the console link holds a valid local unlock right now (AUTHORITY.md section 10). Asked each time a context is
  // built, so the 10 minutes lapse on their own; null: never.
  bool (*unlocked)(void* user) = nullptr;
  void* unlocked_user = nullptr;
  // Whether the record holds a local unlock secret, asked each time a context is built (it changes with a signed request).
  bool (*has_secret)(void* user) = nullptr;
  void* has_secret_user = nullptr;
};

inline ServerCaller& serverCaller() {
  static ServerCaller s;
  return s;
}

// The context as the table must see it. Only a console line becomes the signed principal, and only when it was
// verified; every other caller keeps what its transport proved, and the enrolled flag is applied to all of them.
inline Context withServerCaller(Context c) {
  const ServerCaller& s = serverCaller();
  const bool console = !c.remote && c.transport == T_USB && c.auth == A_CABLE;
  if (console && s.net_line) {
    const uint8_t posture = c.posture;
    c = *s.net_line;
    c.posture = posture;
  }
  c.enrolled = s.enrolled;
  c.has_unlock_secret = s.has_secret && s.has_secret(s.has_secret_user);
  c.unlocked = s.unlocked && !s.signed_line && !s.net_line && console && s.unlocked(s.unlocked_user);
  if (s.signed_line && console) c.auth = A_SIGNED;
  return c;
}

// One request or line that came over the broker: while it runs, the server's handlers see the session, not the cable.
struct NetLineScope {
  const Context* was;
  explicit NetLineScope(const Context& ctx) : was(serverCaller().net_line) { serverCaller().net_line = &ctx; }
  ~NetLineScope() { serverCaller().net_line = was; }
};

// After a successful format of the file system.
inline void serverFormatted() {
  const ServerCaller& s = serverCaller();
  if (s.on_format) s.on_format(s.on_format_user);
}

}  // namespace Cli
