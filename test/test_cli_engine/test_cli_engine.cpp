// The command-line engine (src/helpers/cli/, docs/CLI.md): the privilege table cell by cell, the registry's
// classification, the gate's replies and audit entries, and the MQTT request/response framing.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/Posture.h"
#include "helpers/cli/CliCaller.h"
#include "helpers/cli/CliEngine.h"
#include "helpers/cli/NetConsole.h"
#include "helpers/connmode/ConnMode.h"

using namespace Cli;

namespace {

// ---- the expected table, written out independently of CliPolicy.h
// One character per class, in the order READ WRITE SECRET POSTURE DESTRUCTIVE REBOOT:
//   .  allowed          U  untrusted transport      P  not permitted          L  flagged contact limited to reads and reboot
//   H  hardened remote  O  remote CLI off           D  admin password is default
//   K  key refused in Hardened      Y  local console only      V  broker session not verified
//   N  Ethernet console off in Hardened
//   F  allowed when the broker profile is flagged for management, else R (read-only in Hardened)
struct Expect {
  Principal p;
  const char* name;
  const char* normal;
  const char* hardened;
};
const Expect EXPECT_TABLE[] = {
  {P_UNTRUSTED,       "untrusted",        "UUUUUUU", "UUUUUUU"},
  {P_CABLE,           "cable",            ".......", "..K...."},
  {P_PAIRED,          "paired BLE",       ".......", "..K.P.."},
  {P_ADMIN,           "remote admin",     ". . Y P Y . P", ". H H P H . P"},
  {P_ADMIN_DEFAULT,   "admin, default pw", "DDDDDDD", "DDDDDDD"},
  {P_GUEST,           "guest",            "PPPPPPP", "PPPPPPP"},
  {P_CONTACT,         "flagged contact",  ".LLPL.P", "OOOOOOO"},
  {P_MQTT,            "MQTT verified",    "..PPP..", ".FPPPF."},
  {P_MQTT_UNVERIFIED, "MQTT unverified",  "VVVVVVV", "VVVVVVV"},
  {P_BUTTON,          "button",           "PPP.PPP", "PPP.PPP"},
  {P_NETCON_ADMIN,    "Ethernet admin",   ". . Y P Y . P", "NNNNNNN"},
  {P_SIGNED,          "signed",           "..P.P..", "..P.P.."},
};

std::string squeeze(const char* s) {
  std::string o;
  for (; *s; s++) if (*s != ' ') o += *s;
  return o;
}

uint8_t expectedReason(char c, bool mgmt) {
  switch (c) {
    case '.': return R_ALLOW;
    case 'U': return R_UNTRUSTED;
    case 'P': return R_NOT_PERMITTED;
    case 'L': return R_REMOTE_LIMITED;
    case 'H': return R_HARDENED;
    case 'O': return R_CLI_OFF;
    case 'D': return R_DEFAULT_PW;
    case 'K': return R_KEY_HARDENED;
    case 'Y': return R_LOCAL_ONLY;
    case 'V': return R_MQTT_UNVERIFIED;
    case 'N': return R_NETCON_OFF;
    case 'R': return R_MQTT_READONLY;
    case 'F': return mgmt ? R_ALLOW : R_MQTT_READONLY;
  }
  ADD_FAILURE() << "bad code " << c;
  return 255;
}

// The context combinations that mean something; any other (transport, authentication, remote) is untrusted.
struct Valid { uint8_t t, a; bool remote; Principal p; };
const Valid VALID[] = {
  {T_USB, A_CABLE, false, P_CABLE},
  {T_BLE, A_PAIRED_PIN, false, P_PAIRED},
  {T_LORA, A_ADMIN, true, P_ADMIN},
  {T_LORA, A_ADMIN_DEFAULT_PW, true, P_ADMIN_DEFAULT},
  {T_LORA, A_GUEST, true, P_GUEST},
  {T_LORA, A_CONTACT_FLAG, true, P_CONTACT},
  {T_MQTT, A_TLS_VERIFIED, true, P_MQTT},
  {T_MQTT, A_TLS_UNVERIFIED, true, P_MQTT_UNVERIFIED},
  {T_BUTTON, A_BUTTON, false, P_BUTTON},
  {T_NETCON, A_NETCON_ADMIN, false, P_NETCON_ADMIN},
  {T_USB, A_SIGNED, false, P_SIGNED},
  {T_BLE, A_SIGNED, false, P_SIGNED},
  {T_TCP, A_SIGNED, false, P_SIGNED},
  {T_MQTT, A_SIGNED, true, P_SIGNED},
  {T_LORA, A_SIGNED, true, P_SIGNED},
  {T_NETCON, A_SIGNED, false, P_SIGNED},
};

Principal expectedPrincipal(uint8_t t, uint8_t a, bool remote) {
  for (const Valid& v : VALID) if (v.t == t && v.a == a && v.remote == remote) return v.p;
  return P_UNTRUSTED;
}

const Expect& rowFor(Principal p) {
  for (const Expect& e : EXPECT_TABLE) if (e.p == p) return e;
  static Expect none = {P_UNTRUSTED, "?", "UUUUUUU", "UUUUUUU"};
  return none;
}

}  // namespace

// ---- the privilege matrix: every transport x authentication x remote x posture x class x flag

struct Cell {
  uint8_t transport, auth;
  bool remote;
};

class PrivilegeMatrix : public ::testing::TestWithParam<Cell> {};

TEST_P(PrivilegeMatrix, EveryCellOfTheTableIsAsWritten) {
  const Cell c = GetParam();
  const Principal want_p = expectedPrincipal(c.transport, c.auth, c.remote);
  const Expect& row = rowFor(want_p);
  for (uint8_t posture : {(uint8_t)0, (uint8_t)1, (uint8_t)2, (uint8_t)255}) {   // anything but Normal is Hardened
    const std::string codes = squeeze(posture == 0 ? row.normal : row.hardened);
    ASSERT_EQ(codes.size(), (size_t)C_COUNT);
    for (bool mgmt : {false, true}) {
      Context ctx(c.transport, c.auth, posture, mgmt);
      ctx.remote = c.remote;   // the constructor derives it from the transport; the matrix also covers a lie
      EXPECT_EQ(principalOf(ctx), want_p);
      for (uint8_t cls = 0; cls < C_COUNT; cls++) {
        SCOPED_TRACE(std::string(row.name) + " transport=" + std::to_string(c.transport) + " auth=" +
                     std::to_string(c.auth) + " remote=" + std::to_string(c.remote) + " posture=" +
                     std::to_string(posture) + " mgmt=" + std::to_string(mgmt) + " class=" + std::to_string(cls));
        const uint8_t want = expectedReason(codes[cls], mgmt);
        const Verdict v = decide(ctx, cls);
        EXPECT_EQ(v.reason, want);
        EXPECT_EQ(v.allow, want == R_ALLOW);
        // "lowers protection" is for a cable: no other caller gets through with the flag, whatever its cell says
        const Verdict lower = decide(ctx, cls, F_CABLE_ONLY);
        EXPECT_EQ(lower.allow, want == R_ALLOW && (want_p == P_CABLE || want_p == P_SIGNED));
        if (want == R_ALLOW && want_p != P_CABLE && want_p != P_SIGNED) EXPECT_EQ(lower.reason, R_NOT_PERMITTED);
        else EXPECT_EQ(lower.reason, want);
      }
    }
  }
}

static std::vector<Cell> allCells() {
  std::vector<Cell> v;
  for (uint8_t t = T_NONE; t <= T_NETCON; t++)
    for (uint8_t a = A_NONE; a <= A_SIGNED; a++)
      for (bool remote : {false, true}) v.push_back({t, a, remote});
  return v;
}
INSTANTIATE_TEST_SUITE_P(AllContexts, PrivilegeMatrix, ::testing::ValuesIn(allCells()));

TEST(PrivilegeProperties, ARemoteCallerNeverInheritsALocalCallersPrivileges) {
  for (Principal p : {P_ADMIN, P_ADMIN_DEFAULT, P_GUEST, P_CONTACT, P_MQTT, P_MQTT_UNVERIFIED, P_NETCON_ADMIN}) {
    for (bool hardened : {false, true}) {
      for (uint8_t cls : {C_SECRET, C_POSTURE, C_DESTRUCTIVE}) {
        EXPECT_NE(tableCell(p, hardened, cls), R_ALLOW) << "principal " << p << " class " << (int)cls << " hardened " << hardened;
      }
    }
  }
}

TEST(PrivilegeProperties, PrivateTelemetryGoesToTheBrokerAndTheDevicesOwnLinksOnly) {
  // The owner's rule, 2026-10-28: "All this should go via the broker to the backend" (location, inventory, screen).
  for (bool hardened : {false, true})
    for (uint8_t p = 0; p < P_COUNT; p++) {
      const bool want = p == P_CABLE || p == P_PAIRED || p == P_MQTT || p == P_SIGNED;
      EXPECT_EQ(tableCell((Principal)p, hardened, C_PRIVATE) == R_ALLOW, want) << "principal " << (int)p << " hardened " << hardened;
    }
}

TEST(PrivilegeProperties, ASignedCallerNeverEraseNorReadsKeysOnAnyTransport) {
  for (uint8_t t : {T_USB, T_BLE, T_TCP, T_MQTT, T_LORA, T_NETCON})
    for (uint8_t posture : {(uint8_t)0, (uint8_t)1}) {
      const Context c(t, A_SIGNED, posture);
      EXPECT_EQ(principalOf(c), P_SIGNED);
      EXPECT_EQ(c.remote, t == T_MQTT || t == T_LORA) << "remote follows the transport, not the signature";
      EXPECT_FALSE(decide(c, C_DESTRUCTIVE).allow);
      EXPECT_FALSE(decide(c, C_SECRET).allow);
      EXPECT_TRUE(decide(c, C_WRITE).allow);
      EXPECT_TRUE(decide(c, C_REBOOT).allow);
      EXPECT_TRUE(decide(c, C_POSTURE, F_CABLE_ONLY).allow) << "a signed request may lower the posture (section 11: signed, or the cable)";
    }
  EXPECT_EQ(principalOf(Context(T_BUTTON, A_SIGNED, 0)), P_UNTRUSTED);
  EXPECT_EQ(principalOf(Context(T_NONE, A_SIGNED, 0)), P_UNTRUSTED);
  Context lie(T_MQTT, A_SIGNED, 0);
  lie.remote = false;                      // a signature does not make a broker request local
  EXPECT_EQ(principalOf(lie), P_UNTRUSTED);
  EXPECT_EQ(decide(Context(T_USB, A_SIGNED, 0), C_WRITE).caller, Posture::CALLER_CABLE);
  EXPECT_EQ(decide(Context(T_MQTT, A_SIGNED, 0), C_WRITE).caller, Posture::CALLER_MQTT);
}

TEST(PrivilegeProperties, UntrustedAndUnverifiedCallersAreRefusedEverything) {
  for (Principal p : {P_UNTRUSTED, P_MQTT_UNVERIFIED, P_ADMIN_DEFAULT, P_GUEST}) {
    for (bool hardened : {false, true})
      for (uint8_t cls = 0; cls < C_COUNT; cls++) EXPECT_NE(tableCell(p, hardened, cls), R_ALLOW);
  }
}

TEST(PrivilegeProperties, HardenedNeverLoosensARow) {
  for (uint8_t p = 0; p < P_COUNT; p++)
    for (uint8_t cls = 0; cls < C_COUNT; cls++) {
      if (tableCell((Principal)p, false, cls) != R_ALLOW) EXPECT_NE(tableCell((Principal)p, true, cls), R_ALLOW) << (int)p << "/" << (int)cls;
    }
}

TEST(PrivilegeProperties, AnInconsistentContextIsUntrusted) {
  Context a(T_USB, A_CABLE, 0);
  a.remote = true;                                   // a cable that claims to be remote
  EXPECT_EQ(principalOf(a), P_UNTRUSTED);
  Context b(T_LORA, A_ADMIN, 0);
  b.remote = false;                                  // an over-the-air admin that claims to be local
  EXPECT_EQ(principalOf(b), P_UNTRUSTED);
  EXPECT_EQ(principalOf(Context()), P_UNTRUSTED);    // an unset context
  EXPECT_FALSE(decide(Context(), C_READ).allow);
}

TEST(PrivilegeProperties, AnUnknownClassIsRefused) {
  EXPECT_FALSE(decide(Context(T_USB, A_CABLE, 0), 99).allow);
}

TEST(PrivilegeProperties, TheAuditCallerIsTheWayTheRefusalHasAlwaysBeenRecorded) {
  EXPECT_EQ(decide(Context(T_USB, A_CABLE, 1), C_SECRET).caller, Posture::CALLER_CABLE);
  EXPECT_EQ(decide(Context(T_BLE, A_PAIRED_PIN, 1), C_DESTRUCTIVE).caller, Posture::CALLER_BLE);
  EXPECT_EQ(decide(Context(T_TCP, A_NONE, 0), C_READ).caller, Posture::CALLER_NET);
  EXPECT_EQ(decide(Context(T_LORA, A_ADMIN, 1), C_WRITE).caller, Posture::CALLER_REMOTE_ADMIN);
  EXPECT_EQ(decide(Context(T_LORA, A_CONTACT_FLAG, 1), C_WRITE).caller, Posture::CALLER_REMOTE_CLI);
  EXPECT_EQ(decide(Context(T_MQTT, A_TLS_VERIFIED, 1), C_WRITE).caller, Posture::CALLER_MQTT);
  EXPECT_EQ(decide(Context(T_BLE, A_PAIRED_DEFAULT_PIN, 0), C_READ).caller, Posture::CALLER_BLE);   // a default PIN is not a phone, but it is BLE
  EXPECT_EQ(decide(Context(T_NETCON, A_NONE, 0), C_READ).caller, Posture::CALLER_NET);
  EXPECT_EQ(decide(Context(T_NETCON, A_NETCON_ADMIN, 0), C_SECRET).caller, Posture::CALLER_NET);
}

TEST(PrivilegeProperties, TheEthernetConsoleIsNeverTheCable) {
  // An unauthenticated Ethernet console connection, and one that logged in, never resolve to the cable row.
  EXPECT_EQ(principalOf(Context(T_NETCON, A_NONE, 0)), P_UNTRUSTED);
  EXPECT_EQ(principalOf(Context(T_NETCON, A_CABLE, 0)), P_UNTRUSTED);            // a cable claim over the network
  EXPECT_EQ(principalOf(Context(T_NETCON, A_NETCON_ADMIN, 0)), P_NETCON_ADMIN);
  EXPECT_EQ(principalOf(Context(T_USB, A_NETCON_ADMIN, 0)), P_UNTRUSTED);        // and the reverse
  for (uint8_t cls = 0; cls < C_COUNT; cls++) EXPECT_FALSE(decide(Context(T_NETCON, A_NONE, 0), cls).allow);
}

// ---- the registry

TEST(Registry, CompanionClassification) {
  struct { const char* line; uint8_t cls; } cases[] = {
    {"get name", C_READ}, {"get radio", C_READ}, {"get posture", C_READ}, {"ver", C_READ}, {"board", C_READ},
    {"get connectivity.mode", C_READ}, {"get wifi.pwd", C_READ}, {"get mqtt.profile 0", C_READ},
    {"reboot", C_REBOOT},
    {"get audit", C_POSTURE}, {"get audit 4", C_POSTURE}, {"set posture hardened", C_POSTURE},
    {"set posture normal confirm", C_POSTURE}, {"set posture", C_POSTURE}, {"set connectivity.mode wifi", C_POSTURE},
    {"set mqtt.profile 2 manage on", C_POSTURE}, {"set mqtt.profile 2 manage", C_POSTURE},
    {"set name x", C_WRITE}, {"set pin 123456", C_WRITE}, {"poweroff", C_DESTRUCTIVE}, {"shutdown", C_DESTRUCTIVE},
    {"set wifi.pwd secret", C_WRITE}, {"set mqtt.profile 0 pwd secret", C_WRITE}, {"set mqtt.profile 0 host a.b", C_WRITE},
    {"set mqtt.enabled 1", C_WRITE}, {"rebootnow", C_WRITE}, {"reboot now", C_WRITE}, {"unknowncommand", C_WRITE},
    {"", C_WRITE}, {"set postures hardened", C_WRITE}, {"get auditx", C_READ},
    {"clock", C_WRITE}, {"neighbors", C_WRITE}, {"gps", C_WRITE}, {"region", C_WRITE}, {"erase", C_WRITE},
  };
  for (auto& c : cases) EXPECT_EQ(classify(c.line, ROLE_COMPANION), c.cls) << c.line;
}

TEST(Registry, RepeaterClassification) {
  struct { const char* line; uint8_t cls; } cases[] = {
    {"get name", C_READ}, {"ver", C_READ}, {"board", C_READ}, {"clock", C_READ}, {"neighbors", C_READ}, {"power", C_READ},
    {"powersaving", C_READ}, {"region", C_READ}, {"region get x", C_READ}, {"sensor get k", C_READ}, {"sensor list", C_READ},
    {"gps", C_READ}, {"get posture", C_READ}, {"reboot", C_REBOOT},
    {"get prv.key", C_SECRET}, {"set prv.key 00", C_SECRET}, {"get prv.key.x", C_SECRET},
    {"erase", C_DESTRUCTIVE},
    {"get audit", C_POSTURE}, {"set posture normal confirm", C_POSTURE},
    {"set name x", C_WRITE}, {"password x", C_WRITE}, {"setperm aa 1", C_WRITE}, {"advert", C_WRITE}, {"advert.zerohop", C_WRITE},
    {"start ota", C_WRITE}, {"clock sync", C_WRITE}, {"discover.neighbors", C_WRITE}, {"tempradio 1,2,3,4,5", C_WRITE},
    {"region load", C_WRITE}, {"region put a", C_WRITE}, {"poweroff", C_DESTRUCTIVE}, {"shutdown", C_DESTRUCTIVE}, {"log start", C_WRITE},
    {"sensor set a b", C_WRITE}, {"gps on", C_WRITE}, {"powersaving on", C_WRITE}, {"clear stats", C_WRITE},
    {"reboot now", C_WRITE}, {"time 5", C_WRITE}, {"neighbor.remove aa", C_WRITE},
    {"get connectivity.mode", C_READ},   // a read by prefix, as a Companion-only command it is unknown here
  };
  for (auto& c : cases) EXPECT_EQ(classify(c.line, ROLE_REPEATER), c.cls) << c.line;
}

TEST(Registry, SetPostureArgumentsAreClassifiedLikeThePostureHandlerParsesThem) {
  EXPECT_EQ(resolve("set posture hardened", ROLE_COMPANION).flags, F_RAISES);
  EXPECT_EQ(resolve("set posture   hardened", ROLE_COMPANION).flags, F_RAISES);       // the handler skips spaces too
  EXPECT_EQ(resolve("set posture normal", ROLE_COMPANION).flags, F_CABLE_ONLY);
  EXPECT_EQ(resolve("set posture normal confirm", ROLE_COMPANION).flags, F_CABLE_ONLY);
  EXPECT_EQ(resolve("set posture  normal confirm", ROLE_COMPANION).flags, F_CABLE_ONLY);
  EXPECT_EQ(resolve("set posture", ROLE_COMPANION).flags, F_CABLE_ONLY);        // anything but "hardened" lowers or is junk
  EXPECT_EQ(resolve("set posture hardenedX", ROLE_COMPANION).flags, F_CABLE_ONLY);
}

TEST(Registry, FramesCarryTheClassesOfWhatTheyDo) {
  EXPECT_EQ(resolveFrame(FR_CLI).cls, C_READ);
  EXPECT_EQ(resolveFrame(FR_KEY_EXPORT).cls, C_SECRET);
  EXPECT_EQ(resolveFrame(FR_KEY_IMPORT).cls, C_SECRET);
  EXPECT_EQ(resolveFrame(FR_SET_PIN).cls, C_WRITE);
  EXPECT_EQ(resolveFrame(FR_FACTORY_RESET).cls, C_DESTRUCTIVE);
  EXPECT_EQ(resolveFrame(FR_APP_WRITE).cls, C_WRITE);
  EXPECT_EQ(resolveFrame(FR_APP_WRITE).what, Posture::W_APP_FRAME);
  EXPECT_EQ(resolveFrame(FR_APP_REBOOT).cls, C_REBOOT);
  EXPECT_EQ(resolveFrame(FR_APP_REBOOT).what, Posture::W_APP_FRAME);
  EXPECT_EQ(resolveFrame(99).cls, C_DESTRUCTIVE);   // an id nobody registered is gated as the strictest class
}

// ---- the gate

struct Gate : public ::testing::Test {
  char reply[160];
  void SetUp() override { Posture::audit().clear(); }
  std::string run(const char* line, const Context& ctx, uint8_t role, bool* answered = nullptr, Outcome* out = nullptr) {
    reply[0] = 0;
    bool a = gate(line, ctx, role, 1000, reply, 150, out);
    if (answered) *answered = a;
    return reply;
  }
  int count(uint8_t caller, uint8_t what) {
    for (int i = 0; i < Posture::audit().n; i++) {
      auto& e = Posture::audit().e[i];
      if (e.event == Posture::EV_REFUSED && e.caller == caller && e.what == what) return e.count;
    }
    return 0;
  }
};

TEST_F(Gate, AnAllowedCommandIsLeftToTheHandler) {
  bool a = true;
  run("get name", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, &a);
  EXPECT_FALSE(a);
  run("set name x", Context(T_LORA, A_ADMIN, 0), ROLE_REPEATER, &a);
  EXPECT_FALSE(a);
  EXPECT_EQ(Posture::audit().n, 0);
}

TEST_F(Gate, TheWifiPasswordIsAnsweredWriteOnlyToEveryoneWhoMayRead) {
  for (const Context& ctx : {Context(T_USB, A_CABLE, 0), Context(T_BLE, A_PAIRED_PIN, 1), Context(T_LORA, A_CONTACT_FLAG, 0),
                             Context(T_MQTT, A_TLS_VERIFIED, 1)}) {
    bool a = false;
    Outcome o;
    EXPECT_EQ(run("get wifi.pwd", ctx, ROLE_COMPANION, &a, &o), "> wifi.pwd is write-only");
    EXPECT_TRUE(a);
    EXPECT_TRUE(o.allowed);
    EXPECT_TRUE(o.canned);
  }
  EXPECT_EQ(run("get wifi.pwd", Context(T_TCP, A_NONE, 0), ROLE_COMPANION), "ERR: not permitted");   // not even that for a stranger
}

TEST_F(Gate, FlaggedContactRefusals) {
  const Context n(T_LORA, A_CONTACT_FLAG, 0), h(T_LORA, A_CONTACT_FLAG, 1);
  EXPECT_EQ(run("set name x", n, ROLE_COMPANION), "ERR: not permitted remotely");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_REMOTE_WRITE), 1);
  EXPECT_EQ(run("set posture hardened", n, ROLE_COMPANION), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_POSTURE), 1);
  EXPECT_EQ(run("get audit", n, ROLE_COMPANION), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_AUDIT), 1);
  EXPECT_EQ(run("set connectivity.mode wifi", n, ROLE_COMPANION), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_CONNMODE), 1);
  EXPECT_EQ(run("set mqtt.profile 0 manage on", n, ROLE_COMPANION), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_MGMT_FLAG), 1);
  bool a = true;
  run("ver", n, ROLE_COMPANION, &a);
  EXPECT_FALSE(a);
  run("reboot", n, ROLE_COMPANION, &a);
  EXPECT_FALSE(a);
  EXPECT_EQ(run("ver", h, ROLE_COMPANION), "ERR: remote CLI is off in hardened posture");   // Hardened: not even a read
  EXPECT_EQ(count(Posture::CALLER_REMOTE_CLI, Posture::W_REMOTE_CLI), 1);
}

TEST_F(Gate, RemoteAdminRefusals) {
  const Context n(T_LORA, A_ADMIN, 0), h(T_LORA, A_ADMIN, 1), d(T_LORA, A_ADMIN_DEFAULT_PW, 0);
  EXPECT_EQ(run("set prv.key 00", n, ROLE_REPEATER), "ERR: prv.key can only be set from the local console");
  EXPECT_EQ(Posture::audit().n, 0);                                        // explained, not logged: as it always was
  EXPECT_EQ(run("set prv.key 00", h, ROLE_REPEATER), "ERR: not permitted in hardened posture");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_ADMIN, Posture::W_REMOTE_WRITE), 1);
  EXPECT_EQ(run("set name x", h, ROLE_REPEATER), "ERR: not permitted in hardened posture");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_ADMIN, Posture::W_REMOTE_WRITE), 2);
  EXPECT_EQ(run("get name", d, ROLE_REPEATER),
            "ERR: remote admin is off until the admin password is set on the local console");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_ADMIN, Posture::W_DEFAULT_PASSWORD), 1);
  EXPECT_EQ(run("erase", n, ROLE_REPEATER), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_REMOTE_ADMIN, Posture::W_REMOTE_WRITE), 3);
}

// ---- the Ethernet console (NetConsole.h): untrusted until `login`, then the remote admin's rights, off in Hardened

struct EthConsole : public Gate {
  Cli::NetConsole nc;
  std::string line(const char* l, const char* pw = "s3cret", uint8_t posture = 0, bool* answered = nullptr) {
    reply[0] = 0;
    bool a = netConsoleLine(nc, l, pw, "password", posture, 1000, reply, 150);
    if (answered) *answered = a;
    return reply;
  }
  int logins(uint8_t what) {
    for (int i = 0; i < Posture::audit().n; i++) {
      auto& e = Posture::audit().e[i];
      if (e.event == Posture::EV_LOGIN_FAILED && e.caller == Posture::CALLER_NET && e.what == what) return e.count;
    }
    return 0;
  }
};

TEST_F(EthConsole, EveryLineIsRefusedAndAuditedUntilLogin) {
  EXPECT_EQ(line("get prv.key"), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_NET, Posture::W_KEY_EXPORT), 1);
  EXPECT_EQ(line("set prv.key 00"), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_NET, Posture::W_KEY_IMPORT), 1);
  EXPECT_EQ(line("set posture hardened"), "ERR: not permitted");
  EXPECT_EQ(count(Posture::CALLER_NET, Posture::W_POSTURE), 1);
  EXPECT_EQ(line("erase"), "ERR: not permitted");
  EXPECT_EQ(line("get name"), "ERR: not permitted");
  EXPECT_EQ(line("reboot"), "ERR: not permitted");
}

TEST_F(EthConsole, LoginRulesAndWhatItOpens) {
  EXPECT_EQ(line("login wrong"), "ERR: wrong password");
  EXPECT_EQ(logins(Posture::W_BAD_PASSWORD), 1);
  EXPECT_EQ(line("get name"), "ERR: not permitted");
  EXPECT_EQ(line("login s3cret"), "OK - logged in");
  bool a = true;
  line("get name", "s3cret", 0, &a); EXPECT_FALSE(a);                        // runs, as a remote admin
  line("set name x", "s3cret", 0, &a); EXPECT_FALSE(a);
  line("reboot", "s3cret", 0, &a); EXPECT_FALSE(a);
  EXPECT_EQ(line("get prv.key"), "ERR: not permitted");
  EXPECT_EQ(line("set prv.key 00"), "ERR: prv.key can only be set from the local console");
  EXPECT_EQ(line("erase"), "ERR: not permitted");
  EXPECT_EQ(line("poweroff"), "ERR: not permitted");
  EXPECT_EQ(line("get audit"), "ERR: not permitted");
  EXPECT_EQ(line("set posture normal confirm"), "ERR: not permitted");
  EXPECT_EQ(line("set posture hardened"), "ERR: not permitted");
  EXPECT_EQ(line("logout"), "OK - logged out");
  EXPECT_EQ(line("get name"), "ERR: not permitted");
}

TEST_F(EthConsole, TheDefaultPasswordOpensNothing) {
  for (const char* pw : {"password", ""}) {
    nc = Cli::NetConsole();
    const std::string cmd = std::string("login ") + pw;
    EXPECT_NE(line(cmd.c_str(), pw[0] ? pw : ""), "OK - logged in") << pw;       // stored == typed, and the stock default
    EXPECT_FALSE(nc.authed);
  }
  nc = Cli::NetConsole();
  Posture::audit().clear();
  EXPECT_EQ(line("login password", "password"),
            "ERR: the Ethernet console is off until the admin password is set on the local console");
  EXPECT_EQ(logins(Posture::W_DEFAULT_PASSWORD), 1);
  EXPECT_EQ(line("get name", "password"), "ERR: not permitted");
}

TEST_F(EthConsole, HardenedRefusesLoginAndDropsALiveSession) {
  EXPECT_EQ(line("login s3cret"), "OK - logged in");
  EXPECT_EQ(line("get name", "s3cret", 1), "ERR: not permitted");
  EXPECT_FALSE(nc.authed);
  EXPECT_EQ(line("login s3cret", "s3cret", 1), "ERR: the Ethernet console is off in hardened posture");
  EXPECT_FALSE(nc.authed);
  EXPECT_EQ(line("get name", "s3cret", 0), "ERR: not permitted");              // back in Normal: logged out
}

TEST_F(EthConsole, ChangingBackToTheDefaultPasswordDropsTheSession) {
  EXPECT_EQ(line("login s3cret"), "OK - logged in");
  EXPECT_EQ(line("get name", "password"), "ERR: not permitted");
}

TEST_F(EthConsole, FiveWrongPasswordsLockLoginsForAMinute) {
  for (int i = 0; i < 5; i++) EXPECT_EQ(line("login nope"), "ERR: wrong password");
  EXPECT_EQ(line("login s3cret"), "ERR: too many failed logins, try again later");
  nc.endSession();                                                             // a new connection: still locked
  EXPECT_EQ(line("login s3cret"), "ERR: too many failed logins, try again later");
  reply[0] = 0;
  EXPECT_TRUE(netConsoleLine(nc, "login s3cret", "s3cret", "password", 0, 1000 + 61, reply, 150));
  EXPECT_STREQ(reply, "OK - logged in");
}

TEST_F(Gate, HardenedKeyRefusalsOnTheConsole) {
  const Context h(T_USB, A_CABLE, 1);
  EXPECT_EQ(run("get prv.key", h, ROLE_REPEATER), "ERR: key export is refused in hardened posture");
  EXPECT_EQ(count(Posture::CALLER_CABLE, Posture::W_KEY_EXPORT), 1);
  EXPECT_EQ(run("set prv.key 00", h, ROLE_REPEATER), "ERR: key import is refused in hardened posture");
  EXPECT_EQ(count(Posture::CALLER_CABLE, Posture::W_KEY_IMPORT), 1);
  bool a = true;
  run("get prv.key", Context(T_USB, A_CABLE, 0), ROLE_REPEATER, &a);
  EXPECT_FALSE(a);                                                           // Normal: the console may
}

TEST_F(Gate, NoRemoteCallerPowersTheDeviceOff) {
  for (const Context& ctx : {Context(T_MQTT, A_TLS_VERIFIED, 0, true), Context(T_MQTT, A_TLS_VERIFIED, 1, true),
                             Context(T_LORA, A_ADMIN, 0), Context(T_LORA, A_CONTACT_FLAG, 0)})
    for (const char* line : {"poweroff", "shutdown"}) {
      bool a = false;
      run(line, ctx, ROLE_COMPANION, &a);
      EXPECT_TRUE(a) << line;
    }
  bool a = true;
  run("poweroff", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, &a);
  EXPECT_FALSE(a);
}

TEST_F(Gate, TheBrokerSession) {
  const Context unv(T_MQTT, A_TLS_UNVERIFIED, 0);
  EXPECT_EQ(run("get name", unv, ROLE_COMPANION), "ERR: management needs a TLS-verified broker");
  EXPECT_EQ(count(Posture::CALLER_MQTT, Posture::W_MQTT_SESSION), 1);
  EXPECT_EQ(run("get name", unv, ROLE_COMPANION), "ERR: management needs a TLS-verified broker");
  EXPECT_EQ(Posture::audit().n, 1);                                          // repeats fold: a flood cannot fill the ring
  EXPECT_EQ(Posture::audit().e[0].count, 2);

  const Context v(T_MQTT, A_TLS_VERIFIED, 0), vh(T_MQTT, A_TLS_VERIFIED, 1), vhm(T_MQTT, A_TLS_VERIFIED, 1, true);
  bool a = true;
  run("get name", vh, ROLE_COMPANION, &a);
  EXPECT_FALSE(a);                                                           // reads always
  run("set name x", v, ROLE_COMPANION, &a);
  EXPECT_FALSE(a);                                                           // Normal: management
  EXPECT_EQ(run("set name x", vh, ROLE_COMPANION), "ERR: management is off for this broker profile");
  EXPECT_EQ(count(Posture::CALLER_MQTT, Posture::W_REMOTE_WRITE), 1);
  run("set name x", vhm, ROLE_COMPANION, &a);
  EXPECT_FALSE(a);                                                           // Hardened with the flag: management
  EXPECT_EQ(run("set posture normal confirm", vhm, ROLE_COMPANION), "ERR: not permitted");   // never the posture
  EXPECT_EQ(run("set mqtt.profile 0 manage on", vhm, ROLE_COMPANION), "ERR: not permitted"); // nor the flag itself
  EXPECT_EQ(run("set connectivity.mode wifi", vhm, ROLE_COMPANION), "ERR: not permitted");
  EXPECT_EQ(run("get audit", vhm, ROLE_COMPANION), "ERR: not permitted");
}

TEST_F(Gate, FramesAreGatedByTheSameTable) {
  Outcome o;
  EXPECT_TRUE(allowFrame(Context(T_USB, A_CABLE, 0), FR_KEY_EXPORT, 5));
  EXPECT_FALSE(allowFrame(Context(T_USB, A_CABLE, 1), FR_KEY_EXPORT, 5, &o));
  EXPECT_EQ(o.reason, R_KEY_HARDENED);
  EXPECT_EQ(count(Posture::CALLER_CABLE, Posture::W_KEY_EXPORT), 1);
  EXPECT_FALSE(allowFrame(Context(T_USB, A_CABLE, 1), FR_KEY_IMPORT, 5));
  EXPECT_FALSE(allowFrame(Context(T_TCP, A_NONE, 0), FR_CLI, 5));
  EXPECT_EQ(count(Posture::CALLER_NET, Posture::W_CLI), 1);
  EXPECT_FALSE(allowFrame(Context(T_TCP, A_NONE, 0), FR_SET_PIN, 5));
  EXPECT_EQ(count(Posture::CALLER_NET, Posture::W_SET_PIN), 1);
  EXPECT_TRUE(allowFrame(Context(T_BLE, A_PAIRED_PIN, 0), FR_FACTORY_RESET, 5));                // Normal: a paired phone may
  EXPECT_FALSE(allowFrame(Context(T_BLE, A_PAIRED_PIN, 1), FR_FACTORY_RESET, 5));               // Hardened: a cable only
  EXPECT_EQ(count(Posture::CALLER_BLE, Posture::W_FACTORY_RESET), 1);
  EXPECT_TRUE(allowFrame(Context(T_USB, A_CABLE, 1), FR_FACTORY_RESET, 5));
  // a management frame: the TCP port and a default-PIN phone are refused in Normal; a cable and a paired phone are not
  EXPECT_FALSE(allowFrame(Context(T_TCP, A_NONE, 0), FR_APP_WRITE, 5));
  EXPECT_FALSE(allowFrame(Context(T_BLE, A_PAIRED_DEFAULT_PIN, 0), FR_APP_WRITE, 5));
  EXPECT_FALSE(allowFrame(Context(T_TCP, A_NONE, 0), FR_APP_REBOOT, 5));
  EXPECT_TRUE(allowFrame(Context(T_USB, A_CABLE, 0), FR_APP_WRITE, 5));
  EXPECT_TRUE(allowFrame(Context(T_BLE, A_PAIRED_PIN, 1), FR_APP_WRITE, 5));
  EXPECT_TRUE(allowFrame(Context(T_BLE, A_PAIRED_PIN, 1), FR_APP_REBOOT, 5));
  EXPECT_FALSE(allowFrame(Context(T_BLE, A_PAIRED_DEFAULT_PIN, 0), FR_CLI, 5));                 // a default PIN authenticates nobody
}

TEST_F(Gate, TheFacadesInPostureAndConnModeAskTheSameTable) {
  for (uint8_t caller : std::vector<uint8_t>{Posture::CALLER_CABLE, Posture::CALLER_BLE, Posture::CALLER_NET, Posture::CALLER_REMOTE_ADMIN,
                         Posture::CALLER_REMOTE_CLI, Posture::CALLER_BUTTON, 0, 200}) {
    for (uint8_t posture : {(uint8_t)0, (uint8_t)1}) {
      EXPECT_EQ(Posture::keyTransferAllowed(posture), decide(contextForCaller(Posture::CALLER_CABLE, posture), C_SECRET).allow);
      EXPECT_EQ(Posture::factoryResetAllowed(posture, caller), decide(contextForCaller(caller, posture), C_DESTRUCTIVE).allow);
    }
  }
}

TEST_F(Gate, TheConnectivityModeSwitchAsksTheSameTable) {
  for (uint8_t caller : std::vector<uint8_t>{Posture::CALLER_CABLE, Posture::CALLER_BLE, Posture::CALLER_NET, Posture::CALLER_REMOTE_ADMIN,
                         Posture::CALLER_REMOTE_CLI, Posture::CALLER_BUTTON, Posture::CALLER_MQTT, 0, 200})
    EXPECT_EQ(ConnMode::callerMaySwitch(caller), decide(contextForCaller(caller, 0), C_POSTURE).allow) << (int)caller;
}

// ---- MECON's own commands

TEST(MeconCli, WhoReportsTheCallerAsTheEngineSeesIt) {
  char r[160];
  EXPECT_TRUE(handleMecon("get mecon.cli.who", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r)));
  EXPECT_STREQ(r, "> transport=usb auth=cable remote=0 posture=normal principal=cable");
  handleMecon("get mecon.cli.who", Context(T_MQTT, A_TLS_VERIFIED, 1, true), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> transport=mqtt auth=tls-verified remote=1 posture=hardened principal=mqtt mgmt=on");
  handleMecon("get mecon.cli.who", Context(T_MQTT, A_TLS_UNVERIFIED, 0), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> transport=mqtt auth=tls-unverified remote=1 posture=normal principal=mqtt-unverified");
  handleMecon("get mecon.cli.who", Context(), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> transport=none auth=none remote=0 posture=normal principal=untrusted");
}

TEST(MeconCli, MemIsAReadAndSaysWhenItCannotMeasure) {
  char r[160];
  // the native unit build has no heap API: the reply must say so rather than print zeros as if measured
  EXPECT_TRUE(handleMecon("get mecon.mem", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r)));
  EXPECT_STREQ(r, "> heap: not measurable on this platform");
  EXPECT_EQ(resolve("get mecon.mem", ROLE_COMPANION).cls, C_READ);
  EXPECT_EQ(resolve("get mecon.mem", ROLE_REPEATER).cls, C_READ);
}

TEST(MeconCli, CheckIsADryRunOfTheTable) {
  char r[160];
  Posture::audit().clear();
  handleMecon("get mecon.cli.check set name x", Context(T_LORA, A_CONTACT_FLAG, 0), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=write remote-limited");
  handleMecon("get mecon.cli.check set name x", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=write allowed");
  handleMecon("get mecon.cli.check get prv.key", Context(T_USB, A_CABLE, 1), ROLE_REPEATER, r, sizeof(r));
  EXPECT_STREQ(r, "> class=secret key-hardened");
  handleMecon("get mecon.cli.check set name x", Context(T_MQTT, A_TLS_VERIFIED, 1), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=write broker-read-only");
  handleMecon("get mecon.cli.check set name x", Context(T_MQTT, A_TLS_VERIFIED, 1, true), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=write allowed");
  EXPECT_EQ(Posture::audit().n, 0);                       // asking is not refused, and not logged
  EXPECT_FALSE(handleMecon("get name", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r)));
  EXPECT_FALSE(handleMecon("get mecon.cli", Context(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r)));
}

// ---- once the device holds a Deployment record (AUTHORITY.md section 9): Context.enrolled

static Context enrolledCtx(uint8_t t, uint8_t a, uint8_t posture, bool mgmt = false) {
  Context c(t, a, posture, mgmt);
  c.enrolled = true;
  return c;
}

TEST(Enrolled, AnUnsignedCallerKeepsReadsALocalOneRebootAndLosesEveryOtherClass) {
  struct Caller { Context plain; const char* name; };
  const Caller callers[] = {
    {Context(T_USB, A_CABLE, 0), "cable"},
    {Context(T_BLE, A_PAIRED_PIN, 0), "paired BLE"},
    {Context(T_LORA, A_ADMIN, 0), "remote admin"},
    {Context(T_LORA, A_CONTACT_FLAG, 0), "flagged contact"},
    {Context(T_MQTT, A_TLS_VERIFIED, 0, true), "MQTT verified, managed"},
    {Context(T_NETCON, A_NETCON_ADMIN, 0), "Ethernet admin"},
  };
  for (const Caller& k : callers) {
    Context e = k.plain;
    e.enrolled = true;
    for (uint8_t cls = 0; cls < C_COUNT; cls++) {
      SCOPED_TRACE(std::string(k.name) + " class " + std::to_string(cls));
      const Verdict before = decide(k.plain, cls), after = decide(e, cls);
      if (cls == C_READ || (cls == C_REBOOT && !e.remote)) {
        EXPECT_EQ(after.reason, before.reason) << "reads, and a local reboot, are the table's, enrolled or not";
      } else if (cls == C_DESTRUCTIVE && principalOf(e) == P_CABLE) {
        EXPECT_EQ(after.reason, before.reason) << "the cable keeps erase and factory reset";
        EXPECT_TRUE(after.allow);
      } else if (before.allow) {
        EXPECT_FALSE(after.allow);
        EXPECT_EQ(after.reason, R_ENROLLED);
      } else {
        EXPECT_EQ(after.reason, before.reason) << "a cell that already refused keeps its own reason";
      }
    }
  }
}

TEST(Enrolled, TheFlagIsIgnoredForASignedCallerAndNeverLoosensAnything) {
  for (uint8_t t = T_NONE; t <= T_NETCON; t++)
    for (uint8_t a = A_NONE; a <= A_SIGNED; a++)
      for (uint8_t posture : {(uint8_t)0, (uint8_t)1})
        for (uint8_t cls = 0; cls < C_COUNT; cls++) {
          const Context plain(t, a, posture);
          Context e = plain;
          e.enrolled = true;
          const Verdict v0 = decide(plain, cls), v1 = decide(e, cls);
          if (principalOf(plain) == P_SIGNED) EXPECT_EQ(v1.reason, v0.reason);
          if (!v0.allow) EXPECT_FALSE(v1.allow) << (int)t << "/" << (int)a << "/" << (int)cls;
        }
}

TEST(Enrolled, TheCableIsTheOnlyUnsignedCallerThatKeepsTheDestructiveClass) {
  for (uint8_t posture : {(uint8_t)0, (uint8_t)1}) {
    EXPECT_TRUE(decide(enrolledCtx(T_USB, A_CABLE, posture), C_DESTRUCTIVE).allow);
    EXPECT_FALSE(decide(enrolledCtx(T_BLE, A_PAIRED_PIN, posture), C_DESTRUCTIVE).allow) << "no BLE factory reset";
  }
  EXPECT_EQ(decide(enrolledCtx(T_BLE, A_PAIRED_PIN, 0), C_DESTRUCTIVE).reason, R_ENROLLED);
  EXPECT_TRUE(decide(Context(T_BLE, A_PAIRED_PIN, 0), C_DESTRUCTIVE).allow) << "standalone: unchanged";
}

TEST(Enrolled, AUnsignedCommandLineIsRefusedWithAnExplicitReplyAndAnAuditEntry) {
  char r[160];
  Posture::audit().clear();
  Outcome o;
  EXPECT_TRUE(gate("set name x", enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed);
  EXPECT_STREQ(r, "ERR: this device belongs to a Deployment; the change must be signed");
  EXPECT_EQ(Posture::audit().n, 1);
  EXPECT_EQ(Posture::audit().e[0].caller, Posture::CALLER_CABLE);
  for (const char* line : {"set prv.key 00", "set posture hardened", "set mqtt.profile 0 manage on"}) {
    EXPECT_TRUE(gate(line, enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o)) << line;
    EXPECT_FALSE(o.allowed) << line;
  }
  EXPECT_FALSE(gate("get name", enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o)) << "reads run";
  EXPECT_TRUE(o.allowed);
  EXPECT_FALSE(gate("reboot", enrolledCtx(T_BLE, A_PAIRED_PIN, 0), ROLE_COMPANION, 5, r, sizeof(r), &o)) << "reboot runs";
  EXPECT_FALSE(gate("set name x", Context(T_USB, A_SIGNED, 0), ROLE_COMPANION, 5, r, sizeof(r), &o));
}

TEST(Enrolled, MeconCanReportsTheVerdict) {
  char r[160];
  handleMecon("get mecon.cli.check set name x", enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=write enrolled-unsigned");
  handleMecon("get mecon.cli.check get name", enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, r, sizeof(r));
  EXPECT_STREQ(r, "> class=read allowed");
}

// ---- the local unlock (AUTHORITY.md sections 9 and 10): Context.unlocked

static Context unlockedCtx(uint8_t t, uint8_t a, uint8_t posture) {
  Context c = enrolledCtx(t, a, posture);
  c.unlocked = true;
  return c;
}

TEST(Unlocked, AnUnlockedCableOpensOnlyThePostureAndOnlyInNormal) {
  EXPECT_FALSE(decide(enrolledCtx(T_USB, A_CABLE, 0), C_POSTURE).allow) << "control: enrolled, locked";
  EXPECT_TRUE(decide(unlockedCtx(T_USB, A_CABLE, 0), C_POSTURE).allow);
  EXPECT_FALSE(decide(unlockedCtx(T_USB, A_CABLE, 1), C_POSTURE).allow) << "Hardened: the management surface, not the posture";
  EXPECT_EQ(decide(unlockedCtx(T_USB, A_CABLE, 1), C_POSTURE).reason, R_ENROLLED);
  for (uint8_t posture : {(uint8_t)0, (uint8_t)1}) {
    EXPECT_FALSE(decide(unlockedCtx(T_USB, A_CABLE, posture), C_WRITE).allow) << "no configuration write";
    EXPECT_EQ(decide(unlockedCtx(T_USB, A_CABLE, posture), C_WRITE).reason, R_ENROLLED);
    EXPECT_FALSE(decide(unlockedCtx(T_USB, A_CABLE, posture), C_SECRET).allow) << "no key export";
    EXPECT_TRUE(decide(unlockedCtx(T_USB, A_CABLE, posture), C_READ).allow);
  }
}

TEST(Unlocked, TheFlagCountsForTheCablePrincipalAndNoOther) {
  for (uint8_t t = T_NONE; t <= T_NETCON; t++)
    for (uint8_t a = A_NONE; a <= A_SIGNED; a++)
      for (uint8_t posture : {(uint8_t)0, (uint8_t)1})
        for (uint8_t cls = 0; cls < C_COUNT; cls++) {
          Context locked(t, a, posture);
          locked.enrolled = true;
          Context open = locked;
          open.unlocked = true;
          const Verdict v0 = decide(locked, cls), v1 = decide(open, cls);
          if (principalOf(locked) != P_CABLE) {
            EXPECT_EQ(v1.reason, v0.reason) << (int)t << "/" << (int)a << "/" << (int)cls;
            EXPECT_FALSE(unlockManagement(open));
          }
          if (!v0.allow && !(principalOf(locked) == P_CABLE && cls == C_POSTURE && posture == 0)) EXPECT_FALSE(v1.allow);
        }
  Context bad(T_USB, A_CABLE, 0);   // a remote flag that does not match the transport is the untrusted row
  bad.remote = true;
  bad.unlocked = true;
  EXPECT_FALSE(unlockManagement(bad));
  EXPECT_TRUE(unlockManagement(unlockedCtx(T_USB, A_CABLE, 1)));
  EXPECT_FALSE(unlockManagement(enrolledCtx(T_USB, A_CABLE, 1)));
}

TEST(Unlocked, TheServerCallerAsksTheLinkEachTimeAndOnlyTheConsoleCableGetsIt) {
  ServerCaller& sc = serverCaller();
  struct Reset { ~Reset() { serverCaller() = ServerCaller(); } } reset;
  static bool live = true;
  sc.enrolled = true;
  sc.unlocked = [](void*) { return live; };
  EXPECT_TRUE(withServerCaller(Context(T_USB, A_CABLE, 1)).unlocked);
  live = false;
  EXPECT_FALSE(withServerCaller(Context(T_USB, A_CABLE, 1)).unlocked) << "asked each time: a lapsed unlock is gone";
  live = true;
  EXPECT_FALSE(withServerCaller(Context(T_NETCON, A_NETCON_ADMIN, 1)).unlocked);
  EXPECT_FALSE(withServerCaller(Context(T_LORA, A_ADMIN, 1)).unlocked);
  sc.signed_line = true;
  EXPECT_FALSE(withServerCaller(Context(T_USB, A_CABLE, 1)).unlocked) << "a signed line runs as the signed principal";
  sc.signed_line = false;
  sc.unlocked = nullptr;
  EXPECT_FALSE(withServerCaller(Context(T_USB, A_CABLE, 1)).unlocked);
}

TEST(Unlocked, ACommandLineFromAnUnlockedCableMayChangeThePostureInNormalOnly) {
  char r[160];
  Outcome o;
  EXPECT_TRUE(gate("set posture hardened", enrolledCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed);
  Context u = unlockedCtx(T_USB, A_CABLE, 0);
  u.has_unlock_secret = true;   // an unlocked cable implies a secret in the record
  EXPECT_FALSE(gate("set posture hardened", u, ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_TRUE(o.allowed);
  EXPECT_TRUE(gate("set posture normal confirm", unlockedCtx(T_USB, A_CABLE, 1), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed);
  EXPECT_TRUE(gate("set name x", unlockedCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed) << "no configuration write";
  EXPECT_TRUE(gate("set prv.key 00", unlockedCtx(T_USB, A_CABLE, 0), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed) << "no key import";
}

// ---- the servers' caller state (cli/CliCaller.h): enrolled, and a verified signed console line

struct ServerCallerReset {
  ServerCallerReset() { serverCaller() = ServerCaller(); }
  ~ServerCallerReset() { serverCaller() = ServerCaller(); }
};

TEST(ServerCaller, StandaloneChangesNothing) {
  ServerCallerReset r;
  Context c = withServerCaller(Context(T_USB, A_CABLE, 0));
  EXPECT_FALSE(c.enrolled);
  EXPECT_EQ(c.auth, A_CABLE);
  EXPECT_TRUE(decide(c, C_WRITE).allow);
}

TEST(ServerCaller, EnrolledRefusesUnsignedWritesOnEveryServerTransportButTheCableKeepsErase) {
  ServerCallerReset r;
  serverCaller().enrolled = true;
  const Context cable = withServerCaller(Context(T_USB, A_CABLE, 0));
  const Context lora = withServerCaller(Context(T_LORA, A_ADMIN, 0));
  const Context eth = withServerCaller(Context(T_NETCON, A_NETCON_ADMIN, 0));
  EXPECT_FALSE(decide(cable, C_WRITE).allow);
  EXPECT_FALSE(decide(lora, C_WRITE).allow);
  EXPECT_FALSE(decide(eth, C_WRITE).allow);
  EXPECT_TRUE(decide(lora, C_READ).allow) << "LoRa remote admin keeps reads";
  EXPECT_FALSE(decide(lora, C_REBOOT).allow) << "and nothing else";
  EXPECT_TRUE(decide(cable, C_DESTRUCTIVE).allow) << "the cable keeps erase";
  EXPECT_FALSE(decide(eth, C_DESTRUCTIVE).allow);
}

TEST(ServerCaller, OnlyAVerifiedLineOnTheCableBecomesTheSignedPrincipal) {
  ServerCallerReset r;
  serverCaller().enrolled = true;
  EXPECT_EQ(withServerCaller(Context(T_USB, A_CABLE, 0)).auth, A_CABLE);
  serverCaller().signed_line = true;
  const Context s = withServerCaller(Context(T_USB, A_CABLE, 0));
  EXPECT_EQ(s.auth, A_SIGNED);
  EXPECT_TRUE(decide(s, C_WRITE).allow) << "a signed write runs on an enrolled device";
  EXPECT_FALSE(decide(s, C_DESTRUCTIVE).allow) << "no erase, whatever the signature";
  EXPECT_EQ(withServerCaller(Context(T_LORA, A_ADMIN, 0)).auth, A_ADMIN) << "a remote caller is never promoted";
  EXPECT_EQ(withServerCaller(Context(T_NETCON, A_NETCON_ADMIN, 0)).auth, A_NETCON_ADMIN);
}

TEST(ServerCaller, FormattedCallsTheRegisteredHookOnce) {
  ServerCallerReset r;
  static int calls;
  calls = 0;
  serverFormatted();   // no hook: nothing happens
  serverCaller().on_format = [](void*) { calls++; };
  serverFormatted();
  EXPECT_EQ(calls, 1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ---- lowering by a signed request, and Hardened only with a local unlock secret (AUTHORITY.md sections 10 and 11)

TEST(Posture2, ASignedRequestMayLowerTheFlaggedPostureCommandsAndNoOtherWireCallerMay) {
  for (uint8_t t : {T_USB, T_BLE, T_TCP, T_MQTT, T_LORA}) {
    EXPECT_TRUE(decide(Context(t, A_SIGNED, 1), C_POSTURE, F_CABLE_ONLY).allow) << "transport " << (int)t;
  }
  char r[160];
  Outcome o;
  Context sig(T_MQTT, A_SIGNED, 1);
  sig.enrolled = true;
  EXPECT_FALSE(gate("set posture normal confirm", sig, ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_TRUE(o.allowed) << "signed, enrolled, Hardened to Normal";
  EXPECT_TRUE(gate("set posture normal confirm", enrolledCtx(T_BLE, A_PAIRED_PIN, 1), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed) << "an unsigned paired phone still may not";
  EXPECT_TRUE(gate("set posture normal confirm", Context(T_MQTT, A_TLS_VERIFIED, 1), ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed);
}

TEST(Posture2, EnteringHardenedWhileEnrolledNeedsALocalUnlockSecretInTheRecord) {
  Context sig(T_USB, A_SIGNED, 0);
  sig.enrolled = true;
  Verdict v = decide(sig, C_POSTURE, F_RAISES);
  EXPECT_FALSE(v.allow);
  EXPECT_EQ(v.reason, R_NO_UNLOCK_SECRET);
  sig.has_unlock_secret = true;
  EXPECT_TRUE(decide(sig, C_POSTURE, F_RAISES).allow);
  char r[160];
  Outcome o;
  Context nos(T_MQTT, A_SIGNED, 0);
  nos.enrolled = true;
  EXPECT_TRUE(gate("set posture hardened", nos, ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_FALSE(o.allowed);
  EXPECT_EQ(o.reason, R_NO_UNLOCK_SECRET);
  EXPECT_STREQ(r, "ERR: set a local unlock secret before Hardened (the device belongs to a Deployment)");
  nos.has_unlock_secret = true;
  EXPECT_FALSE(gate("set posture hardened", nos, ROLE_COMPANION, 5, r, sizeof(r), &o));
  EXPECT_TRUE(o.allowed);
  // a device with no record is untouched by the rule
  EXPECT_TRUE(decide(Context(T_USB, A_CABLE, 0), C_POSTURE, F_RAISES).allow);
  EXPECT_TRUE(decide(Context(T_BLE, A_PAIRED_PIN, 0), C_POSTURE, F_RAISES).allow);
  // the server's context carries the fact from the record's owner
  ServerCaller& sc = serverCaller();
  sc.enrolled = true;
  EXPECT_FALSE(withServerCaller(Context(T_USB, A_CABLE, 0)).has_unlock_secret);
  static bool have = false;
  sc.has_secret = [](void*) { return have; };
  EXPECT_FALSE(withServerCaller(Context(T_USB, A_CABLE, 0)).has_unlock_secret);
  have = true;
  EXPECT_TRUE(withServerCaller(Context(T_USB, A_CABLE, 0)).has_unlock_secret);
  sc.has_secret = nullptr;
  sc.enrolled = false;
}
