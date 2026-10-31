#include "MqttWs.h"
#include <stdio.h>
#include <string.h>

namespace MqttWs {

namespace {

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void sha1Block(uint32_t h[5], const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    const uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";   // RFC 6455 section 1.3

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool eqNoCase(const char* a, size_t an, const char* b) {
  const size_t bn = strlen(b);
  if (an != bn) return false;
  for (size_t i = 0; i < an; i++) if (lower(a[i]) != lower(b[i])) return false;
  return true;
}

// True when the comma-separated list holds `token` (without case), as Connection must hold "upgrade".
bool hasToken(const char* v, size_t n, const char* token) {
  size_t i = 0;
  while (i < n) {
    while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == ',')) i++;
    size_t j = i;
    while (j < n && v[j] != ',') j++;
    size_t e = j;
    while (e > i && (v[e - 1] == ' ' || v[e - 1] == '\t')) e--;
    if (e > i && eqNoCase(v + i, e - i, token)) return true;
    i = j;
  }
  return false;
}

}  // namespace

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  size_t i = 0;
  for (; i + 64 <= len; i += 64) sha1Block(h, data + i);
  uint8_t tail[128];
  const size_t rest = len - i;
  memcpy(tail, data + i, rest);
  tail[rest] = 0x80;
  const size_t total = rest + 1 + 8 <= 64 ? 64 : 128;
  memset(tail + rest + 1, 0, total - rest - 1);
  const uint64_t bits = (uint64_t)len * 8;
  for (int k = 0; k < 8; k++) tail[total - 1 - k] = (uint8_t)(bits >> (8 * k));
  sha1Block(h, tail);
  if (total == 128) sha1Block(h, tail + 64);
  for (int k = 0; k < 5; k++) {
    out[4 * k] = (uint8_t)(h[k] >> 24); out[4 * k + 1] = (uint8_t)(h[k] >> 16);
    out[4 * k + 2] = (uint8_t)(h[k] >> 8); out[4 * k + 3] = (uint8_t)h[k];
  }
}

size_t base64(const uint8_t* data, size_t len, char* out) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t v = (uint32_t)data[i] << 16 | (i + 1 < len ? (uint32_t)data[i + 1] << 8 : 0) | (i + 2 < len ? data[i + 2] : 0);
    out[o++] = A[(v >> 18) & 63];
    out[o++] = A[(v >> 12) & 63];
    out[o++] = i + 1 < len ? A[(v >> 6) & 63] : '=';
    out[o++] = i + 2 < len ? A[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

void keyText(const uint8_t raw[16], char out[25]) { base64(raw, 16, out); }

void acceptFor(const char* key_text, char out[29]) {
  char buf[24 + sizeof(GUID)];
  const size_t kn = strlen(key_text) < 24 ? strlen(key_text) : 24;
  memcpy(buf, key_text, kn);
  memcpy(buf + kn, GUID, sizeof(GUID) - 1);
  uint8_t d[20];
  sha1(reinterpret_cast<const uint8_t*>(buf), kn + sizeof(GUID) - 1, d);
  base64(d, 20, out);
}

size_t request(char* out, size_t cap, const char* host, uint16_t port, bool tls, const char* path,
               const char* key_text) {
  char port_text[8] = "";
  if (port != (tls ? 443 : 80)) snprintf(port_text, sizeof(port_text), ":%u", (unsigned)port);
  const int n = snprintf(out, cap,
                         "GET %s HTTP/1.1\r\nHost: %s%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: mqtt\r\n\r\n",
                         path, host, port_text, key_text);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

Answer checkAnswer(const char* text, size_t len, const char* key_text) {
  if (len < 4 || memcmp(text + len - 4, "\r\n\r\n", 4) != 0) return Answer::Malformed;
  const char* end = text + len - 2;   // the final empty line
  const char* eol = static_cast<const char*>(memchr(text, '\r', (size_t)(end - text)));
  if (!eol || eol[1] != '\n') return Answer::Malformed;
  static const char S[] = "HTTP/1.1 101";
  const size_t sl = (size_t)(eol - text);
  if (sl < sizeof(S) - 1 || memcmp(text, S, sizeof(S) - 1) != 0 || (sl > sizeof(S) - 1 && text[sizeof(S) - 1] != ' '))
    return Answer::Status;
  char want[29];
  acceptFor(key_text, want);
  bool upgrade = false, connection = false, accept = false, protocol = false, extension = false;
  for (const char* p = eol + 2; p < end;) {
    const char* e = static_cast<const char*>(memchr(p, '\r', (size_t)(end - p)));
    if (!e || e[1] != '\n') return Answer::Malformed;
    const char* colon = static_cast<const char*>(memchr(p, ':', (size_t)(e - p)));
    if (!colon || colon == p) return Answer::Malformed;
    const char* v = colon + 1;
    while (v < e && (*v == ' ' || *v == '\t')) v++;
    const char* ve = e;
    while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
    const size_t nn = (size_t)(colon - p), vn = (size_t)(ve - v);
    if (eqNoCase(p, nn, "upgrade")) upgrade = eqNoCase(v, vn, "websocket");
    else if (eqNoCase(p, nn, "connection")) connection = hasToken(v, vn, "upgrade");
    else if (eqNoCase(p, nn, "sec-websocket-accept")) accept = vn == 28 && memcmp(v, want, 28) == 0;
    else if (eqNoCase(p, nn, "sec-websocket-protocol")) protocol = vn == 4 && memcmp(v, "mqtt", 4) == 0;
    else if (eqNoCase(p, nn, "sec-websocket-extensions")) extension = true;
    p = e + 2;
  }
  if (!upgrade || !connection) return Answer::Upgrade;
  if (!accept) return Answer::Accept;
  if (!protocol) return Answer::Protocol;
  if (extension) return Answer::Extension;
  return Answer::Ok;
}

const char* answerName(Answer a) {
  switch (a) {
    case Answer::Ok: return "ok";
    case Answer::Status: return "status";
    case Answer::Upgrade: return "upgrade";
    case Answer::Accept: return "accept";
    case Answer::Protocol: return "protocol";
    case Answer::Extension: return "extension";
    case Answer::Malformed: return "malformed";
  }
  return "?";
}

size_t frameHeader(uint8_t* out, uint8_t opcode, uint64_t len, const uint8_t mask[4]) {
  out[0] = (uint8_t)(0x80 | opcode);
  size_t n;
  if (len <= 125) {
    out[1] = (uint8_t)(0x80 | len);
    n = 2;
  } else if (len <= 0xFFFF) {
    out[1] = 0x80 | 126;
    out[2] = (uint8_t)(len >> 8);
    out[3] = (uint8_t)len;
    n = 4;
  } else {
    out[1] = 0x80 | 127;
    for (int k = 0; k < 8; k++) out[2 + k] = (uint8_t)(len >> (8 * (7 - k)));
    n = 10;
  }
  memcpy(out + n, mask, 4);
  return n + 4;
}

Decoder::Event Decoder::push(uint8_t b) {
  switch (_state) {
    case S_H0: {
      _fin = (b & 0x80) != 0;
      _op = b & 0x0F;
      if (b & 0x70) break;   // a reserved bit: no extension was agreed
      if (_op >= 8) {
        if ((_op != OP_CLOSE && _op != OP_PING && _op != OP_PONG) || !_fin) break;
      } else if (_op == OP_BINARY) {
        if (_in_message) break;    // a new message inside a fragmented one
      } else if (_op == OP_CONT) {
        if (!_in_message) break;   // a continuation of nothing
      } else {
        break;                     // text (MQTT is binary) or a reserved data opcode
      }
      _state = S_H1;
      return More;
    }
    case S_H1: {
      if (b & 0x80) break;   // a server never masks
      const uint8_t l = b & 0x7F;
      if (_op >= 8 && l > 125) break;
      _left = 0;
      if (l == 126 || l == 127) {
        _len_left = l == 126 ? 2 : 8;
        _state = S_LEN;
        return More;
      }
      _left = l;
      return startPayload();
    }
    case S_LEN:
      _left = (_left << 8) | b;
      if (--_len_left) return More;
      if (_left >> 62) break;   // the most significant bit must be 0; nothing near this size is MQTT anyway
      return startPayload();
    case S_DATA:
      if (--_left == 0) endFrame();
      return Data;
    case S_CTRL:
      _ctrl[_ctrl_len++] = b;
      if (--_left == 0) return endFrame();
      return More;
    case S_ERROR:
      break;
  }
  _state = S_ERROR;
  return Error;
}

Decoder::Event Decoder::startPayload() {
  if (_op >= 8) {
    _ctrl_len = 0;
    if (_left == 0) return endFrame();
    _state = S_CTRL;
    return More;
  }
  if (_left == 0) return endFrame();
  _state = S_DATA;
  return More;
}

Decoder::Event Decoder::endFrame() {
  _state = S_H0;
  if (_op == OP_CLOSE) return Close;
  if (_op == OP_PING) return Ping;
  if (_op == OP_PONG) return Pong;
  _in_message = !_fin;   // a data frame: the message goes on until a frame with FIN
  return More;
}

}  // namespace MqttWs
