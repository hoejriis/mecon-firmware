#pragma once
// MQTT controller for the Companion `_wifi` builds: the thin platform layer over MqttCore (the decisions), MqttNet (the
// network) and PubSubClient (the MQTT 3.1.1 protocol). One broker session at a time. It asks the Wi-Fi controller's
// link state (through MqttNet) and never joins a network itself. It carries a retained status topic and nothing else:
// the unified CLI over MQTT and MECON services are separate work.
//
// A connect attempt blocks the main loop for as long as DNS, the TCP connect, the TLS handshake and the CONNACK take,
// each bounded (about 6 s for the transport and 6 s for the CONNACK). The mDNS browse never blocks.
#include <stddef.h>
#include <stdint.h>
#include "MqttCore.h"
#include "MqttNet.h"
#include <PubSubClient.h>

class MqttController {
public:
  // `profiles` and `posture` must outlive the controller. `node_id` (8 hex digits) names the client and the topic.
  void begin(MqttProfiles* profiles, MqttNet* net, const uint8_t* posture, const char* node_id,
             const MqttTiming& timing = MqttTiming());
  void loop();                                              // call from the main loop
  void profileChanged(uint8_t index, bool credentials);     // a profile was edited; applied at once, no reboot
  void setEnabled(bool enabled);
  bool started() const { return _started; }
  const MqttCore* core() const { return &_core; }

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
  char _client_id[24] = {0};
  char _topic[32] = {0};
  uint32_t _cur_addr = 0;
  uint16_t _cur_port = 0;
  uint32_t _next_status = 0;
  void attempt(const MqttAction& a);
  void closeSession();
  void dropTransport();
  void publishStatus();
};

MqttController& mqttController();
