#pragma once

// The command registry of the command-line engine (docs/CLI.md): which class a command line belongs to.
//
// The registry decides nothing about privilege; it only says what kind of command this is. CliPolicy.h turns
// (caller, posture, class) into allow or refuse. A command nobody registered is C_WRITE: it fails closed for
// every caller that is limited to reads (a flagged contact, Hardened remote admin, a broker that is not flagged
// for management) and runs for the callers that may write.
//
// Upstream commands keep their names, syntax and replies; they are classified here, not changed. Entries are
// matched in order, the first match wins, so a specific entry sits above the generic "get " read.

#include <stdint.h>
#include <string.h>
#include "CliPolicy.h"

namespace Cli {

enum Role : uint8_t {
  ROLE_COMPANION = 1,
  ROLE_REPEATER = 2     // repeater, room server and sensor share one command set
};

enum Match : uint8_t {
  M_EXACT,    // the whole line
  M_PREFIX,   // the line starts with the text
  M_WORD      // the whole line, or the text followed by a space
};

struct Entry {
  const char* text;
  uint8_t match;
  uint8_t roles;        // Role bits
  uint8_t cls;
  uint8_t flags;
  uint8_t what;         // Posture::What recorded when this command is refused (W_NONE: from the reason)
  const char* canned;   // a reply the engine gives for an allowed command (secrets that are write-only)
  const char* msg_hardened;   // reply for R_KEY_HARDENED
  const char* msg_local;      // reply for R_LOCAL_ONLY
};

// What the registry knows about one command line.
struct Resolved {
  uint8_t cls = C_WRITE;
  uint8_t flags = 0;
  uint8_t what = Posture::W_NONE;
  const char* canned = nullptr;
  const char* msg_hardened = nullptr;
  const char* msg_local = nullptr;
};

inline bool matches(const Entry& e, const char* cmd) {
  const size_t n = strlen(e.text);
  switch (e.match) {
    case M_EXACT: return strcmp(cmd, e.text) == 0;
    case M_PREFIX: return memcmp(cmd, e.text, n) == 0;
    case M_WORD: return memcmp(cmd, e.text, n) == 0 && (cmd[n] == 0 || cmd[n] == ' ');
  }
  return false;
}

inline const Entry* entries(size_t& count) {
  static const Entry E[] = {
    // text                    match      roles                          class          flags         what                  canned                        msg_hardened                                        msg_local
    {"get posture",            M_EXACT,   ROLE_COMPANION | ROLE_REPEATER, C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"get audit",              M_WORD,    ROLE_COMPANION | ROLE_REPEATER, C_POSTURE,     0,            Posture::W_AUDIT,     nullptr,                      nullptr,                                            nullptr},
    {"get connectivity.mode",  M_EXACT,   ROLE_COMPANION,                 C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"set connectivity.mode",  M_WORD,    ROLE_COMPANION,                 C_POSTURE,     0,            Posture::W_CONNMODE,  nullptr,                      nullptr,                                            nullptr},
    {"get prv.key",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET,      0,            Posture::W_KEY_EXPORT, nullptr,                     "ERR: key export is refused in hardened posture",   nullptr},
    {"set prv.key",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET,      F_QUIET,      Posture::W_KEY_IMPORT, nullptr,                     "ERR: key import is refused in hardened posture",   "ERR: prv.key can only be set from the local console"},
    {"get wifi.pwd",           M_EXACT,   ROLE_COMPANION,                 C_READ,        0,            Posture::W_NONE,      "> wifi.pwd is write-only",   nullptr,                                            nullptr},
    {"erase",                  M_EXACT,   ROLE_REPEATER,                  C_DESTRUCTIVE, 0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    // powering off needs someone on site to recover the device: no remote caller, broker or not
    {"poweroff",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_DESTRUCTIVE, 0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"shutdown",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_DESTRUCTIVE, 0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    // reads
    {"get ",                   M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"ver",                    M_EXACT,   ROLE_COMPANION | ROLE_REPEATER, C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"board",                  M_EXACT,   ROLE_COMPANION | ROLE_REPEATER, C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"clock",                  M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"neighbors",              M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"power",                  M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"powersaving",            M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"region",                 M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"region get ",            M_PREFIX,  ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"sensor get ",            M_PREFIX,  ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"sensor list",            M_PREFIX,  ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"gps",                    M_EXACT,   ROLE_REPEATER,                  C_READ,        0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
    {"reboot",                 M_EXACT,   ROLE_COMPANION | ROLE_REPEATER, C_REBOOT,      0,            Posture::W_NONE,      nullptr,                      nullptr,                                            nullptr},
  };
  count = sizeof(E) / sizeof(E[0]);
  return E;
}

// "set posture ...": raising to hardened is a local-trusted act, everything else (lowering, and any argument that
// is not "hardened") is for a cable. The match is the one the posture handler uses: the words, then spaces.
inline bool isSetPosture(const char* cmd, const char*& arg) {
  if (memcmp(cmd, "set posture", 11) != 0 || (cmd[11] != 0 && cmd[11] != ' ')) return false;
  arg = cmd + 11;
  while (*arg == ' ') arg++;
  return true;
}

// "set mqtt.profile <n> manage ...": the flag that lets a broker manage the device. Only a local caller sets it.
inline bool isMgmtFlag(const char* cmd) {
  static const char P[] = "set mqtt.profile ";
  if (memcmp(cmd, P, sizeof(P) - 1) != 0) return false;
  const char* q = cmd + sizeof(P) - 1;
  if (*q < '0' || *q > '9') return false;
  q++;
  while (*q == ' ') q++;
  return memcmp(q, "manage", 6) == 0 && (q[6] == 0 || q[6] == ' ');
}

inline Resolved resolve(const char* cmd, uint8_t role) {
  Resolved r;
  const char* arg;
  if (isSetPosture(cmd, arg)) {
    r.cls = C_POSTURE;
    r.what = Posture::W_POSTURE;
    if (strcmp(arg, "hardened") != 0) r.flags = F_CABLE_ONLY;
    return r;
  }
  if (role == ROLE_COMPANION && isMgmtFlag(cmd)) {
    r.cls = C_POSTURE;
    r.what = Posture::W_MGMT_FLAG;
    return r;
  }
  size_t n;
  const Entry* e = entries(n);
  for (size_t i = 0; i < n; i++) {
    if ((e[i].roles & role) && matches(e[i], cmd)) {
      r.cls = e[i].cls;
      r.flags = e[i].flags;
      r.what = e[i].what;
      r.canned = e[i].canned;
      r.msg_hardened = e[i].msg_hardened;
      r.msg_local = e[i].msg_local;
      return r;
    }
  }
  return r;   // C_WRITE
}

inline uint8_t classify(const char* cmd, uint8_t role) { return resolve(cmd, role).cls; }

// Companion frames that are not command lines but carry the same classes: the app's commands that run the CLI,
// move the private key, set the BLE PIN or factory-reset the device.
enum Frame : uint8_t { FR_NONE = 0, FR_CLI, FR_KEY_EXPORT, FR_KEY_IMPORT, FR_SET_PIN, FR_FACTORY_RESET };

inline Resolved resolveFrame(uint8_t f) {
  Resolved r;
  switch (f) {
    case FR_CLI: r.cls = C_READ; r.what = Posture::W_CLI; break;   // may reach the CLI at all; the line is then classified
    case FR_KEY_EXPORT: r.cls = C_SECRET; r.what = Posture::W_KEY_EXPORT; break;
    case FR_KEY_IMPORT: r.cls = C_SECRET; r.what = Posture::W_KEY_IMPORT; break;
    case FR_SET_PIN: r.cls = C_WRITE; r.what = Posture::W_SET_PIN; break;
    case FR_FACTORY_RESET: r.cls = C_DESTRUCTIVE; r.what = Posture::W_FACTORY_RESET; break;
    default: r.cls = C_DESTRUCTIVE; break;   // unknown frame ids are not gated here; refuse if asked
  }
  return r;
}

}  // namespace Cli
