// Host-native stand-in for the one ESP-IDF event registration the Wi-Fi controller makes (the scan-done event).
#pragma once
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
typedef const char* esp_event_base_t;
typedef void (*esp_event_handler_t)(void* arg, esp_event_base_t base, int32_t id, void* data);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void* arg);
