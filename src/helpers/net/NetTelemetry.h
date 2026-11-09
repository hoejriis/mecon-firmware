#pragma once
// The links' part of the connectivity stream (docs/contract/TELEMETRY.md), as plain values: the same members on the
// Companion, the Repeater and the Room server.
#ifdef ENABLE_WIFI_INTERFACE
#include <stdint.h>
#include <stdio.h>
#include <helpers/iface/IfaceJson.h>
#include <helpers/wifi/WifiController.h>
#ifdef ENABLE_MQTT
  #include <helpers/mqtt/MqttController.h>
#endif

namespace Iface {

inline void writeNetConnectivity(JsonWriter& w) {
  WifiController::Snapshot ws;
  wifiController().snapshot(&ws);
  w.key("wifi"); w.beginObject();
  w.key("state"); w.str(WifiCore::stateName(ws.state));
  const bool joined = ws.state == WifiState::Connected;
  w.key("profile"); if (ws.profile != WifiCore::NONE) w.unum(ws.profile); else w.null();
  w.key("ssid"); if (ws.ssid) w.str(ws.ssid); else w.null();
  w.key("rssi_dbm"); if (joined) w.num(ws.rssi); else w.null();
  w.key("ip"); if (joined && ws.ip[0]) w.str(ws.ip); else w.null();
  w.key("up_s"); w.unum(ws.up_s);
  w.key("last_fail"); w.str(WifiCore::failName(ws.last_fail));
  w.endObject();
#ifdef ENABLE_MQTT
  MqttController::Snapshot ms;
  mqttController().snapshot(&ms);
  w.key("mqtt"); w.beginObject();
  w.key("state"); w.str(ms.active ? MqttCore::stateName(ms.state) : "off");
  const bool up = ms.state == MqttState::Connected;
  w.key("profile"); if (ms.profile != MqttCore::NONE) w.unum(ms.profile); else w.null();
  char broker[24];
  snprintf(broker, sizeof(broker), "%u.%u.%u.%u:%u", (unsigned)(ms.addr >> 24), (unsigned)((ms.addr >> 16) & 255),
           (unsigned)((ms.addr >> 8) & 255), (unsigned)(ms.addr & 255), (unsigned)ms.port);
  w.key("broker"); if (up) w.str(broker); else w.null();
  static const char* const TLS[] = {"plain", "verified", "unverified"};
  w.key("tls"); if (up) w.str(TLS[ms.tls]); else w.null();
  w.key("up_s"); w.unum(ms.up_s);
  w.key("browsing"); w.boolean(ms.browsing);
  w.key("reconnects"); w.unum(ms.reconnects);
  w.key("write_ms_max"); w.unum(ms.write_ms_max);   // the longest one publish held the loop (#447)
  w.key("slow_writes"); w.unum(ms.slow_writes);
  w.endObject();
#endif
}

// Changes when a link changes state or profile: the stream then sends an event at once.
inline uint32_t netConnSignature() {
  WifiController::Snapshot ws;
  wifiController().snapshot(&ws);
  uint32_t sig = (uint32_t)ws.state | ((uint32_t)ws.profile << 8);
#ifdef ENABLE_MQTT
  MqttController::Snapshot ms;
  mqttController().snapshot(&ms);
  sig |= ((uint32_t)ms.state << 16) | ((uint32_t)ms.profile << 24);
#endif
  return sig;
}

}  // namespace Iface
#endif  // ENABLE_WIFI_INTERFACE
