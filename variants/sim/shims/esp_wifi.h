// Host-native stand-in for the ESP-IDF Wi-Fi scan calls the Wi-Fi controller uses (esp_wifi.h of ESP-IDF 4.4, only the
// members used). The driver behind them is simulated by variants/sim/sim_wifi.cpp.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_event.h"

#define ESP_FAIL (-1)
#define ESP_ERR_WIFI_STATE 0x300B

typedef enum { WIFI_SCAN_TYPE_ACTIVE = 0, WIFI_SCAN_TYPE_PASSIVE } wifi_scan_type_t;
typedef struct { uint32_t min; uint32_t max; } wifi_active_scan_time_t;
typedef struct { wifi_active_scan_time_t active; uint32_t passive; } wifi_scan_time_t;
typedef struct {
  uint8_t* ssid;
  uint8_t* bssid;
  uint8_t channel;
  bool show_hidden;
  wifi_scan_type_t scan_type;
  wifi_scan_time_t scan_time;
} wifi_scan_config_t;
typedef struct { uint8_t bssid[6]; uint8_t ssid[33]; uint8_t primary; int8_t rssi; } wifi_ap_record_t;
typedef struct { uint32_t status; uint8_t number; uint8_t scan_id; } wifi_event_sta_scan_done_t;

#define WIFI_EVENT_SCAN_DONE 1
extern const char* WIFI_EVENT_BASE_NAME;
#define WIFI_EVENT WIFI_EVENT_BASE_NAME

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* number, wifi_ap_record_t* ap_records);
