// Host-native stand-in for the ESP32 WiFi API: just the calls the Companion example makes.
// The simulated board has no Wi-Fi radio; the link is reported as down.
#pragma once
#include <Arduino.h>
#include <string>

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

typedef int WiFiEvent_t;
#define ARDUINO_EVENT_WIFI_STA_DISCONNECTED 5
#define ARDUINO_EVENT_WIFI_STA_GOT_IP 7
struct WiFiEventInfo_t { int unused; };

struct SimIPAddress { std::string toString() const { return "0.0.0.0"; } };

class SimWiFi {
public:
  void begin(const char*, const char*) {}
  void setAutoReconnect(bool) {}
  template <typename F> void onEvent(F) {}
  void disconnect() {}
  void reconnect() {}
  int status() { return WL_DISCONNECTED; }
  SimIPAddress localIP() { return SimIPAddress(); }
};
extern SimWiFi WiFi;

// Members of the real SerialWifiInterface; the simulator has no network stack behind them.
class WiFiClient { };
class WiFiServer { };
