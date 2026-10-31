#pragma once
// The WebSocket connection PubSubClient talks through for a `ws` profile (docs/MQTT.md, "WebSocket"). It wraps the
// connection MqttNet opened, after TLS and its anchor check, runs the upgrade (MqttWs.h checks the answer), then turns
// each write into one masked binary frame and hands PubSubClient the data bytes of the frames that arrive. A ping is
// answered with a pong; a close, or any frame MqttWs::Decoder refuses, ends the connection.
//
// Masking keys come from a small generator seeded by the handshake key. RFC 6455 masks client frames so that a browser
// script cannot steer the bytes an intermediary sees; no script runs here, and the session's protection is the TLS
// under it, so the masks need not be unpredictable.
#include <Client.h>
#include "MqttWs.h"

class MqttWsClient : public Client {
public:
  // Runs the upgrade over `in`, which is open (and, with TLS, already authenticated): sends the request and reads the
  // answer, for at most `timeout_ms`. On success the client is connected; otherwise `why` says what the server
  // answered (Malformed covers no answer at all, an answer too long, and a timeout) and nothing is left open here.
  bool begin(Client& in, const char* host, uint16_t port, bool tls, const char* path, const uint8_t key[16],
             uint32_t timeout_ms, MqttWs::Answer* why);

  int connect(IPAddress, uint16_t) override { return 0; }   // begin() is the way in
  int connect(const char*, uint16_t) override { return 0; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t size) override;
  int peek() override;
  void flush() override { if (_in) _in->flush(); }
  void stop() override;   // sends a close (1000) if the connection is up, then stops the connection under it
  uint8_t connected() override;
  operator bool() override { return connected() != 0; }

private:
  static const size_t RX = 128;
  bool sendFrame(uint8_t op, const uint8_t* p, size_t n);
  void pump();          // reads what the connection has, up to a full buffer
  void fail(uint16_t code);
  void nextMask(uint8_t m[4]);

  Client* _in = nullptr;
  bool _open = false;
  MqttWs::Decoder _dec;
  uint8_t _rx[RX];
  size_t _rx_at = 0, _rx_len = 0;
  uint32_t _mask = 0;
};
