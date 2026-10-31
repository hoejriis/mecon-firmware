#include "MqttWsClient.h"
#include <Arduino.h>
#include <string.h>

bool MqttWsClient::begin(Client& in, const char* host, uint16_t port, bool tls, const char* path, const uint8_t key[16],
                         uint32_t timeout_ms, MqttWs::Answer* why) {
  _in = &in;
  _open = false;
  _dec = MqttWs::Decoder();
  _rx_at = _rx_len = 0;
  _mask = (uint32_t)key[0] << 24 | (uint32_t)key[1] << 16 | (uint32_t)key[2] << 8 | key[3];
  if (!_mask) _mask = 0x9E3779B9;
  *why = MqttWs::Answer::Malformed;
  char key_text[25];
  MqttWs::keyText(key, key_text);
  char req[320];
  const size_t n = MqttWs::request(req, sizeof(req), host, port, tls, path, key_text);
  if (!n || in.write(reinterpret_cast<const uint8_t*>(req), n) != n) return false;
  // The answer, a byte at a time so that nothing after its empty line is taken: the first frame is the decoder's.
  char ans[512];
  size_t len = 0;
  const uint32_t start = millis();
  while (len < 4 || memcmp(ans + len - 4, "\r\n\r\n", 4) != 0) {
    if (len == sizeof(ans)) return false;
    if ((uint32_t)(millis() - start) >= timeout_ms || !in.connected()) return false;
    if (in.available() <= 0) { delay(5); continue; }
    const int c = in.read();
    if (c < 0) continue;
    ans[len++] = (char)c;
  }
  *why = MqttWs::checkAnswer(ans, len, key_text);
  if (*why != MqttWs::Answer::Ok) return false;
  _open = true;
  return true;
}

void MqttWsClient::nextMask(uint8_t m[4]) {
  _mask ^= _mask << 13;
  _mask ^= _mask >> 17;
  _mask ^= _mask << 5;
  m[0] = (uint8_t)(_mask >> 24); m[1] = (uint8_t)(_mask >> 16); m[2] = (uint8_t)(_mask >> 8); m[3] = (uint8_t)_mask;
}

bool MqttWsClient::sendFrame(uint8_t op, const uint8_t* p, size_t n) {
  if (!_in) return false;
  uint8_t mask[4];
  nextMask(mask);
  uint8_t buf[14 + 128];
  size_t at = MqttWs::frameHeader(buf, op, n, mask);
  for (size_t i = 0; i < n; i++) {   // masked in pieces, so a long write needs no buffer of its own
    buf[at++] = p[i] ^ mask[i & 3];
    if (at == sizeof(buf) || i + 1 == n) {
      if (_in->write(buf, at) != at) return false;
      at = 0;
    }
  }
  if (at && _in->write(buf, at) != at) return false;   // a frame with no payload
  return true;
}

size_t MqttWsClient::write(const uint8_t* buf, size_t size) {
  if (!_open || !size) return 0;
  if (!sendFrame(MqttWs::OP_BINARY, buf, size)) { _open = false; return 0; }
  return size;
}

void MqttWsClient::fail(uint16_t code) {
  if (_open) {
    const uint8_t c[2] = {(uint8_t)(code >> 8), (uint8_t)code};
    sendFrame(MqttWs::OP_CLOSE, c, 2);
  }
  _open = false;
}

void MqttWsClient::pump() {
  if (!_open || !_in) return;
  if (_rx_at == _rx_len) _rx_at = _rx_len = 0;
  while (_rx_len < RX && _in->available() > 0) {
    const int c = _in->read();
    if (c < 0) break;
    switch (_dec.push((uint8_t)c)) {
      case MqttWs::Decoder::More:
      case MqttWs::Decoder::Pong:
        break;
      case MqttWs::Decoder::Data:
        _rx[_rx_len++] = (uint8_t)c;
        break;
      case MqttWs::Decoder::Ping:
        if (!sendFrame(MqttWs::OP_PONG, _dec.ctrl(), _dec.ctrlLen())) { _open = false; return; }
        break;
      case MqttWs::Decoder::Close: {
        // Answer with the server's status code, as RFC 6455 section 5.5.1 asks, and end.
        const uint8_t n = _dec.ctrlLen() >= 2 ? 2 : 0;
        sendFrame(MqttWs::OP_CLOSE, _dec.ctrl(), n);
        _open = false;
        return;
      }
      case MqttWs::Decoder::Error:
        fail(1002);   // protocol error
        return;
    }
  }
}

int MqttWsClient::available() {
  if (_rx_at == _rx_len) pump();
  return (int)(_rx_len - _rx_at);
}

int MqttWsClient::read() {
  if (available() <= 0) return -1;
  return _rx[_rx_at++];
}

int MqttWsClient::read(uint8_t* buf, size_t size) {
  size_t got = 0;
  while (got < size && available() > 0) {
    const size_t k = (_rx_len - _rx_at) < (size - got) ? (_rx_len - _rx_at) : (size - got);
    memcpy(buf + got, _rx + _rx_at, k);
    _rx_at += k;
    got += k;
  }
  return got ? (int)got : -1;
}

int MqttWsClient::peek() {
  if (available() <= 0) return -1;
  return _rx[_rx_at];
}

void MqttWsClient::stop() {
  fail(1000);   // normal closure
  if (_in) _in->stop();
  _rx_at = _rx_len = 0;
}

uint8_t MqttWsClient::connected() {
  if (_rx_at < _rx_len) return 1;   // what arrived before an end is still the caller's to read
  return (_open && _in && _in->connected()) ? 1 : 0;
}
