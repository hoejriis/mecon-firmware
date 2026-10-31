#pragma once
// The parts of a download over IP that are pure text (docs/contract/UPDATES.md section 6): the offer's source.url and the
// HTTP/1.1 response head. Strict on purpose: a URL this cannot read is refused, never guessed at. Header-only and
// platform-free: the native tests drive it.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Upd {

struct Url {
  bool tls;            // https
  char host[64];       // a DNS name or a dotted IPv4 address
  uint16_t port;
  char path[200];      // from the first "/", query included
};

inline bool urlHostChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

// "http://host[:port]/path" or "https://...". No user info, no fragment, no IPv6 literal, nothing outside printable ASCII.
inline bool parseUrl(const char* s, Url* out) {
  if (!s || !out) return false;
  memset(out, 0, sizeof(*out));
  const char* p;
  if (strncmp(s, "https://", 8) == 0) { out->tls = true; p = s + 8; out->port = 443; }
  else if (strncmp(s, "http://", 7) == 0) { out->tls = false; p = s + 7; out->port = 80; }
  else return false;
  size_t h = 0;
  while (urlHostChar(p[h])) {
    if (h + 1 >= sizeof(out->host)) return false;
    out->host[h] = p[h];
    h++;
  }
  if (h == 0) return false;
  p += h;
  if (*p == ':') {
    p++;
    uint32_t port = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 5) return false;
      port = port * 10 + (uint32_t)(*p++ - '0');
    }
    if (!digits || port == 0 || port > 65535) return false;
    out->port = (uint16_t)port;
  }
  if (*p == 0) { strcpy(out->path, "/"); return true; }
  if (*p != '/') return false;   // user info ("@"), an IPv6 literal, anything else between host and path
  size_t n = strlen(p);
  if (n >= sizeof(out->path)) return false;
  for (size_t i = 0; i < n; i++)
    if ((unsigned char)p[i] <= 0x20 || (unsigned char)p[i] >= 0x7f || p[i] == '#') return false;
  memcpy(out->path, p, n + 1);
  return true;
}

// The address a host names directly: a dotted IPv4 literal. False for a DNS name.
inline bool urlIpv4(const char* host, uint32_t* addr) {
  uint32_t a = 0;
  const char* p = host;
  for (int i = 0; i < 4; i++) {
    if (*p < '0' || *p > '9') return false;
    uint32_t octet = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 3) return false;
      octet = octet * 10 + (uint32_t)(*p++ - '0');
    }
    if (octet > 255) return false;
    a = a << 8 | octet;
    if (i < 3 && *p++ != '.') return false;
  }
  if (*p) return false;
  *addr = a;
  return true;
}

// The response head, one line at a time (without its CRLF). The first line is the status line.
struct HttpHead {
  int status = 0;              // 0 until the status line is read
  bool has_length = false;
  uint32_t length = 0;
  bool chunked = false;        // Transfer-Encoding other than identity: this download cannot take it
  bool bad = false;            // a line this cannot read
};

inline bool headerIs(const char* line, const char* name, const char** value) {
  const size_t n = strlen(name);
  for (size_t i = 0; i < n; i++) {
    char c = line[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != name[i]) return false;
  }
  if (line[n] != ':') return false;
  const char* v = line + n + 1;
  while (*v == ' ' || *v == '\t') v++;
  *value = v;
  return true;
}

inline void httpHeadLine(HttpHead& h, const char* line) {
  if (h.bad) return;
  if (!h.status) {   // "HTTP/1.x SSS reason"
    if (strncmp(line, "HTTP/1.", 7) != 0 || (line[7] != '0' && line[7] != '1') || line[8] != ' ') { h.bad = true; return; }
    const char* s = line + 9;
    if (s[0] < '1' || s[0] > '5' || s[1] < '0' || s[1] > '9' || s[2] < '0' || s[2] > '9' || (s[3] && s[3] != ' ')) {
      h.bad = true;
      return;
    }
    h.status = (s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0');
    return;
  }
  const char* v;
  if (headerIs(line, "content-length", &v)) {
    uint32_t n = 0;
    int digits = 0;
    while (*v >= '0' && *v <= '9') {
      if (++digits > 9) { h.bad = true; return; }
      n = n * 10 + (uint32_t)(*v++ - '0');
    }
    while (*v == ' ' || *v == '\t') v++;
    if (!digits || *v || (h.has_length && h.length != n)) { h.bad = true; return; }   // two different lengths: refused
    h.has_length = true;
    h.length = n;
  } else if (headerIs(line, "transfer-encoding", &v)) {
    if (strcmp(v, "identity") != 0) h.chunked = true;
  }
}

}  // namespace Upd
