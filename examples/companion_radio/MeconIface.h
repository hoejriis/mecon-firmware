#pragma once
// The device interface on the Companion (docs/contract/DEVICE_INTERFACE.md): CMD_MECON (0x7F) frames over USB and
// BLE are reassembled per transport, dispatched through the interface core (src/helpers/iface/) with the caller's
// real context, and the results are queued per transport and handed out one frame per loop pass.
#include <stddef.h>
#include <stdint.h>
#include <helpers/iface/IfaceDispatch.h>
#include <helpers/iface/IfaceConfig.h>
#include <helpers/iface/IfaceFrames.h>
#include <helpers/iface/Streams.h>
#include <helpers/cli/CliPolicy.h>
#include <helpers/iface/DeployAuth.h>

class MyMesh;
class BaseSerialInterface;
class DataStore;

// The Companion's DataStore as the Deployment record's file store (the shared layer in helpers/iface/DeployAuth).
class DataStoreCopies : public Dep::CopyStore {
public:
  void set(DataStore* s) { _s = s; }
  bool read(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, Dep::StoreFile which) override;
  bool write(int slot, const uint8_t* frame, size_t len, Dep::StoreFile which) override;
  bool removeBoth(int newer_slot, Dep::StoreFile which) override;
private:
  DataStore* _s = nullptr;
};

#define CMD_MECON 0x7F   // the one Companion code the interface uses, both directions

class MeconIface {
public:
  void begin(MyMesh* mesh, const uint8_t device_id[16], uint32_t epoch);
  // One CMD_MECON frame from the transport the current command arrived on (called while the reply is bound to it).
  void onFrame(const BaseSerialInterface* src, const uint8_t* frame, size_t len, uint32_t now);
  // Hands out pending frames, at most one per transport per call, and drops stale partial messages.
  void loop(uint32_t now);
  // One request envelope from the MQTT session (its context built by the controller); writes the result envelope and
  // returns its length.
  size_t onMqtt(const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap);
  // How events reach the MQTT session (set by main.cpp on MQTT builds): publish one event envelope on the stream's
  // topic, and whether a session is up. A subscription made over MQTT lapses with the session.
  typedef bool (*EventPublishFn)(const char* stream, const char* payload, size_t len);
  typedef bool (*SessionUpFn)();
  void setMqttEvents(EventPublishFn publish, SessionUpFn up) { _ev_publish = publish; _ev_up = up; }

  // Subscriptions (Streams.h, TELEMETRY.md): one set per transport, and one for the MQTT session.
  static const uint32_t STREAMS_PRODUCED = (1u << Iface::SID_HEALTH) | (1u << Iface::SID_RADIO) |
                                           (1u << Iface::SID_POWER) | (1u << Iface::SID_RUNTIME) |
                                           (1u << Iface::SID_OBSERVATIONS) | (1u << Iface::SID_INVENTORY);
  Iface::SubSet* currentSubs();   // the set of the transport whose request is being handled
  // The connectivity stream, on builds with a network (set by main.cpp, which owns the controllers): write writes the
  // stream's data; signature changes whenever a link changes state, which sends an event at once.
  typedef void (*ConnWriteFn)(Iface::JsonWriter& w);
  typedef uint32_t (*ConnSignatureFn)();
  void setConnectivity(ConnWriteFn write, ConnSignatureFn signature) {
    _conn_write = write;
    _conn_sig = signature;
  }
  uint32_t streamsProduced() const {
    return STREAMS_PRODUCED | (_conn_write ? (1u << Iface::SID_CONNECTIVITY) : 0) |
           (_env_ok ? (1u << Iface::SID_ENVIRONMENT) : 0);
  }
  void scheduleStream(int stream, uint32_t now) { _clock.scheduleNow(stream, now); }

  MyMesh* mesh() const { return _mesh; }
  Iface::ConfigHost configHost() { return Iface::ConfigHost{cliRun, cliAllow, this}; }
  const char* deviceIdHex() const { return _id_hex; }
  uint32_t epoch() const { return _epoch; }

  // ---- the Deployment authority layer (docs/contract/AUTHORITY.md; MeconDeploy.cpp)
  // Loads the record and its sequence marks from storage; called once from begin(). No record leaves a standalone device
  // with no DepState at all.
  void deployLoad();
  // The channel nonce hello reports on the current link: empty when the device holds no Deployment record.
  const char* helloNonce();
  // status.deployment: nothing for a device with no record.
  void writeDeploymentStatus(Iface::JsonWriter& w, bool hardened);
  static Iface::Status opDeployGet(Iface::OpContext& c);
  static Iface::Status opDeployProvision(Iface::OpContext& c);
  static Iface::Status opDeployUpdate(Iface::OpContext& c);
  static Iface::Status opDeployReset(Iface::OpContext& c);
  // Whether the device holds a Deployment record, healthy or corrupt: an unsigned caller then changes nothing
  // (AUTHORITY.md section 9). The CLI privilege table takes it as Context.enrolled.
  bool enrolled() const { return _auth.enrolled(); }
  // After a factory reset has formatted the storage: the provisioned marker goes with the record, and the state in
  // memory follows what is now stored.
  void onFactoryReset() { _auth.onFactoryReset(); }
  // The rescue console's rebuild must not unenrol (section 9, 11): the record's two copies, the marks' two copies and
  // the provisioned marker are read before it, and written back after. A null snapshot means something exists that
  // cannot be read; the rebuild is then refused.
  typedef Iface::DeployAuth::RecordSnapshot RecordSnapshot;
  RecordSnapshot* snapshotRecord() { return _auth.snapshotRecord(); }
  bool restoreRecord(RecordSnapshot* s) { return _auth.restoreRecord(s); }
  static void freeSnapshot(RecordSnapshot* s) { Iface::DeployAuth::freeSnapshot(s); }

  static const size_t QUEUE_BYTES = 2048;   // per transport, allocated on its first message
  static const size_t RESULT_MAX = 1536;    // largest result an operation writes in one piece (pages above that)

private:
  struct Link {
    const BaseSerialInterface* src = nullptr;
    Iface::Reassembler rx;
    Iface::OutQueue* q = nullptr;
    Iface::SubSet subs;
    char nonce[33] = {0};                   // the channel nonce issued for this link (AUTHORITY.md section 7.1)
  };
  static const int LINKS = 4;               // USB, BLE, TCP and one spare

  // Every operation is decided by the CLI privilege table with the caller's real context (design section 6): an
  // interface request never gets more than the same caller's CLI line would.
  static bool authorize(void* mesh, const Iface::OpEntry& op, const Iface::Request& req);
  static bool cliRun(void* self, const char* line, char* reply, size_t cap);
  static bool cliAllow(void* self, const char* line);
  Link* linkFor(const BaseSerialInterface* src, bool create);
  int dispatch(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out, size_t cap);
  int runDispatch(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out, size_t cap,
                  Iface::ResultCache* cache);
  int dispatchSigned(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out,
                     size_t cap);
  struct SignedRun;
  static int runSigned(void* user, const Cli::Context& ctx, const char* body, size_t len, char* out, size_t cap);
  static const char* denyReason(void* self);
  char* nonceOf(Link* link) { return link ? link->nonce : _mqtt_nonce; }
  bool queueResult(Link& l, const char* msg, size_t len);
  bool ensureQueue(Link& l);

  Link _links[LINKS];
  Iface::ResultCache* _cache = nullptr;     // one for the device (design section 3), allocated on the first request
  MyMesh* _mesh = nullptr;
  Cli::Context _ctx;                        // the caller of the request being handled (a transport, or the broker)
  Link* _cur_link = nullptr;                // its transport, or nullptr for the broker
  Iface::SubSet _mqtt_subs;
  EventPublishFn _ev_publish = nullptr;
  SessionUpFn _ev_up = nullptr;
  Iface::StreamClock _clock;                // per stream per device: seq (design section 3) and pacing
  void streamTick(uint32_t now);
  static void onObserve(void* self, float snr, float rssi, const uint8_t* raw, int len);
  Iface::ObsLimiter _obs;
  Iface::LoopGap _gap;
  ConnWriteFn _conn_write = nullptr;
  ConnSignatureFn _conn_sig = nullptr;
  uint32_t _conn_last = 0;
  bool _env_ok = false;          // the environment stream has something to report (sensors start after the mesh)
  bool _env_checked = false;
  void refreshEnvironment(uint32_t now);
  uint32_t _env_at = 0;
  bool writeStream(int stream, Iface::JsonWriter& w);
  uint8_t _caller = 0;                      // its audit identity (Posture::CALLER_*)
  Iface::DeployAuth _auth;                  // the record, its marks and the signed-request checks (shared with the servers)
  DataStoreCopies _copies;
  const char* _deny = nullptr;              // why authorize() refused, when it has a contract code for it
  char _mqtt_nonce[33] = {0};               // the MQTT session is one link
  char _id_hex[33] = {0};
  uint32_t _epoch = 0;
};
