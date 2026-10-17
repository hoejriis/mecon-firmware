#pragma once
// MQTT controller for the Companion `_wifi` builds: the thin platform layer over MqttCore (the decisions), MqttNet (the
// network) and PubSubClient (the MQTT 3.1.1 protocol). One broker session at a time. It asks the Wi-Fi controller's
// link state (through MqttNet) and never joins a network itself. It carries a retained status topic and the command
// line over MQTT (docs/CLI.md): a request topic it subscribes to and a response topic it publishes on. Who may run
// what is the engine's table; this class only frames the exchange and says how the session was authenticated.
// MECON services are separate work.
//
// A connect attempt (DNS, the TCP connect, the TLS handshake, the MQTT CONNECT and its CONNACK; each bounded, about 6 s
// for the transport and 6 s for the CONNACK) runs as a job on the platform's worker (MqttNet::asyncStart), never on the
// main loop. The loop only starts it, polls for it and takes the result. While the job runs it owns the transport and
// PubSubClient; the main loop touches neither, and a cancellation (profile edit, posture, link loss, disable) only
// raises a flag that the job honours between its steps, so no client is freed under it and none is leaked. Once the
// session is up, PubSubClient::loop() and every publish stay on the main loop. The mDNS browse never blocks.
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include "MqttCore.h"
#include "MqttNet.h"
#include "../cli/CliMqtt.h"
#if defined(SIM_FREE_HEAP) && defined(ESP32)
  // The simulator builds libraries without ESP32 (variants/sim/platformio.ini) and PubSubClient picks its callback type
  // (std::function or a plain pointer) by that macro. Read the header here as the library was built, so setCallback()
  // links and the object has the layout the library expects.
  #pragma push_macro("ESP32")
  #undef ESP32
  #include <PubSubClient.h>
  #pragma pop_macro("ESP32")
#else
  #include <PubSubClient.h>
#endif

class MqttController {
public:
  // `profiles` and `posture` must outlive the controller. `node_id` (8 hex digits) names the client and the topic.
  void begin(MqttProfiles* profiles, MqttNet* net, const uint8_t* posture, const char* node_id,
             const MqttTiming& timing = MqttTiming());
  void loop();                                              // call from the main loop
  void profileChanged(uint8_t index, bool credentials);     // a profile was edited; applied at once, no reboot
  void setEnabled(bool enabled);
  bool started() const { return _started; }
  // The command line over MQTT: `fn` runs a request for the role (it is given the context built from the session).
  // Without a handler nothing is subscribed.
  void setCliHandler(Cli::Mqtt::RunFn fn, void* user) { _cli_fn = fn; _cli_user = user; }
  const MqttCore* core() const { return &_core; }
  // Seconds clock for the audit entry of a refused profile (a stored anchor that cannot be used); without one, 0.
  void setAuditClock(uint32_t (*now)()) { _audit_now = now; }

  // One status line: state, profile, broker address, TLS state, time connected. Never contains a credential.
  void formatStatus(char* out, size_t cap);
  // The retained status topic ("mecon/<node id>/status") and payload.
  const char* statusTopic() const { return _topic; }

private:
  MqttCore _core;
  MqttProfiles* _profiles = nullptr;
  MqttNet* _net = nullptr;
  const uint8_t* _posture = nullptr;
  PubSubClient _psc;
  bool _started = false;
  bool _enabled = false;
  bool _open = false;                // a transport is open and PubSubClient points at it

  // One connect attempt. `req` is a copy of everything the job needs, taken on the main loop, so a profile edited while
  // the job runs is not read by it. The job writes `result` and the main loop reads it after MqttNet::asyncIdle().
  struct ConnectJob {
    enum Result : uint8_t { Ok, Dns, Tcp, Tls, Pin, Auth, Timeout, Cancelled };
    bool resolve;                       // `host` is a name to resolve now (every attempt), not an address
    char host[MqttProfile::HOST_BUF];
    uint32_t addr;                      // host order; the resolved address once the job has run
    uint16_t port;
    bool tls;
    bool has_anchor;
    char anchor[MqttProfile::ANCHOR_BUF];   // leaf fingerprint, "" when the anchor is a CA
    char* ca;                           // a private copy of the CA certificate (base64 DER), freed by takeResult()
    char name[MqttProfile::HOST_BUF];   // the broker's host name for SNI and the name check, "" when it has none
    char user[MqttProfile::USER_BUF];   // "" when none
    char pwd[MqttProfile::PWD_BUF];
    Result result;
  };
  ConnectJob _job;
  bool _job_active = false;          // a job was started and its result has not been taken yet (main loop only)
  uint8_t _job_profile = 0;          // the profile the job connects, for the posture check
  std::atomic<bool> _job_cancel{false};   // set by the main loop, read by the job between its steps
  char _client_id[24] = {0};
  char _topic[32] = {0};
  char _req_topic[40] = {0};
  char _resp_topic[40] = {0};
  Cli::Mqtt::RunFn _cli_fn = nullptr;
  void* _cli_user = nullptr;
  uint32_t (*_audit_now)() = nullptr;
  uint32_t _cur_addr = 0;
  uint16_t _cur_port = 0;
  uint32_t _next_status = 0;
  uint32_t _cli_open_at = 0;         // requests before this are ignored: a retained one is replayed to every new subscriber
  void attempt(const MqttAction& a);
  void takeResult();
  void cancelAttempt();
  void runConnect();                 // the job body, on the worker
  static void connectEntry(void* self);
  void closeSession();
  void dropTransport();
  void publishStatus();
  void onMessage(const char* topic, const uint8_t* payload, unsigned len);
  static void trampoline(char* topic, uint8_t* payload, unsigned int len);
};

MqttController& mqttController();
