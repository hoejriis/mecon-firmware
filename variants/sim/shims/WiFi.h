// Host-native stand-in for the ESP32 Arduino WiFi API: the subset the Companion example and the Wi-Fi controller
// use. The radio is simulated by variants/sim/sim_wifi.cpp: the access points a test defines (SSID, password,
// RSSI, up/down at run time) are read from the file named by --wifi-env.
#pragma once
#include <Arduino.h>
#include <string>

// wl_status_t values, as in the Arduino cores
#define WL_NO_SSID_AVAIL 1
#define WL_CONNECTED 3
#define WL_CONNECT_FAILED 4
#define WL_CONNECTION_LOST 5
#define WL_DISCONNECTED 6

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

enum WiFiMode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };

typedef int WiFiEvent_t;
#define ARDUINO_EVENT_WIFI_STA_DISCONNECTED 5
#define ARDUINO_EVENT_WIFI_STA_GOT_IP 7
struct WiFiEventInfo_t {
  struct { uint8_t reason; } wifi_sta_disconnected;
};

struct SimIPAddress { std::string toString() const; };

class SimWiFi {
public:
  void mode(WiFiMode_t) {}
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void setTimeout(unsigned long) {}
  void onEvent(void (*cb)(WiFiEvent_t, WiFiEventInfo_t));
  void begin(const char* ssid, const char* pwd);
  void disconnect(bool wifioff = false);
  int status();
  int32_t RSSI();
  SimIPAddress localIP();
  // Arduino's read side of a scan (the table its own done handler keeps); the scan itself runs through esp_wifi.h
  int16_t scanComplete();
  void scanDelete();
  std::string SSID(uint8_t i);
  int32_t RSSI(uint8_t i);
};
extern SimWiFi WiFi;

// Members of the real SerialWifiInterface; the simulator has no network stack behind them.
class WiFiClient { };
class WiFiServer { };
