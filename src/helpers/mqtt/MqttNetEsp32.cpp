// ESP32 network for the MQTT controller: Arduino WiFi / WiFiClientSecure (mbedTLS) and the ESP-IDF mDNS component's
// asynchronous query. Not exercised on hardware yet; compiled for the Heltec V3 and V4 Wi-Fi builds.
#if defined(ESP32) && defined(ENABLE_MQTT)
#include "MqttNet.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <stdio.h>
#include <string.h>

namespace {
class Esp32Net : public MqttNet {
  WiFiClient* _plain = nullptr;
  WiFiClientSecure* _sec = nullptr;
  WiFiClient _none;                   // handed out only when nothing is open
  char _hostname[24] = {0};
  bool _mdns_up = false;
  mdns_search_once_t* _q = nullptr;
  uint8_t _phase = 0;                 // 0: _secure-mqtt._tcp, 1: _mqtt._tcp
  MqttResponder _res[8];
  int _n = 0;

  void startQuery() {
    _q = mdns_query_async_new(NULL, _phase == 0 ? "_secure-mqtt" : "_mqtt", "_tcp", MDNS_TYPE_PTR, 2500, 8, NULL);
  }

public:
  void init(const char* node_id) override { snprintf(_hostname, sizeof(_hostname), "mecon-%s", node_id); }

  bool linkUp() override { return WiFi.status() == WL_CONNECTED; }

  bool resolve(const char* host, uint32_t& addr) override {
    IPAddress ip;
    if (WiFi.hostByName(host, ip) != 1) return false;
    addr = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
    return addr != 0;
  }

  void browseBegin() override {
    // The responder is started once and kept: restarting it for every browse costs a task and heap each time.
    if (!_mdns_up) _mdns_up = MDNS.begin(_hostname);
    if (_q) { mdns_query_async_delete(_q); _q = nullptr; }   // a browse the controller abandoned
    _phase = 0;
    _n = 0;
    if (_mdns_up) startQuery();
  }

  int browsePoll(MqttResponder* out, int max) override {
    if (!_q) return 0;   // could not start: a browse that found nothing
    mdns_result_t* r = nullptr;
    if (!mdns_query_async_get_results(_q, 0, &r)) return -1;   // still running
    for (mdns_result_t* it = r; it && _n < 8; it = it->next) {
      uint32_t addr = 0;
      for (mdns_ip_addr_t* a = it->addr; a; a = a->next) {
        if (a->addr.type == ESP_IPADDR_TYPE_V4) {
          uint32_t raw = a->addr.u_addr.ip4.addr;   // network order in memory
          addr = ((raw & 0xFF) << 24) | (((raw >> 8) & 0xFF) << 16) | (((raw >> 16) & 0xFF) << 8) | ((raw >> 24) & 0xFF);
          break;
        }
      }
      if (!addr) continue;
      MqttResponder& m = _res[_n++];
      m.addr = addr;
      m.port = it->port;
      m.secure = _phase == 0;
      m.site[0] = 0;
      for (size_t t = 0; t < it->txt_count; t++) {
        if (it->txt[t].key && strcmp(it->txt[t].key, "site") == 0 && it->txt[t].value) {
          strncpy(m.site, it->txt[t].value, sizeof(m.site) - 1);
          m.site[sizeof(m.site) - 1] = 0;
        }
      }
    }
    mdns_query_results_free(r);
    mdns_query_async_delete(_q);
    _q = nullptr;
    if (_phase == 0) {
      _phase = 1;
      startQuery();
      return _q ? -1 : (int)_n;
    }
    int n = _n < max ? _n : max;
    for (int k = 0; k < n; k++) out[k] = _res[k];
    return n;
  }

  MqttOpen open(bool tls, uint32_t addr, uint16_t port, const char* anchor_hex, uint32_t timeout_ms) override {
    close();
    IPAddress ip((addr >> 24) & 255, (addr >> 16) & 255, (addr >> 8) & 255, addr & 255);
    if (tls) {
      _sec = new WiFiClientSecure();
      if (!_sec) return MqttOpen::Tls;
      // The chain is not validated against a CA: the broker is authenticated by the pinned fingerprint below. A TLS
      // profile without an anchor is encrypted but unauthenticated, and the controller reports it as such.
      _sec->setInsecure();
      _sec->setHandshakeTimeout((timeout_ms + 999) / 1000);
      if (!_sec->connect(ip, port, (int32_t)timeout_ms)) return MqttOpen::Tls;
      if (anchor_hex && !_sec->verify(anchor_hex, nullptr)) return MqttOpen::Pin;
      return MqttOpen::Ok;
    }
    _plain = new WiFiClient();
    if (!_plain) return MqttOpen::Tcp;
    return _plain->connect(ip, port, (int32_t)timeout_ms) ? MqttOpen::Ok : MqttOpen::Tcp;
  }

  Client& client() override {
    if (_sec) return *_sec;
    if (_plain) return *_plain;
    return _none;
  }

  void close() override {
    if (_sec) { _sec->stop(); delete _sec; _sec = nullptr; }   // frees the TLS context, the large heap user
    if (_plain) { _plain->stop(); delete _plain; _plain = nullptr; }
  }
};
}  // namespace

MqttNet& mqttPlatformNet() {
  static Esp32Net n;
  return n;
}
#endif
