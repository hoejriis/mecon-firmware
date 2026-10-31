// ESP32 network for the MQTT controller: Arduino WiFi / WiFiClientSecure (mbedTLS) and the ESP-IDF mDNS component's
// asynchronous query. Not exercised on hardware yet; compiled for the Heltec V3 and V4 Wi-Fi builds.
#if defined(ESP32) && defined(ENABLE_MQTT)
#include "MqttNet.h"
#include "MqttProfiles.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <esp_idf_version.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <time.h>
#include <mbedtls/x509_crt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
// The connect worker: one FreeRTOS task, created on the first connect attempt and kept (creating and deleting a task
// per attempt would churn the heap and risk a failed allocation exactly when a TLS context needs the memory). It
// sleeps on a task notification, runs one job, and sleeps again.
//
// Stack: 10240 bytes. The sequence it runs (DNS, WiFiClientSecure::connect with the mbedTLS handshake, then
// PubSubClient::connect) runs today on the Arduino loop task, whose stack is 8192 bytes and which also carries the
// Companion's call depth; the mbedTLS handshake is the deepest part (ECDHE and ECDSA/RSA verification, X.509 parsing)
// and its large buffers are on the heap, not the stack. 10 KiB is that 8 KiB plus 25 % headroom, not a measurement:
// the high-water mark of the worker's stack is read after every job (stackFree()) so the hardware run can replace the
// estimate with a figure. The stack is allocated from the internal heap when the first attempt starts, so a device
// that never connects never pays it.
//
// Core: the Arduino loop (and with it the mesh) runs on ARDUINO_RUNNING_CORE; the worker is pinned to the other core,
// so a handshake that takes hundreds of milliseconds of CPU does not compete with mesh processing. Priority 1, the
// same as the loop task: the Wi-Fi and lwIP tasks above it keep the radio and the network stack responsive.
const uint32_t WORKER_STACK_BYTES = 10240;
#ifdef ARDUINO_RUNNING_CORE
const BaseType_t WORKER_CORE = ARDUINO_RUNNING_CORE == 0 ? 1 : 0;
#else
const BaseType_t WORKER_CORE = 0;
#endif

class Worker {
  TaskHandle_t _task = nullptr;
  void (*_job)(void*) = nullptr;
  void* _arg = nullptr;
  std::atomic<bool> _busy{false};
  std::atomic<uint32_t> _stack_free{0};

  static void body(void* self) {
    Worker* w = static_cast<Worker*>(self);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      w->_job(w->_arg);
      w->_stack_free.store((uint32_t)uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
#ifdef MESH_DEBUG
      Serial.printf("DEBUG: mqtt worker stack: %u of %u bytes unused at its deepest\n",
                    (unsigned)w->_stack_free.load(std::memory_order_relaxed), (unsigned)WORKER_STACK_BYTES);
#endif
      w->_busy.store(false, std::memory_order_release);   // publishes everything the job wrote
    }
  }

public:
  bool start(void (*job)(void*), void* arg) {
    if (_busy.load(std::memory_order_acquire)) return false;
    if (!_task) {
      if (xTaskCreatePinnedToCore(&Worker::body, "mqttconn", WORKER_STACK_BYTES, this, 1, &_task, WORKER_CORE) != pdPASS) {
        _task = nullptr;
        return false;
      }
    }
    _job = job;
    _arg = arg;
    _busy.store(true, std::memory_order_relaxed);
    xTaskNotifyGive(_task);
    return true;
  }
  bool idle() const { return !_busy.load(std::memory_order_acquire); }
  // Unused stack bytes at the deepest point of any job so far (0 before the first job).
  uint32_t stackFree() const { return _stack_free.load(std::memory_order_relaxed); }
};

// WiFiClientSecure with two additions the library does not offer: connect to an address while presenting a host name
// (SNI), and verify the peer's chain against one CA certificate after an unvalidated handshake, with or without a name.
// The library's own CA validation cannot do either: it uses the host it connects to as the name to check, so a
// connection to an address checks the address text, and a discovered broker has no name at all.
class SecClient : public WiFiClientSecure {
public:
  int connectNamed(IPAddress ip, uint16_t port, const char* name, int32_t timeout_ms) {
    _timeout = timeout_ms;
    return WiFiClientSecure::connect(ip, port, name, nullptr, nullptr, nullptr);   // name may be null: the address text is sent
  }
  // True when the peer's certificate chains to the CA (base64 DER) and, when `name` is given, carries that name.
  // A device whose clock has not been set (before 2025) cannot judge validity dates, so for it only the date flags are
  // ignored; the chain, the signatures and the name are always checked.
  bool chainsTo(const char* ca_b64, const char* name) {
    const mbedtls_x509_crt* peer = getPeerCertificate();
    uint8_t* der = (uint8_t*)malloc(MqttProfile::CA_B64_MAX / 4 * 3 + 3);   // heap: the worker's stack is sized for the handshake
    if (!peer || !der) { free(der); return false; }
    int n = mqttCaDecode(ca_b64, der, MqttProfile::CA_B64_MAX / 4 * 3 + 3);
    mbedtls_x509_crt ca;
    mbedtls_x509_crt_init(&ca);
    bool ok = false;
    if (n > 0 && mbedtls_x509_crt_parse_der(&ca, der, (size_t)n) == 0) {
      uint32_t flags = 0;
      mbedtls_x509_crt_verify(const_cast<mbedtls_x509_crt*>(peer), &ca, nullptr, name, &flags, nullptr, nullptr);
      if (time(nullptr) < 1735689600) flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);
      ok = flags == 0;
    }
    mbedtls_x509_crt_free(&ca);
    free(der);
    return ok;
  }
};

class Esp32Net : public MqttNet {
  WiFiClient* _plain = nullptr;
  SecClient* _sec = nullptr;
  WiFiClient _none;                   // handed out only when nothing is open
  Worker _worker;
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
#if ESP_IDF_VERSION_MAJOR >= 5   // ESP-IDF 5 also reports the number of results
    uint8_t nres = 0;
    if (!mdns_query_async_get_results(_q, 0, &r, &nres)) return -1;   // still running
#else
    if (!mdns_query_async_get_results(_q, 0, &r)) return -1;   // still running
#endif
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

  MqttOpen open(const MqttOpenArgs& a) override {
    close();
    IPAddress ip((a.addr >> 24) & 255, (a.addr >> 16) & 255, (a.addr >> 8) & 255, a.addr & 255);
    if (a.tls) {
      _sec = new SecClient();
      if (!_sec) return MqttOpen::Tls;
      // The handshake runs without the library's chain validation; the broker is authenticated afterwards by the
      // anchor: a leaf fingerprint, or a chain to a CA certificate (and the host name, when the profile has one). A TLS
      // profile without an anchor is encrypted but unauthenticated, and the controller reports it as such.
      _sec->setInsecure();
      _sec->setHandshakeTimeout((a.timeout_ms + 999) / 1000);
      if (!_sec->connectNamed(ip, a.port, a.name, (int32_t)a.timeout_ms)) return MqttOpen::Tls;
      if (a.ca_b64) return _sec->chainsTo(a.ca_b64, a.name) ? MqttOpen::Ok : MqttOpen::Pin;
      if (a.anchor_hex && !_sec->verify(a.anchor_hex, nullptr)) return MqttOpen::Pin;
      return MqttOpen::Ok;
    }
    _plain = new WiFiClient();
    if (!_plain) return MqttOpen::Tcp;
    return _plain->connect(ip, a.port, (int32_t)a.timeout_ms) ? MqttOpen::Ok : MqttOpen::Tcp;
  }

  Client& client() override {
    if (_sec) return *_sec;
    if (_plain) return *_plain;
    return _none;
  }

  bool asyncStart(void (*job)(void*), void* arg) override { return _worker.start(job, arg); }
  bool asyncIdle() override { return _worker.idle(); }

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

MqttNet& updatePlatformNet() {
  static Esp32Net n;
  return n;
}
#endif
