#pragma once
#include <SPIFFS.h>
#if __has_include(<esp_idf_version.h>)   // not on the simulated board
  #include <esp_idf_version.h>
#endif
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  #define SPIFFS_BOOT_IDF_FORMAT
  #include <esp_spiffs.h>
#endif

// SPIFFS.begin(true) on a board's first boot. SPIFFS.format() takes core 0's idle task off the task watchdog for the
// format, but on ESP-IDF 5 the idle hook stays registered: every time that task runs, it calls esp_task_wdt_reset() and
// logs `task not found` (seen: 3,268 lines on a V3 and 3,516 on a V4, #227). So format through ESP-IDF directly, as its
// own format_if_mount_failed does. The idle task stays on the watchdog, and the flash driver yields during an erase so it
// keeps running.
inline bool spiffsBeginOnBoot() {
#ifdef SPIFFS_BOOT_IDF_FORMAT
  if (SPIFFS.begin(false)) return true;
  return esp_spiffs_format(nullptr) == ESP_OK && SPIFFS.begin(false);
#else
  return SPIFFS.begin(true);
#endif
}
