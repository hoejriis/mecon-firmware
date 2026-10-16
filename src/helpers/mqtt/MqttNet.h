#pragma once
// The network under the MQTT controller: link state, DNS, a TCP or TLS connection, and an asynchronous mDNS browse.
// Implemented by MqttNetEsp32.cpp for ESP32 boards and by variants/sim/sim_mqtt_net.cpp for the simulated board.
// Everything above this interface (profiles, selection, the MQTT protocol through PubSubClient) is shared.
#include <Arduino.h>
#include <Client.h>
#include <stdint.h>

struct MqttResponder {
  uint32_t addr;     // IPv4, host order
  uint16_t port;
  bool secure;       // found as _secure-mqtt._tcp (TLS); false: _mqtt._tcp (plain)
  char site[24];     // the TXT "site" value, "" when absent
};

enum class MqttOpen : uint8_t { Ok, Tcp, Tls, Pin };

class MqttNet {
public:
  virtual ~MqttNet() { }
  virtual void init(const char* node_id) { (void)node_id; }
  virtual bool linkUp() = 0;
  // Blocking, bounded by the platform's DNS timeout. IPv4 only.
  virtual bool resolve(const char* host, uint32_t& addr) = 0;
  // Asynchronous browse of _secure-mqtt._tcp and _mqtt._tcp. browseBegin() returns at once; browsePoll() returns -1
  // while the browse runs, then the number of responders written to `out` (at most `max`). The mDNS responder is
  // initialised on the first browse and stays initialised.
  virtual void browseBegin() = 0;
  virtual int browsePoll(MqttResponder* out, int max) = 0;
  // Open the transport to addr:port, blocking for at most about timeout_ms. With `tls`, the peer certificate's SHA-256
  // fingerprint must equal `anchor_hex` (64 hex digits) when one is given; without one the connection is encrypted
  // but the broker is not authenticated.
  virtual MqttOpen open(bool tls, uint32_t addr, uint16_t port, const char* anchor_hex, uint32_t timeout_ms) = 0;
  virtual Client& client() = 0;   // the open connection, for PubSubClient
  virtual void close() = 0;
};

MqttNet& mqttPlatformNet();
