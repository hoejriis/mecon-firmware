#include "WifiController.h"
#include <WiFi.h>
#if defined(ESP32)
#include <esp_wifi.h>
#include <esp_event.h>
#include <stdlib.h>
#endif
#include <stdio.h>
#include <string.h>

WifiController& wifiController() {
  static WifiController c;
  return c;
}

namespace {
// SSID(i) is a String on ESP32 and a const char* on RP2040.
inline const char* cstr(const char* s) { return s; }
template <typename T> inline const char* cstr(const T& s) { return s.c_str(); }

// ESP-IDF disconnect reasons (esp_wifi_types.h), as numbers so a differing IDF enum naming cannot break the build.
const uint8_t REASON_ASSOC_LEAVE = 8;                 // we left on purpose
const uint8_t REASON_4WAY_HANDSHAKE_TIMEOUT = 15;     // wrong passphrase on WPA2
const uint8_t REASON_NO_AP_FOUND = 201;
const uint8_t REASON_AUTH_FAIL = 202;
const uint8_t REASON_HANDSHAKE_TIMEOUT = 204;

WifiDrop dropFromReason(uint8_t r) {
  if (r == REASON_AUTH_FAIL || r == REASON_4WAY_HANDSHAKE_TIMEOUT || r == REASON_HANDSHAKE_TIMEOUT) return WifiDrop::AuthFailed;
  if (r == REASON_NO_AP_FOUND) return WifiDrop::NotFound;
  return WifiDrop::Other;
}

// The scan is bounded by us: a full sweep with the dwell below takes about 1.5 s, so a scan still running after
// SCAN_BOUND_MS has failed whatever the driver says. WifiTiming::scan_timeout_ms (20 s) is only the core's backstop.
const uint32_t SCAN_BOUND_MS = 5000;
const uint32_t SCAN_ARDUINO_GRACE_MS = 300;   // after the done event, Arduino's own tables get this long to fill
const uint16_t SCAN_MAX_RECORDS = 48;         // strongest first, as the driver sorts them
}  // namespace

#if defined(ESP32)
// Runs in the Wi-Fi task on ESP32: only sets flags, loop() does the rest.
struct WifiControllerEvents {
  static void onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    WifiController& c = wifiController();
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      c._ev_got_ip = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      c._ev_reason = info.wifi_sta_disconnected.reason;
      c._ev_disc = true;
    }
  }
  // Registered on the default event loop itself, so it does not depend on Arduino's own scan-done path (the
  // arduino_events task and WiFiScanClass). Runs in the event loop task: only flags.
  static void onScanDone(void*, esp_event_base_t, int32_t, void* data) {
    WifiController& c = wifiController();
    if (!c._scan_armed) return;   // not a scan of ours (or one we already gave up on)
    const wifi_event_sta_scan_done_t* ev = static_cast<const wifi_event_sta_scan_done_t*>(data);
    c._ev_scan_ok = ev != nullptr && ev->status == 0;
    __sync_synchronize();
    c._ev_scan_done = true;
  }
};
#endif

void WifiController::begin(WifiProfiles* profiles, const WifiTiming& timing) {
  _profiles = profiles;
  _core.configure(profiles, timing);
  WiFi.mode(WIFI_STA);
#if defined(ESP32)
  WiFi.setAutoReconnect(false);   // the controller decides which network to rejoin
  WiFi.persistent(false);         // do not keep a second copy of the password in the radio's own flash store
  WiFi.onEvent(WifiControllerEvents::onEvent);
  static bool scan_done_registered = false;   // the default event loop exists once WiFi.mode has run
  if (!scan_done_registered) {
    scan_done_registered = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &WifiControllerEvents::onScanDone,
                                                      nullptr) == ESP_OK;
  }
#endif
  _started = true;
  _scanning = false;
  _scan_armed = false;
  _core.begin(millis());
}

void WifiController::profileChanged(uint8_t index, bool credentials) {
  if (_started) _core.profileChanged(millis(), index, credentials);
}

void WifiController::doConnect(uint8_t i) {
  WIFICTL_LOG("joining profile %u (%s)", (unsigned)i, _profiles->ssid(i));
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false);
#if defined(RP2040_PLATFORM)
  WiFi.setTimeout(5000);   // the join blocks inside the core: cap how long one attempt may stall the mesh loop
  WiFi.beginNoBlock(_profiles->ssid(i), _profiles->pwd(i));   // the join itself still blocks inside the core
#else
  WiFi.begin(_profiles->ssid(i), _profiles->pwd(i));
#endif
}

#if defined(ESP32)
// ESP32: the scan is ours end to end. esp_wifi_scan_start returns at once; the done event (our own handler) says it
// ended; the results are Arduino's table when its handler filled it, else read from the driver; and a scan that has
// not ended after SCAN_BOUND_MS is stopped in the driver so the next one can run.
void WifiController::scanStart(uint32_t now) {
  WiFi.scanDelete();   // Arduino's table from an earlier scan, and its done bit
  wifi_scan_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));   // all channels, any SSID, no hidden networks
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 40;    // ms per channel: leave early when nothing answers ...
  cfg.scan_time.active.max = 120;   // ... never longer than this (Arduino's default is 300)
  _ev_scan_done = false;
  _ev_scan_ok = false;
  _scan_ev_at = 0;
  _scan_t0 = now;
  _scan_armed = true;
  if (esp_wifi_scan_start(&cfg, false) != ESP_OK) {   // refused (a scan the driver still holds): clear it, report failure
    _scan_armed = false;
    esp_wifi_scan_stop();
    WIFICTL_LOG("scan refused");
    _core.scanFailed(now);
    return;
  }
  _scanning = true;
}

void WifiController::scanAbort() {
  _scan_armed = false;
  _scanning = false;
  esp_wifi_scan_stop();
}

void WifiController::scanPoll(uint32_t now) {
  if (_ev_scan_done) {
    if (!_ev_scan_ok) { scanAbort(); _core.scanFailed(now); return; }
    if (!_scan_ev_at) _scan_ev_at = now ? now : 1;
    int n = WiFi.scanComplete();   // Arduino's handler, when it ran, read the driver's list into its own table
    if (n >= 0) {
      for (int k = 0; k < n; k++) _core.scanObserve(cstr(WiFi.SSID(k)), (int8_t)WiFi.RSSI(k));
      WiFi.scanDelete();
    } else if ((uint32_t)(now - _scan_ev_at) >= SCAN_ARDUINO_GRACE_MS) {
      // Arduino never took the event: the list is still in the driver. Read it (and free it) ourselves.
      uint16_t cnt = 0;
      esp_wifi_scan_get_ap_num(&cnt);
      if (cnt > SCAN_MAX_RECORDS) cnt = SCAN_MAX_RECORDS;
      wifi_ap_record_t* rec = cnt ? static_cast<wifi_ap_record_t*>(malloc(sizeof(wifi_ap_record_t) * cnt)) : nullptr;
      if (cnt && !rec) { scanAbort(); _core.scanFailed(now); return; }
      if (cnt && esp_wifi_scan_get_ap_records(&cnt, rec) != ESP_OK) cnt = 0;
      for (uint16_t k = 0; k < cnt; k++) _core.scanObserve(reinterpret_cast<const char*>(rec[k].ssid), rec[k].rssi);
      free(rec);
    } else {
      return;   // give Arduino's tables a moment
    }
    _scan_armed = false;
    _scanning = false;
    _core.scanDone(now);
    return;
  }
  if ((uint32_t)(now - _scan_t0) >= SCAN_BOUND_MS) {
    WIFICTL_LOG("scan did not report");
    scanAbort();
    _core.scanFailed(now);
  }
}
#else
// RP2040: Arduino's async scan, with the same bound.
void WifiController::scanStart(uint32_t now) {
  _scan_t0 = now;
  _scanning = true;
  if (WiFi.scanNetworks(true) == -2) {   // could not start (WIFI_SCAN_FAILED)
    _scanning = false;
    _core.scanFailed(now);
  }
}

void WifiController::scanAbort() {
  _scanning = false;
  WiFi.scanDelete();
}

void WifiController::scanPoll(uint32_t now) {
  int n = WiFi.scanComplete();
  if (n >= 0) {
    for (int k = 0; k < n; k++) _core.scanObserve(cstr(WiFi.SSID(k)), (int8_t)WiFi.RSSI(k));
    WiFi.scanDelete();
    _scanning = false;
    _core.scanDone(now);
  } else if (n != -1 || (uint32_t)(now - _scan_t0) >= SCAN_BOUND_MS) {   // failed, or still running past the bound
    scanAbort();
    _core.scanFailed(now);
  }
}
#endif

void WifiController::loop() {
  if (!_started) return;
  uint32_t now = millis();

#if defined(ESP32)
  if (_ev_got_ip) { _ev_got_ip = false; _core.onConnected(now); }
  if (_ev_disc) {
    _ev_disc = false;
    if (_ev_reason != REASON_ASSOC_LEAVE) _core.onDisconnected(now, dropFromReason(_ev_reason));
  }
#else
  // No Wi-Fi events here: follow the link state.
  uint8_t st = WiFi.status();
  if (_core.state() == WifiState::Connecting && st == WL_CONNECTED) _core.onConnected(now);
  else if (_core.state() == WifiState::Connected && st != WL_CONNECTED) _core.onDisconnected(now, WifiDrop::Other);
  else if (_core.state() == WifiState::Connecting && st == WL_NO_SSID_AVAIL) _core.onDisconnected(now, WifiDrop::NotFound);
  else if (_core.state() == WifiState::Connecting && st == WL_CONNECT_FAILED) _core.onDisconnected(now, WifiDrop::AuthFailed);
#endif

  if (_scanning) scanPoll(now);

  WifiAction a = _core.poll(now);
  switch (a.kind) {
    case WifiAction::Scan:
      WIFICTL_LOG("scanning");
      scanStart(now);
      break;
    case WifiAction::Connect:
      if (_scanning) scanAbort();   // a join while a scan runs (a profile edit): leave the driver idle
      doConnect(a.profile);
      break;
    case WifiAction::Disconnect:
      WiFi.disconnect(false);
      break;
    default:
      break;
  }
}

void WifiController::formatIp(char* out, size_t cap) {
  out[0] = 0;
  if (connected()) snprintf(out, cap, "%s", cstr(WiFi.localIP().toString()));
}

void WifiController::formatStatus(char* out, size_t cap) {
  if (!_started) { snprintf(out, cap, "> off"); return; }
  uint32_t now = millis();
  WifiState st = _core.state();
  uint8_t cur = _core.current();
  if (st == WifiState::Connected && cur != WifiCore::NONE) {
    char ip[20];
    formatIp(ip, sizeof(ip));
    snprintf(out, cap, "> connected profile=%u ssid=%s rssi=%d ip=%s up=%lus", (unsigned)cur, _profiles->ssid(cur),
             (int)WiFi.RSSI(), ip, (unsigned long)(_core.connectedMs(now) / 1000));
  } else if (st == WifiState::Connecting && cur != WifiCore::NONE) {
    snprintf(out, cap, "> connecting profile=%u ssid=%s", (unsigned)cur, _profiles->ssid(cur));
  } else {
    snprintf(out, cap, "> %s", WifiCore::stateName(st));
  }
}
