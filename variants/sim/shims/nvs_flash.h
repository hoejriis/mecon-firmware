// The simulated board has no NVS partition; erasing it is a successful no-op.
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
inline esp_err_t nvs_flash_erase() { return ESP_OK; }
