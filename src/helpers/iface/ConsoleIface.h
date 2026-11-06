#pragma once
// The device interface on the Repeater and Room server console (docs/contract/DEVICE_INTERFACE.md section 7). A
// console line that starts with "mecon> " carries one request envelope; its result, and any event, goes back as one
// line "mecon> <envelope>". Every other line stays the stock CLI. The caller is the console: the cable, decided by
// the CLI privilege table like any console line. Where the build has a broker, the same operations answer over MQTT
// too (onMqtt), as that session's caller, with the session's own subscriptions, nonce and unlock state.
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
#include "helpers/updates/UpdService.h"
#include "LoraLink.h"
#include "Provenance.h"
#include "Streams.h"
#include "MgmtChannel.h"
#include "MgmtSlot.h"

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
    // Wall-clock seconds for the audit entries of the unlock handshake and for job events, 0 while the clock is not set
    // (RTCClock::knownTime); null: seconds since boot for the audit, and no time on an event.
    uint32_t (*clock_s)(void* user);
    // The node identity, to sign results and events with (AUTHORITY.md section 3.4); null: nothing is signed.
    const mesh::LocalIdentity* identity;
    // The job table's file (OPERATIONS.md section 3); null keeps the table in RAM only.
    JobStore* job_store;
    // The commit-or-revert snapshot's file (OPERATIONS.md 3.4); null: nothing is applied tentatively, so only the
    // cable may change the radio.
    SnapStore* snap_store;
    // identity.rotate (AUTHORITY.md section 11). rotate returns as rotationRun does; removed names what the role drops.
    int (*rotate)(void* user, const char* device_id_hex, uint32_t job_id, const char** err);
    bool (*read_rotation)(void* user, char* text, size_t tcap, char* sig_b64, size_t scap);
    const char* const* removed;   // null-terminated
    // The management channel (MANAGEMENT_CHANNEL.md section 6): its slot, binding record and pending change's snapshot.
    // Null: the role offers none.
    SnapStore* mgmt_slot;
    SnapStore* mgmt_record;
    SnapStore* mgmt_snap;
    // Where the build has Wi-Fi and a broker. read_secret: a write-only setting, for a confirm-or-revert snapshot only.
    // profile_exists: an MQTT profile is configured (the Deployment's broker roles). Null: no such settings.
    bool (*read_secret)(void* user, const KeyDef& k, unsigned idx, char* out, size_t cap);
    bool (*profile_exists)(void* user, uint8_t index);
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

  // The device interface over the broker (DEVICE_INTERFACE.md section 7): one request as the session's caller. Returns
  // the result's length (0: nothing to send).
  size_t onMqtt(const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap);
  // The LoRa link (LORA_MANAGEMENT.md): a complete message runs as the broker's does, as its own caller. The role's main
  // gives the way to send a reply.
  void setLoraSend(LoraLink::SendFn fn, void* user) {
    _lora_send = fn; _lora_send_user = user;
    loraSink().fn = onLoraFrame; loraSink().user = this;
    loraChannelSink().key = loraChannelKey; loraChannelSink().frame = onLoraChannelFrame; loraChannelSink().user = this;
  }
  LoraLink::Counts loraCounts() const { return _lora.counts(); }
  bool onLora() const { return _on_lora; }
  typedef bool (*EventPublishFn)(const char* stream, const char* payload, size_t len);
  typedef bool (*SessionUpFn)();
  void setMqttEvents(EventPublishFn publish, SessionUpFn up) { _ev_publish = publish; _ev_up = up; }
  void setRolesListener(DeployAuth::RolesListener f, void* user) { _auth.setRolesListener(f, user); }
  // The connectivity stream (TELEMETRY.md): the links the role's main owns. Null: the stream is not produced.
  typedef void (*ConnWriteFn)(JsonWriter& w);
  typedef uint32_t (*ConnSignatureFn)();
  void setConnectivity(ConnWriteFn write, ConnSignatureFn signature) { _conn_write = write; _conn_sig = signature; }
  int computeMqtt(const Cli::Context& ctx, const char* msg, size_t len, char* out, size_t cap);

  // For the operations.
  const Host& host() const { return _host; }
  DeployAuth& auth() { return _auth; }
  Status rotateIdentity(OpContext& c);   // the operations identity.rotate and identity.rotation, and deployment.get's member
  bool writeRotation(JsonWriter& w);
  bool mayRead();
  JobService& jobs() { return _jobs; }
  Upd::UpdateService& updates() { return _updates; }
  // The channel nonce hello reports: the console and the broker session are one link each, and hold their own.
  const char* helloNonce() { return _auth.helloNonce(_on_mqtt ? _mqtt_nonce : _nonce); }
  const char* deviceIdHex() const { return _id_hex; }
  uint32_t epoch() const { return _epoch; }
  ConfigHost configHost();
  ChannelHost channelHost() { return _mgmt_slot.host(); }
  MgmtHost mgmtHost();
  SubSet& subs() { return _on_mqtt ? _mqtt_subs : _subs; }
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
  static bool updSigned(void* self);
  static void updReboot(void* self);
  static bool jobClasses(void* self, uint8_t cls);
  static void jobChanged(void* self, const Job& j);
  mesh::LocalIdentity* _old_id = nullptr;   // the key identity.rotate retired: signs that one result, then is wiped
  MgmtSlot _mgmt_slot;
  ChannelHost _chan_host = {};
  uint8_t _public_fp[MGMT_FP_BYTES] = {0};
  static bool mgmtSlotFp(void* user, int slot, uint8_t out[MGMT_FP_BYTES]);
  static void mgmtKeyFp(void* user, const uint8_t key[CHANNEL_SECRET_BYTES], uint8_t out[MGMT_FP_BYTES]);
  static bool mgmtReadSlot(void* user, int slot, char name[CHANNEL_NAME_MAX + 1], uint8_t key[CHANNEL_SECRET_BYTES]);
  ConfirmHost _confirm = {};
  uint32_t _reboot_at = 0;     // a revert restored a key, or an update activated or reverted an image (0: none)
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
  Upd::UpdateService _updates; // the update operations and their install (UPDATES.md sections 5 to 8)
  Cli::Context _ctx;           // the caller of the request being handled: the cable, or the signed principal on it
  const char* _deny = nullptr; // why authorize() refused, when it has a contract code for it
  char _nonce[33] = {0};       // the console's channel nonce (AUTHORITY.md section 7.1)
  LinkUnlock _unlock;          // the console's local unlock challenge and state (section 10). The console is the cable and
                               // has no link-drop signal: an unlock ends after 10 minutes or at a reboot.
  static bool consoleUnlocked(void* self);
  static bool confirmReadSecret(void* self, const KeyDef& k, unsigned idx, char* out, size_t cap);
  static bool authProfileExists(void* self, uint8_t index);
  // The broker session, while the build has one: one more link beside the console.
  static void onLoraFrame(void* user, const uint8_t* key, const uint8_t* secret, const uint8_t* path, uint8_t path_len,
                          const uint8_t* frame, size_t n);
  void loraTick(uint32_t now);
  LoraLink _lora;
  LoraLink::SendFn _lora_send = nullptr;
  void* _lora_send_user = nullptr;
  bool _on_lora = false, _lora_channel = false;
  static bool loraChannelKey(void* user, uint8_t out[16]);
  static void onLoraChannelFrame(void* user, const uint8_t* data, size_t n);
  uint8_t _lora_key[16] = {0};   // the bound channel's key, read again every few seconds: the mesh asks on each group packet
  bool _lora_key_ok = false;
  uint32_t _lora_key_at = 0;
  bool _lora_key_read = false;
  bool _on_mqtt = false;       // the request being handled came over the broker
  Cli::Context _link_ctx;      // its transport's own context, before any signature replaced it
  char _mqtt_nonce[33] = {0};
  LinkUnlock _mqtt_unlock;     // the broker has no cable: its unlock state never leaves cleared (the handshake refuses)
  SubSet _mqtt_subs;
  bool _mqtt_may_read = false;
  EventPublishFn _ev_publish = nullptr;
  SessionUpFn _ev_up = nullptr;
  ConnWriteFn _conn_write = nullptr;
  ConnSignatureFn _conn_sig = nullptr;
  uint32_t _conn_last = 0;
  static uint32_t authClock(void* self);
  // One event to the console, unless a request is being typed, and to the broker while it is subscribed.
  void emit(int stream, const char* ev, size_t n);
  bool mqttSubscribed() const;
  // The stream's seconds, when the clock knows them.
  uint32_t knownSeconds(bool* known);
  SubSet _subs;
  StreamClock _clock;
  bool writeStream(int stream, JsonWriter& w);
  static void onObserve(void* self, float snr, float rssi, const uint8_t* raw, int len);
  ObsLimiter _obs;
  bool _env_ok = false, _env_checked = false;
  bool _loc_ok = false;   // the build has a location provider (the location stream)
  void refreshEnvironment(uint32_t now);
  uint32_t _env_at = 0;
  uint32_t streamsProduced() const {
    return STREAMS_PRODUCED | (_env_ok ? (1u << SID_ENVIRONMENT) : 0) | (_loc_ok ? (1u << SID_LOCATION) : 0) |
           (_conn_write ? (1u << SID_CONNECTIVITY) : 0);
  }
  LoopGap _gap;
  bool _boot_sent = false, _boot_pending = false;   // the runtime boot event: once per epoch
  LogFeed _logs;
  Print* _out = nullptr;   // the console, for events raised outside loop()
};

}  // namespace Iface
