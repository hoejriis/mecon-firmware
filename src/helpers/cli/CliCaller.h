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
//
// The default of both is false, which is a standalone device. Pure logic: runs in the native unit tests.

#include "CliPolicy.h"

namespace Cli {

struct ServerCaller {
  bool enrolled = false;
  bool signed_line = false;
  // Called once after the file system has been formatted (the `erase` of the local console): the Deployment record's
  // owner clears the provisioned marker and its state in memory (AUTHORITY.md section 11).
  void (*on_format)(void* user) = nullptr;
  void* on_format_user = nullptr;
};

inline ServerCaller& serverCaller() {
  static ServerCaller s;
  return s;
}

// The context as the table must see it. Only the cable becomes the signed principal, and only on a line that was
// verified; every other caller keeps what its transport proved, and the enrolled flag is applied to all of them.
inline Context withServerCaller(Context c) {
  const ServerCaller& s = serverCaller();
  c.enrolled = s.enrolled;
  if (s.signed_line && !c.remote && c.transport == T_USB && c.auth == A_CABLE) c.auth = A_SIGNED;
  return c;
}

// After a successful format of the file system.
inline void serverFormatted() {
  const ServerCaller& s = serverCaller();
  if (s.on_format) s.on_format(s.on_format_user);
}

}  // namespace Cli
