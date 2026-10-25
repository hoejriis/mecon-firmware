#pragma once

// The one privilege table of the command-line engine (docs/CLI.md).
//
// Every command is registered with a class (CliRegistry.h); every caller arrives with a context: the transport it
// came over, how it was authenticated, whether it is remote, and the device's posture. The table below maps
// (caller, posture, class) to "allowed" or to the reason it is refused. No other code decides who may run what:
// the adapters (USB, BLE, LoRa, MQTT) only build the context, and the handlers only run commands that were allowed.
//
// A remote caller never inherits a local caller's privileges: a LoRa or MQTT caller is a different row from the
// cable, whatever it sent. A context that is inconsistent (a remote flag that does not match the transport, an
// authentication that does not belong to the transport) is the "untrusted" row, which refuses everything.
//
// Pure logic: no file system, no clock, no radio, so it runs in the native unit tests.

#include <stdint.h>
#include <string.h>
#include "../AuditRing.h"

namespace Cli {

enum Transport : uint8_t {
  T_NONE = 0,   // not set: fails closed
  T_USB,        // USB / UART serial (the Companion frames and the rescue console)
  T_BLE,        // Bluetooth LE
  T_TCP,        // the Companion TCP server (Wi-Fi, Ethernet), and any local transport that did not say what it is
  T_MQTT,       // a request from the broker
  T_LORA,       // over the air: remote admin login (repeater, room server) or a flagged contact (Companion)
  T_BUTTON,     // the device button: not a command line, it only reaches the connectivity-mode switch
  T_NETCON      // the Ethernet TCP console of a repeater or room server (a plain-text line protocol on the network)
};

enum Auth : uint8_t {
  A_NONE = 0,
  A_CABLE,                // a cable is attached: possession
  A_PAIRED_PIN,           // BLE paired with a PIN of the device's own
  A_PAIRED_DEFAULT_PIN,   // BLE paired with a published default PIN: authenticates nobody
  A_TLS_VERIFIED,         // MQTT over TLS, broker certificate matched its pinned anchor
  A_TLS_UNVERIFIED,       // MQTT on plain TCP, or on TLS without a pinned anchor
  A_ADMIN,                // admin login with a password that has been set
  A_ADMIN_DEFAULT_PW,     // admin login while the admin password is still the stock default
  A_GUEST,                // guest login
  A_CONTACT_FLAG,         // a Companion contact flagged for remote CLI
  A_BUTTON,               // the device button
  A_NETCON_ADMIN,         // `login <admin password>` on one Ethernet console connection, password set, Normal posture
  A_SIGNED                // a request signed by a current member of the Deployment authority set (AUTHORITY.md section 7),
                          // on any wire transport; the transport still decides Context.remote
};

enum Class : uint8_t {
  C_READ = 0,       // changes nothing
  C_WRITE,          // changes settings or transmits (also the default for a command nobody registered)
  C_SECRET,         // private-key material
  C_POSTURE,        // the security posture, the audit log, the connectivity mode, the broker-management flag
  C_DESTRUCTIVE,    // erases or factory-resets the device
  C_REBOOT,
  C_COUNT
};

enum Flags : uint8_t {
  F_CABLE_ONLY = 1,   // lowers protection: only the cable may, whatever the class cell says for the other local callers
  F_QUIET = 2         // registry only: a "local console only" refusal is explained but not audited
};

enum Reason : uint8_t {
  R_ALLOW = 0,          // (not R_OK: <unistd.h> defines that)
  R_NOT_PERMITTED,      // "ERR: not permitted"
  R_UNTRUSTED,          // the transport does not authenticate the peer: a Companion frame answers ERR_CODE_NOT_PERMITTED
  R_REMOTE_LIMITED,     // a flagged contact may read and reboot only
  R_HARDENED,           // remote admin in Hardened: reads and reboot only
  R_CLI_OFF,            // Hardened turns the Companion remote CLI off
  R_DEFAULT_PW,         // remote admin is off while the admin password is the stock default
  R_KEY_HARDENED,       // Hardened refuses private-key export and import on every transport
  R_LOCAL_ONLY,         // the command exists only on the local console
  R_MQTT_UNVERIFIED,    // the broker session is not TLS-verified
  R_MQTT_READONLY,      // Hardened and the broker profile is not flagged for management
  R_NEEDS_FLAG,         // table-internal: allowed when the broker profile is flagged for management
  R_NETCON_OFF,         // the Ethernet console is off in Hardened
  R_ENROLLED            // the device holds a Deployment record: an unsigned caller may not change it (AUTHORITY.md section 9)
};

// Who the context resolves to: one row of the table.
enum Principal : uint8_t {
  P_UNTRUSTED = 0,
  P_CABLE,
  P_PAIRED,           // BLE with a PIN of the device's own
  P_ADMIN,
  P_ADMIN_DEFAULT,
  P_GUEST,
  P_CONTACT,
  P_MQTT,             // TLS-verified broker session
  P_MQTT_UNVERIFIED,
  P_BUTTON,
  P_NETCON_ADMIN,     // Ethernet console, logged in with the admin password
  P_SIGNED,           // a request that passed the signed-request checks (AUTHORITY.md section 9), on any wire transport
  P_COUNT
};

struct Context {
  uint8_t transport = T_NONE;
  uint8_t auth = A_NONE;
  bool remote = false;     // over the air or over the broker
  uint8_t posture = 0;     // Posture::NORMAL (0) or anything else = Hardened
  bool mgmt = false;       // MQTT: the broker profile in use is flagged for management
  // The device holds a Deployment record (healthy or corrupt). Set by the one place that gates a command line
  // (MyMesh::handleCommand) and for the factory-reset frame; a context built anywhere else leaves it false.
  bool enrolled = false;

  Context() { }
  Context(uint8_t t, uint8_t a, uint8_t posture_, bool mgmt_ = false)
      : transport(t), auth(a), remote(t == T_LORA || t == T_MQTT), posture(posture_), mgmt(mgmt_) { }
};

inline Principal principalOf(const Context& c) {
  const bool wire_remote = c.transport == T_LORA || c.transport == T_MQTT;
  if (c.remote != wire_remote) return P_UNTRUSTED;   // a local-looking remote caller, or the reverse: fail closed
  if (c.auth == A_SIGNED) {   // valid on every wire transport; the button and an unset transport are not one
    switch (c.transport) {
      case T_USB: case T_BLE: case T_TCP: case T_MQTT: case T_LORA: case T_NETCON: return P_SIGNED;
      default: return P_UNTRUSTED;
    }
  }
  switch (c.transport) {
    case T_USB: return c.auth == A_CABLE ? P_CABLE : P_UNTRUSTED;
    case T_BLE: return c.auth == A_PAIRED_PIN ? P_PAIRED : P_UNTRUSTED;
    case T_LORA:
      switch (c.auth) {
        case A_ADMIN: return P_ADMIN;
        case A_ADMIN_DEFAULT_PW: return P_ADMIN_DEFAULT;
        case A_GUEST: return P_GUEST;
        case A_CONTACT_FLAG: return P_CONTACT;
        default: return P_UNTRUSTED;
      }
    case T_MQTT:
      switch (c.auth) {
        case A_TLS_VERIFIED: return P_MQTT;
        case A_TLS_UNVERIFIED: return P_MQTT_UNVERIFIED;
        default: return P_UNTRUSTED;
      }
    case T_BUTTON: return c.auth == A_BUTTON ? P_BUTTON : P_UNTRUSTED;
    case T_NETCON: return c.auth == A_NETCON_ADMIN ? P_NETCON_ADMIN : P_UNTRUSTED;   // no login: the untrusted row
    default: return P_UNTRUSTED;   // T_TCP, T_NONE, anything else
  }
}

// The audit "who" for a principal.
inline uint8_t auditCaller(Principal p) {
  switch (p) {
    case P_CABLE: return Posture::CALLER_CABLE;
    case P_PAIRED: return Posture::CALLER_BLE;
    case P_ADMIN: case P_ADMIN_DEFAULT: case P_GUEST: return Posture::CALLER_REMOTE_ADMIN;
    case P_CONTACT: return Posture::CALLER_REMOTE_CLI;
    case P_MQTT: case P_MQTT_UNVERIFIED: return Posture::CALLER_MQTT;
    case P_BUTTON: return Posture::CALLER_BUTTON;
    default: return Posture::CALLER_NET;
  }
}

// ---- the table
// Rows are principals, in the order of the Principal enum; per principal one row for Normal and one for Hardened;
// columns are the classes, in the order of the Class enum. ALLOW means allowed; anything else is the reason for the
// refusal. A change to who may do what is a change to a cell here and to nothing else.
#define CLI_ROW(r, w, s, p, d, b) { R_##r, R_##w, R_##s, R_##p, R_##d, R_##b }
inline Reason tableCell(Principal p, bool hardened, uint8_t cls) {
  static const uint8_t T[P_COUNT][2][C_COUNT] = {
    //                            READ            WRITE           SECRET          POSTURE         DESTRUCTIVE     REBOOT
    /* untrusted      N */ { CLI_ROW(UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED),
    /* untrusted      H */   CLI_ROW(UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED) },
    /* cable          N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW),
    /* cable          H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           ALLOW,           ALLOW) },
    /* paired BLE     N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW),
    /* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           NOT_PERMITTED,   ALLOW) },
    /* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW),
    /* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   HARDENED,        ALLOW) },
    /* admin, default N */ { CLI_ROW(DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW),
    /* admin, default H */   CLI_ROW(DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW) },
    /* guest          N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED),
    /* guest          H */   CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED) },
    /* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW),
    /* flagged contact H */   CLI_ROW(CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF) },
    /* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW),
    /* MQTT verified  H */   CLI_ROW(ALLOW,           NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) },
    /* MQTT unverified N */ { CLI_ROW(MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED),
    /* MQTT unverified H */   CLI_ROW(MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED) },
    /* button         N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   NOT_PERMITTED),
    /* button         H */   CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   NOT_PERMITTED) },
    /* Ethernet admin N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW),
    /* Ethernet admin H */   CLI_ROW(NETCON_OFF,      NETCON_OFF,      NETCON_OFF,      NETCON_OFF,      NETCON_OFF,      NETCON_OFF) },
    // signed: custody (secret) comes with its own change; no remote erase, whatever the signature (AUTHORITY.md section 11)
    /* signed         N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   ALLOW),
    /* signed         H */   CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   ALLOW) },
  };
  if (p >= P_COUNT || cls >= C_COUNT) return R_NOT_PERMITTED;
  return (Reason)T[p][hardened ? 1 : 0][cls];
}
#undef CLI_ROW

struct Verdict {
  bool allow;
  uint8_t reason;   // Reason; R_ALLOW when allowed
  uint8_t caller;   // Posture::Caller for the audit entry of a refusal
};

inline Verdict decide(const Context& c, uint8_t cls, uint8_t flags = 0) {
  const Principal p = principalOf(c);
  Reason r = tableCell(p, c.posture != 0, cls);
  if (r == R_NEEDS_FLAG) r = c.mgmt ? R_ALLOW : R_MQTT_READONLY;
  if (r == R_ALLOW && (flags & F_CABLE_ONLY) && p != P_CABLE) r = R_NOT_PERMITTED;   // lowering protection: a cable
  // Once the device holds a Deployment record, only a member's signature changes it, or someone with the cable for the
  // recovery operations (AUTHORITY.md sections 9 and 11). So an unsigned caller keeps reads and reboot, and every other
  // class is refused whatever the table says for this transport; only the cable keeps the destructive class (erase,
  // factory reset). Fails closed: anything that is not a read or reboot counts, so a new class is refused until decided.
  if (r == R_ALLOW && c.enrolled && p != P_SIGNED && cls != C_READ && cls != C_REBOOT &&
      !(p == P_CABLE && cls == C_DESTRUCTIVE))
    r = R_ENROLLED;
  Verdict v;
  v.allow = r == R_ALLOW;
  v.reason = r;
  if (p == P_SIGNED) {   // audited as the transport it arrived on
    v.caller = c.transport == T_USB ? Posture::CALLER_CABLE : c.transport == T_BLE ? Posture::CALLER_BLE
             : c.transport == T_MQTT ? Posture::CALLER_MQTT : c.transport == T_LORA ? Posture::CALLER_REMOTE_ADMIN
             : Posture::CALLER_NET;
    return v;
  }
  // An untrusted BLE link (default PIN) is audited as the BLE it is, not as "net"; Wi-Fi / Ethernet TCP stay "net".
  v.caller = (p == P_UNTRUSTED && c.transport == T_BLE) ? Posture::CALLER_BLE : auditCaller(p);
  return v;
}

// A context for the numeric callers the posture and connectivity-mode code has always used (Posture::Caller).
inline Context contextForCaller(uint8_t caller, uint8_t posture) {
  switch (caller) {
    case Posture::CALLER_CABLE: return Context(T_USB, A_CABLE, posture);
    case Posture::CALLER_BLE: return Context(T_BLE, A_PAIRED_PIN, posture);
    case Posture::CALLER_NET: return Context(T_TCP, A_NONE, posture);
    case Posture::CALLER_REMOTE_ADMIN: return Context(T_LORA, A_ADMIN, posture);
    case Posture::CALLER_REMOTE_CLI: return Context(T_LORA, A_CONTACT_FLAG, posture);
    case Posture::CALLER_MQTT: return Context(T_MQTT, A_TLS_VERIFIED, posture);
    case Posture::CALLER_BUTTON: return Context(T_BUTTON, A_BUTTON, posture);
  }
  return Context(T_NONE, A_NONE, posture);
}

}  // namespace Cli
