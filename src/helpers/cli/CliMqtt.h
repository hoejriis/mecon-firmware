#pragma once

// The MQTT adapter of the command-line engine (docs/CLI.md): topics, request parsing and response framing.
//
//   request   mecon/<node id>/cli/req    {"id":"<correlation id>","cmd":"<command line>"}
//   response  mecon/<node id>/cli/resp   {"id":"<same id>","status":"ok|refused|unknown|bad-request","ok":<bool>,"reply":"<text>"}
//
// This file only frames bytes. Who may run what is the engine's table: a request is run with a context built from the
// broker session (CliPolicy.h, principal "MQTT"): refused outright unless the session is TLS-verified, read-only in
// Hardened unless the broker profile is flagged for management, and never a secret, a posture change or a factory
// reset. Pure logic: no network, no clock, so it runs in the native unit tests.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "CliPolicy.h"

namespace Cli {
namespace Mqtt {

enum : int { ID_MAX = 32, CMD_MAX = 159 };   // characters, without the terminator

enum Status : uint8_t { S_OK = 0, S_REFUSED, S_UNKNOWN, S_BAD_REQUEST };

inline const char* statusName(uint8_t s) {
  switch (s) {
    case S_OK: return "ok";
    case S_REFUSED: return "refused";
    case S_UNKNOWN: return "unknown";
    default: return "bad-request";
  }
}

// A request is run by the role's handler: `cmd` in, `reply` out (at most `cap` bytes with the NUL). Returns a Status.
typedef uint8_t (*RunFn)(void* user, const Context& ctx, const char* cmd, char* reply, size_t cap);

inline void requestTopic(char* out, size_t cap, const char* node_id) { snprintf(out, cap, "mecon/%s/cli/req", node_id); }
inline void responseTopic(char* out, size_t cap, const char* node_id) { snprintf(out, cap, "mecon/%s/cli/resp", node_id); }

// The context of a request: the broker session decides the authentication, the profile decides management.
inline Context sessionContext(bool tls_verified, uint8_t posture, bool profile_manages) {
  return Context(T_MQTT, tls_verified ? A_TLS_VERIFIED : A_TLS_UNVERIFIED, posture, profile_manages);
}

struct Request {
  char id[ID_MAX + 1];
  char cmd[CMD_MAX + 1];
};

namespace detail {

inline bool idChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' ||
         c == ':' || c == '-';
}

inline int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

struct Cursor {
  const uint8_t* p;
  const uint8_t* end;
  bool more() const { return p < end; }
  void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
};

// Decode a JSON string (the opening quote is under the cursor) into `out`, at most `cap - 1` bytes. False when the
// string is malformed, does not fit, or contains a control character once decoded.
inline bool readString(Cursor& c, char* out, size_t cap) {
  if (!c.more() || *c.p != '"') return false;
  c.p++;
  size_t n = 0;
  auto put = [&](unsigned ch) -> bool {
    if (ch < 0x20 || ch == 0x7f) return false;
    if (ch < 0x80) { if (n + 1 >= cap) return false; out[n++] = (char)ch; return true; }
    if (ch < 0x800) {
      if (n + 2 >= cap) return false;
      out[n++] = (char)(0xC0 | (ch >> 6)); out[n++] = (char)(0x80 | (ch & 0x3F));
      return true;
    }
    if (n + 3 >= cap) return false;
    out[n++] = (char)(0xE0 | (ch >> 12)); out[n++] = (char)(0x80 | ((ch >> 6) & 0x3F)); out[n++] = (char)(0x80 | (ch & 0x3F));
    return true;
  };
  while (c.more()) {
    unsigned char ch = *c.p++;
    if (ch == '"') { out[n] = 0; return true; }
    if (ch == '\\') {
      if (!c.more()) return false;
      char e = (char)*c.p++;
      unsigned v;
      switch (e) {
        case '"': v = '"'; break;
        case '\\': v = '\\'; break;
        case '/': v = '/'; break;
        case 'b': case 'f': case 'n': case 'r': case 't': return false;   // a control character in a command line
        case 'u': {
          if (c.end - c.p < 4) return false;
          v = 0;
          for (int i = 0; i < 4; i++) {
            int h = hexv((char)c.p[i]);
            if (h < 0) return false;
            v = v * 16 + (unsigned)h;
          }
          c.p += 4;
          if (v >= 0xD800 && v <= 0xDFFF) return false;   // surrogate pairs are not accepted
          break;
        }
        default: return false;
      }
      if (!put(v)) return false;
      continue;
    }
    if (ch < 0x20 || ch == 0x7f) return false;   // a raw control character
    if (n + 1 >= cap) return false;
    out[n++] = (char)ch;
  }
  return false;   // no closing quote
}

// Skip a value of a key we do not use: a string, number, true, false or null. Objects and arrays are not accepted.
inline bool skipValue(Cursor& c) {
  if (!c.more()) return false;
  if (*c.p == '"') {
    char tmp[CMD_MAX + 1];
    return readString(c, tmp, sizeof(tmp));
  }
  const uint8_t* start = c.p;
  while (c.more() && ((*c.p >= '0' && *c.p <= '9') || (*c.p >= 'a' && *c.p <= 'z') || *c.p == '-' || *c.p == '+' || *c.p == '.' || *c.p == 'E'))
    c.p++;
  return c.p > start;
}

}  // namespace detail

// Parse a request payload. True when it carries a valid correlation id and a command line; a request that is
// anything else (bad JSON, a missing or duplicated field, a control character, a nested value) is refused as a whole.
inline bool parseRequest(const uint8_t* payload, size_t len, Request& out) {
  using namespace detail;
  Cursor c{payload, payload + len};
  bool have_id = false, have_cmd = false;
  c.ws();
  if (!c.more() || *c.p != '{') return false;
  c.p++;
  c.ws();
  if (c.more() && *c.p == '}') return false;
  for (;;) {
    c.ws();
    char key[16];   // keys of other fields are skipped; a key of 16 characters or more is not accepted
    if (!readString(c, key, sizeof(key))) return false;
    c.ws();
    if (!c.more() || *c.p != ':') return false;
    c.p++;
    c.ws();
    if (strcmp(key, "id") == 0) {
      if (have_id) return false;
      char id[ID_MAX + 2];
      if (!readString(c, id, sizeof(id))) return false;
      size_t n = strlen(id);
      if (n == 0 || n > (size_t)ID_MAX) return false;
      for (size_t i = 0; i < n; i++) if (!idChar(id[i])) return false;
      memcpy(out.id, id, n + 1);
      have_id = true;
    } else if (strcmp(key, "cmd") == 0) {
      if (have_cmd) return false;
      if (!readString(c, out.cmd, sizeof(out.cmd))) return false;
      if (out.cmd[0] == 0) return false;
      have_cmd = true;
    } else {
      if (!skipValue(c)) return false;
    }
    c.ws();
    if (!c.more()) return false;
    if (*c.p == ',') { c.p++; continue; }
    if (*c.p == '}') { c.p++; break; }
    return false;
  }
  c.ws();
  if (c.more()) return false;   // trailing bytes
  return have_id && have_cmd;
}

// Frame a response into `out` (at most `cap` bytes with the NUL): the reply is JSON-escaped and, when it does not
// fit, cut at a character boundary with "truncated":true. `id` is empty for a request that had none. Returns the length.
inline size_t formatResponse(char* out, size_t cap, const char* id, uint8_t status, const char* reply) {
  size_t n = 0;
  n += snprintf(out, cap, "{\"id\":\"%s\",\"status\":\"%s\",\"ok\":%s,\"reply\":\"", id, statusName(status),
                status == S_OK ? "true" : "false");
  if (n >= cap) { out[cap - 1] = 0; return strlen(out); }
  static const char TAIL_OK[] = "\"}";
  static const char TAIL_CUT[] = "\",\"truncated\":true}";
  const size_t reserve = sizeof(TAIL_CUT) - 1;
  size_t i = 0;
  const size_t rl = strlen(reply);
  bool cut = false;
  while (i < rl) {
    const unsigned char ch = (unsigned char)reply[i];
    char esc[7];
    size_t el = 0;
    size_t used = 1;   // input bytes this output unit consumes
    if (ch == '"' || ch == '\\') { esc[0] = '\\'; esc[1] = (char)ch; el = 2; }
    else if (ch == '\n') { esc[0] = '\\'; esc[1] = 'n'; el = 2; }
    else if (ch == '\r') { esc[0] = '\\'; esc[1] = 'r'; el = 2; }
    else if (ch == '\t') { esc[0] = '\\'; esc[1] = 't'; el = 2; }
    else if (ch < 0x20 || ch == 0x7f) { el = (size_t)snprintf(esc, sizeof(esc), "\\u%04x", ch); }
    else if (ch >= 0xC0) {   // a UTF-8 sequence goes whole or not at all
      size_t sl = ch >= 0xF0 ? 4 : (ch >= 0xE0 ? 3 : 2);
      size_t k = 1;
      while (k < sl && i + k < rl && ((unsigned char)reply[i + k] & 0xC0) == 0x80) k++;
      used = k;
      if (n + k + reserve >= cap) { cut = true; break; }   // keeps room for the tail and the NUL
      memcpy(out + n, reply + i, k);
      n += k;
      i += used;
      continue;
    } else { esc[0] = (char)ch; el = 1; }
    if (n + el + reserve >= cap) { cut = true; break; }
    memcpy(out + n, esc, el);
    n += el;
    i += used;
  }
  const char* tail = cut ? TAIL_CUT : TAIL_OK;
  const size_t tl = strlen(tail);
  if (n + tl >= cap) { out[0] = 0; return 0; }   // cannot happen with cap >= 64 and an id of at most 32 characters
  memcpy(out + n, tail, tl + 1);
  return n + tl;
}

}  // namespace Mqtt
}  // namespace Cli
