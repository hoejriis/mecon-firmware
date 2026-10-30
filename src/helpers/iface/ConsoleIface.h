#pragma once
// The device interface on the Repeater and Room server console (docs/contract/DEVICE_INTERFACE.md section 7). A
// console line that starts with "mecon> " carries one request envelope; its result, and any event, goes back as one
// line "mecon> <envelope>". Every other line stays the stock CLI. The caller is the console: the cable, decided by
// the CLI privilege table like any console line.
//
// The stock console reads at most 160 characters and echoes each one. Once a line has shown the prefix, this class
// takes the rest of it without echo, into its own buffer of MAX_LINE bytes, allocated only while such a line is
// arriving. A longer line is refused with busy (its id unread) and never truncated.
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "DeployAuth.h"
#include "IfaceConfig.h"
#include "IfaceDispatch.h"
#include "JobService.h"
#include "Provenance.h"
#include "Streams.h"

namespace Iface {

class ConsoleIface {
public:
  static const size_t MAX_LINE = 1600;   // the request bound MQTT already sets (section 7)
  static constexpr const char* PREFIX = "mecon> ";
  static const size_t PREFIX_LEN = 7;

  // What the role provides. run() executes one CLI line as the local console and returns false for an unknown line.
  struct Host {
    bool (*run)(void* user, const char* line, char* reply, size_t cap);
    uint8_t (*posture)(void* user);       // Posture value, for the CLI gate and status
    uint16_t (*battery_mv)(void* user);   // 0 when the board cannot measure it
    const char* role;                     // "repeater" or "room"
    const char* hardware;
    const uint8_t* node_key;              // 32 bytes
    void* user;
    // For the radio, power and runtime streams.
    mesh::Dispatcher* dispatcher;
    mesh::Radio* radio;
    mesh::MainBoard* board;
    void (*radio_counts)(void* user, RadioCounts* out);
    SensorManager* sensors;
    void (*inventory)(void* user, JsonWriter& w);   // the role's counts (TELEMETRY.md, inventory)
    // Where the Deployment record's files live (docs/contract/AUTHORITY.md section 4): the role's own file system. Null
    // leaves the device a standalone one that cannot be enrolled.
    Dep::CopyStore* dep_store;
    // Wall-clock seconds for the audit entries of the unlock handshake; null: seconds since boot.
    uint32_t (*clock_s)(void* user);
    // The node identity, to sign results and events with (AUTHORITY.md section 3.4); null: nothing is signed.
    const mesh::LocalIdentity* identity;
    // The job table's file (OPERATIONS.md section 3); null keeps the table in RAM only.
    JobStore* job_store;
    // The commit-or-revert snapshot's file (OPERATIONS.md 3.4); null: nothing is applied tentatively, so only the
    // cable may change the radio.
    SnapStore* snap_store;
  };
  static const uint32_t STREAMS_PRODUCED = (1u << SID_HEALTH) | (1u << SID_RADIO) | (1u << SID_POWER) |
                                           (1u << SID_RUNTIME) | (1u << SID_OBSERVATIONS) | (1u << SID_INVENTORY) |
                                           (1u << SID_LOGS);

  // epoch_saved: false when this boot's epoch could not be written, so it may repeat and nothing is signed.
  void begin(const Host& host, const uint8_t device_id[16], uint32_t epoch, bool epoch_saved);

  // The console has just read the prefix: the rest of the line is this class's.
  void startLine();
  bool collecting() const { return _collecting; }
  // One more character of the line being collected; at its end the request is answered on out.
  void feed(char c, Print& out);
  // Health events while a subscription runs.
  void loop(uint32_t now, Print& out);

  // For the operations.
  const Host& host() const { return _host; }
  DeployAuth& auth() { return _auth; }
  JobService& jobs() { return _jobs; }
  // The channel nonce hello reports: the console is one link, and holds its own.
  const char* helloNonce() { return _auth.helloNonce(_nonce); }
  const char* deviceIdHex() const { return _id_hex; }
  uint32_t epoch() const { return _epoch; }
  ConfigHost configHost();
  SubSet& subs() { return _subs; }
  uint32_t produced() const { return streamsProduced(); }
  void scheduleStream(int stream, uint32_t now) {
    _clock.scheduleNow(stream, now);
    if (stream == SID_RUNTIME && !_boot_sent) _boot_pending = true;
  }
  static bool streamAllow(void* self, uint8_t cls);

private:
  static bool authorize(void* self, const OpEntry& op, const Request& req);
  static const char* denyReason(void* self);
  static int runSigned(void* user, const Cli::Context& ctx, const char* body, size_t len, char* out, size_t cap);
  int run(const Cli::Context& ctx, const char* msg, size_t len, char* out, size_t cap, ResultCache* cache);
  static bool cliRun(void* self, const char* line, char* reply, size_t cap);
  // Commit-or-revert: the hooks config.apply and the job table call.
  static bool confirmCable(void* self);
  static uint8_t confirmLineClass(void* self, const char* line);
  static void confirmRecordCaller(void* self, Job& j);
  static bool confirmRunAs(void* self, const Job& j, const char* line, char* reply, size_t cap);
  static bool confirmJobId(void* self, const Request& req, char* out, size_t cap);
  static bool jobRevert(void* self, Job& j);
  static bool jobConfirm(void* self, Job& j);
  static bool jobClasses(void* self, uint8_t cls);
  static void jobChanged(void* self, const Job& j);
  ConfirmHost _confirm = {};
  uint32_t _reboot_at = 0;     // a revert restored a key that needs a reboot (0: none)
  static bool cliAllow(void* self, const char* line);
  void answer(Print& out);
public:
  // One request's work from the parse to the signed result (run on the nRF52 worker); returns the length written.
  int compute(char* res, char* small, size_t small_cap);
  static const size_t OUT_MAX = 1600;
private:
  void printLine(Print& out, const char* msg, size_t len);

  Host _host = {};
  char _id_hex[33] = {0};
  uint32_t _epoch = 0;
  char* _line = nullptr;       // allocated while a line is arriving
  size_t _len = 0;
  bool _collecting = false;
  bool _overflow = false;
  ResultCache* _cache = nullptr;
  Provenance _prov;            // signs every result and event while a record exists
  DeployAuth _auth;            // the Deployment record and the signed-request checks (shared with the Companion)
  JobService _jobs;            // config.apply with confirm creates the jobs (OPERATIONS.md 3.4)
  Cli::Context _ctx;           // the caller of the request being handled: the cable, or the signed principal on it
  const char* _deny = nullptr; // why authorize() refused, when it has a contract code for it
  char _nonce[33] = {0};       // the console's channel nonce (AUTHORITY.md section 7.1)
  LinkUnlock _unlock;          // the console's local unlock challenge and state (section 10). The console is the cable and
                               // has no link-drop signal: an unlock ends after 10 minutes or at a reboot.
  static bool consoleUnlocked(void* self);
  SubSet _subs;
  StreamClock _clock;
  bool writeStream(int stream, JsonWriter& w);
  static void onObserve(void* self, float snr, float rssi, const uint8_t* raw, int len);
  ObsLimiter _obs;
  bool _env_ok = false, _env_checked = false;
  void refreshEnvironment(uint32_t now);
  uint32_t _env_at = 0;
  uint32_t streamsProduced() const { return STREAMS_PRODUCED | (_env_ok ? (1u << SID_ENVIRONMENT) : 0); }
  LoopGap _gap;
  bool _boot_sent = false, _boot_pending = false;   // the runtime boot event: once per epoch
  LogFeed _logs;
  Print* _out = nullptr;   // the console, for events raised outside loop()
};

}  // namespace Iface
