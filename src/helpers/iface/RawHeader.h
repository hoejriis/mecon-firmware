#pragma once
// What the first bytes of a received frame say about itself, for the observations stream (TELEMETRY.md): the payload
// type, the route and the hop count, read from the frame as the radio heard it. No dependencies, so a native test
// can hold it to MeshCore's own encoding. This reads a header; it does not decide whether the frame is a valid
// packet, and says nothing about who sent it or which node last transmitted it.
#include <stdint.h>

namespace Iface {

struct RawHeader {
  uint8_t type;          // payload type, 0..15 (the header's bits 2..5)
  const char* route;     // "tflood", "flood", "direct" or "tdirect" (the header's bits 0..1)
  bool path_known;       // the frame reaches its path-length byte and that byte's mode is not the reserved one
  uint8_t path_len;      // hops recorded in the path (the low 6 bits of that byte); 0 unless path_known
};

inline RawHeader parseRawHeader(const uint8_t* raw, int len) {
  static const char* const ROUTES[4] = {"tflood", "flood", "direct", "tdirect"};
  RawHeader h = {0, ROUTES[1], false, 0};
  if (!raw || len < 1) return h;
  const uint8_t route = raw[0] & 0x03;
  h.type = (raw[0] >> 2) & 0x0F;
  h.route = ROUTES[route];
  const int at = (route == 0 || route == 3) ? 5 : 1;   // transport codes (4 bytes) come before the path length
  if (len > at && (raw[at] >> 6) != 3) {
    h.path_known = true;
    h.path_len = raw[at] & 63;
  }
  return h;
}

}  // namespace Iface
