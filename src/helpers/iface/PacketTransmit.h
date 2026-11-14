#pragma once
// packet.transmit (OPERATIONS.md section 5.7): the checks a manager-supplied native packet passes before a Repeater queues it.
// The bytes are Packet::writeTo's: header, the transport codes when the route has them, path_len, path, payload. Every length
// is checked against what was received, unlike Packet::readFrom and Dispatcher::tryParsePacket, which read the transport
// codes and path without that check: those parse what the radio heard, this parses what a caller sent.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Packet.h"

namespace Iface {

struct RawPacketInfo {
  uint8_t route, type;     // PH_ROUTE_MASK bits and the payload type
  size_t payload_len;
};

// The payload types a manager may have a Repeater send: messaging and what a bridge forwards. An advert re-announces an
// identity, a trace writes its path into the payload, and multipart, control and custom packets are not messaging.
inline bool rawTypeAllowed(uint8_t type) {
  switch (type) {
    case PAYLOAD_TYPE_REQ: case PAYLOAD_TYPE_RESPONSE: case PAYLOAD_TYPE_TXT_MSG: case PAYLOAD_TYPE_ACK:
    case PAYLOAD_TYPE_GRP_TXT: case PAYLOAD_TYPE_GRP_DATA: case PAYLOAD_TYPE_ANON_REQ: case PAYLOAD_TYPE_PATH:
      return true;
    default:
      return false;
  }
}

// nullptr when b is one whole native packet of an allowed type, else the error to answer (invalid unless noted):
// "malformed" (lengths, version, path), "unsupported-type". Fills *info and *pkt (when given) only on success.
inline const char* checkRawPacket(const uint8_t* b, size_t n, RawPacketInfo* info, mesh::Packet* pkt = nullptr) {
  if (!b || n < 3 || n > MAX_TRANS_UNIT) return "malformed";   // a header, path_len and at least one payload byte
  size_t i = 0;
  const uint8_t header = b[i++];
  const uint8_t route = header & PH_ROUTE_MASK;
  const uint8_t type = (header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;
  if (((header >> PH_VER_SHIFT) & PH_VER_MASK) != PAYLOAD_VER_1) return "malformed";
  const bool codes = route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT;
  uint16_t tc[2] = {0, 0};
  if (codes) {
    if (n - i < 4) return "malformed";
    memcpy(&tc[0], &b[i], 2); memcpy(&tc[1], &b[i + 2], 2);
    i += 4;
  }
  if (n - i < 1) return "malformed";
  const uint8_t path_len = b[i++];
  if (!mesh::Packet::isValidPathLen(path_len)) return "malformed";
  const size_t path_bytes = (size_t)(path_len & 63) * ((path_len >> 6) + 1);
  if (n - i < path_bytes) return "malformed";
  const size_t path_at = i;
  i += path_bytes;
  const size_t payload_len = n - i;
  if (payload_len < 1 || payload_len > MAX_PACKET_PAYLOAD) return "malformed";
  if (!rawTypeAllowed(type)) return "unsupported-type";
  if (info) { info->route = route; info->type = type; info->payload_len = payload_len; }
  if (pkt) {
    pkt->header = header;
    pkt->transport_codes[0] = tc[0]; pkt->transport_codes[1] = tc[1];
    pkt->path_len = path_len;
    memcpy(pkt->path, b + path_at, path_bytes);
    pkt->payload_len = (uint16_t)payload_len;
    memcpy(pkt->payload, b + i, payload_len);
  }
  return nullptr;
}

}  // namespace Iface
