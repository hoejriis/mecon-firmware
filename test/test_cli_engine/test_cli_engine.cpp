// The command-line engine (src/helpers/cli/, docs/CLI.md): the privilege table cell by cell, the registry's
// classification, the gate's replies and audit entries, and the MQTT request/response framing.
#include <gtest/gtest.h>
#include <string.h>
#include <string>
#include <vector>
#include "helpers/Posture.h"
#include "helpers/cli/CliEngine.h"
#include "helpers/connmode/ConnMode.h"

using namespace Cli;

namespace {

// ---- the expected table, written out independently of CliPolicy.h
// One character per class, in the order READ WRITE SECRET POSTURE DESTRUCTIVE REBOOT:
//   .  allowed          U  untrusted transport      P  not permitted          L  flagged contact limited to reads and reboot
//   H  hardened remote  O  remote CLI off           D  admin password is default
//   K  key refused in Hardened      Y  local console only      V  broker session not verified
//   F  allowed when the broker profile is flagged for management, else R (read-only in Hardened)
struct Expect {
  Principal p;
  const char* name;
  const char* normal;
  const char* hardened;
};
const Expect EXPECT_TABLE[] = {
  {P_UNTRUSTED,       "untrusted",        "UUUUUU", "UUUUUU"},
  {P_CABLE,           "cable",            "......", "..K..."},
  {P_PAIRED,          "paired BLE",       "......", "..K.P."},
  {P_ADMIN,           "remote admin",     ". . Y P Y .", ". H H P H ."},
  {P_ADMIN_DEFAULT,   "admin, default pw", "DDDDDD", "DDDDDD"},
  {P_GUEST,           "guest",            "PPPPPP", "PPPPPP"},
  {P_CONTACT,         "flagged contact",  ".LLPL.", "OOOOOO"},
  {P_MQTT,            "MQTT verified",    "..PPP.", ".FPPPF"},
  {P_MQTT_UNVERIFIED, "MQTT unverified",  "VVVVVV", "VVVVVV"},
  {P_BUTTON,          "button",           "PPP.PP", "PPP.PP"},
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
};

Principal expectedPrincipal(uint8_t t, uint8_t a, bool remote) {
  for (const Valid& v : VALID) if (v.t == t && v.a == a && v.remote == remote) return v.p;
  return P_UNTRUSTED;
}

const Expect& rowFor(Principal p) {
  for (const Expect& e : EXPECT_TABLE) if (e.p == p) return e;
  static Expect none = {P_UNTRUSTED, "?", "UUUUUU", "UUUUUU"};
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
        EXPECT_EQ(lower.allow, want == R_ALLOW && want_p == P_CABLE);
        if (want == R_ALLOW && want_p != P_CABLE) EXPECT_EQ(lower.reason, R_NOT_PERMITTED);
        else EXPECT_EQ(lower.reason, want);
      }
    }
  }
}

static std::vector<Cell> allCells() {
  std::vector<Cell> v;
  for (uint8_t t = T_NONE; t <= T_BUTTON; t++)
    for (uint8_t a = A_NONE; a <= A_BUTTON; a++)
      for (bool remote : {false, true}) v.push_back({t, a, remote});
  return v;
}
INSTANTIATE_TEST_SUITE_P(AllContexts, PrivilegeMatrix, ::testing::ValuesIn(allCells()));

TEST(PrivilegeProperties, ARemoteCallerNeverInheritsALocalCallersPrivileges) {
  for (Principal p : {P_ADMIN, P_ADMIN_DEFAULT, P_GUEST, P_CONTACT, P_MQTT, P_MQTT_UNVERIFIED}) {
    for (bool hardened : {false, true}) {
      for (uint8_t cls : {C_SECRET, C_POSTURE, C_DESTRUCTIVE}) {
        EXPECT_NE(tableCell(p, hardened, cls), R_ALLOW) << "principal " << p << " class " << (int)cls << " hardened " << hardened;
      }
    }
  }
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
  EXPECT_EQ(decide(Context(T_BLE, A_PAIRED_DEFAULT_PIN, 0), C_READ).caller, Posture::CALLER_NET);   // a default PIN is not a phone
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
  EXPECT_EQ(resolve("set posture hardened", ROLE_COMPANION).flags, 0);
  EXPECT_EQ(resolve("set posture   hardened", ROLE_COMPANION).flags, 0);       // the handler skips spaces too
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
