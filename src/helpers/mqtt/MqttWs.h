#pragma once
// MQTT over WebSocket (RFC 6455, subprotocol "mqtt"): the parts that need no network, so they are unit-tested natively.
// The client that uses them (MqttWsClient.h) sits between PubSubClient and the connection MqttNet opened, after TLS and
// its anchor check, so a WebSocket session is exactly as authenticated as the TLS under it and no more.
//
// What this client accepts from a server is deliberately narrow: binary data frames (fragmented or not), ping, pong
// and close. A text frame, a masked frame, a reserved bit, an unknown opcode, a control frame that is fragmented or
// longer than 125 bytes, or a data frame that breaks the fragmentation order is a protocol error, and the connection
// ends.
#include <stddef.h>
#include <stdint.h>

namespace MqttWs {

// SHA-1 (needed only for Sec-WebSocket-Accept; nothing here relies on it for security).
void sha1(const uint8_t* data, size_t len, uint8_t out[20]);
// Standard base64 with padding. `out` holds at least 4 * ceil(len / 3) + 1 bytes; returns the length written.
size_t base64(const uint8_t* data, size_t len, char* out);

// The Sec-WebSocket-Key for 16 random bytes (24 characters and a NUL), and the Sec-WebSocket-Accept a server must
// answer it with (28 characters and a NUL).
void keyText(const uint8_t raw[16], char out[25]);
void acceptFor(const char* key_text, char out[29]);

// The upgrade request. `host` is the broker's host name, or its address as dotted digits; the port is added to the Host
// header only when it is not the default for the scheme (80, or 443 with TLS). Returns the length, or 0 when it does
// not fit in `cap`.
size_t request(char* out, size_t cap, const char* host, uint16_t port, bool tls, const char* path,
               const char* key_text);

// The server's answer, up to and including the empty line. Ok only for "HTTP/1.1 101" with Upgrade: websocket, a
// Connection that carries the upgrade token, the Sec-WebSocket-Accept the key calls for and Sec-WebSocket-Protocol:
// mqtt, and no extension (none was offered). Header names and the Upgrade and Connection values match without case.
enum class Answer : uint8_t { Ok, Status, Upgrade, Accept, Protocol, Extension, Malformed };
Answer checkAnswer(const char* text, size_t len, const char* key_text);
const char* answerName(Answer a);

// The header of one client frame: FIN, the binary opcode, the mask bit, the length and the masking key. Returns the
// header's length (6, 8 or 14 bytes); `out` holds at least 14.
size_t frameHeader(uint8_t* out, uint8_t opcode, uint64_t len, const uint8_t mask[4]);
const uint8_t OP_CONT = 0x0, OP_TEXT = 0x1, OP_BINARY = 0x2, OP_CLOSE = 0x8, OP_PING = 0x9, OP_PONG = 0xA;

// The server's frames, one byte at a time. push() says what the byte was: part of a header (More), a byte of MQTT data
// (Data: the byte is the caller's), or the last byte of a control frame, whose payload is then in ctrl()/ctrlLen()
// (Ping, Pong, Close). Error is final: the decoder stays in it.
class Decoder {
public:
  enum Event : uint8_t { More, Data, Ping, Pong, Close, Error };
  Event push(uint8_t b);
  const uint8_t* ctrl() const { return _ctrl; }
  uint8_t ctrlLen() const { return _ctrl_len; }
  bool failed() const { return _state == S_ERROR; }

private:
  enum State : uint8_t { S_H0, S_H1, S_LEN, S_DATA, S_CTRL, S_ERROR };
  Event endFrame();
  Event startPayload();

  State _state = S_H0;
  uint8_t _op = 0;
  bool _fin = false;
  bool _in_message = false;   // a fragmented data message has begun and not ended
  uint8_t _len_left = 0;      // extended length bytes still to read
  uint64_t _left = 0;         // payload bytes still to read in this frame
  uint8_t _ctrl[125];
  uint8_t _ctrl_len = 0;
};

}  // namespace MqttWs
