#include "WifiController.h"
#include <WiFi.h>
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
}  // namespace

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
};

void WifiController::begin(WifiProfiles* profiles, const WifiTiming& timing) {
  _profiles = profiles;
  _core.configure(profiles, timing);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // the controller decides which network to rejoin
#if defined(ESP32)
  WiFi.persistent(false);         // do not keep a second copy of the password in the radio's own flash store
  WiFi.onEvent(WifiControllerEvents::onEvent);
#endif
  _started = true;
  _scanning = false;
  _core.begin(millis());
}

void WifiController::profileChanged(uint8_t index, bool credentials) {
  if (_started) _core.profileChanged(millis(), index, credentials);
}

void WifiController::doConnect(uint8_t i) {
  WIFICTL_LOG("joining profile %u (%s)", (unsigned)i, _profiles->ssid(i));
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false);
#if defined(RP2040_PLATFORM)
  WiFi.beginNoBlock(_profiles->ssid(i), _profiles->pwd(i));   // the join itself still blocks inside the core
#else
  WiFi.begin(_profiles->ssid(i), _profiles->pwd(i));
#endif
}

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

  if (_scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      for (int k = 0; k < n; k++) _core.scanObserve(cstr(WiFi.SSID(k)), (int8_t)WiFi.RSSI(k));
      WiFi.scanDelete();
      _scanning = false;
      _core.scanDone(now);
    } else if (n != -1) {   // not "still running": the scan failed, treat it as one that found nothing
      WiFi.scanDelete();
      _scanning = false;
      _core.scanDone(now);
    }
  }

  WifiAction a = _core.poll(now);
  switch (a.kind) {
    case WifiAction::Scan:
      WIFICTL_LOG("scanning");
      _scanning = true;
      if (WiFi.scanNetworks(true) == -2) {   // could not start (WIFI_SCAN_FAILED): report an empty result next loop
        _scanning = false;
        _core.scanDone(now);
      }
      break;
    case WifiAction::Connect:
      _scanning = false;
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
