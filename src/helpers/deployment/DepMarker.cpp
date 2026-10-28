#include "DepMarker.h"

#if defined(SIM_FREE_HEAP)   // the simulated board: sim_base sets it, no firmware build does
#include "sim_config.h"
#include <errno.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace Dep {

static std::string markerPath() {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d + ".prov";   // beside the data directory, not inside it: a simulated format removes the directory's content
}

MarkerState markerRead() {
  struct stat st;
  // Test knob of the simulated board only: a file beside the data directory makes it behave like a platform that has
  // nowhere outside the file system to keep the marker (the nRF52 until its reserved page exists).
  if (stat((markerPath() + ".unsupported").c_str(), &st) == 0) return MK_UNSUPPORTED;
  if (stat(markerPath().c_str(), &st) == 0) return MK_PRESENT;
  return errno == ENOENT ? MK_ABSENT : MK_PRESENT;   // anything but "not there" reads as present
}

bool markerSet() {
  FILE* f = fopen(markerPath().c_str(), "wb");
  if (!f) return false;
  const uint8_t one = 1;
  const bool ok = fwrite(&one, 1, 1, f) == 1;
  fclose(f);
  return ok && markerRead() == MK_PRESENT;
}

bool markerClear() {
  if (unlink(markerPath().c_str()) != 0 && errno != ENOENT) return false;
  return markerRead() == MK_ABSENT;
}

}  // namespace Dep

#elif defined(ESP_PLATFORM)
#include <nvs.h>
#include <nvs_flash.h>

namespace Dep {

static const char NS[] = "mecon";
static const char KEY[] = "prov";

// NVS is erased by the factory reset without being initialised again (the device reboots); the rebuild path writes the
// marker back in the same boot, so an uninitialised store is initialised here.
static esp_err_t openNs(nvs_open_mode_t mode, nvs_handle_t* h) {
  esp_err_t e = nvs_open(NS, mode, h);
  if (e == ESP_ERR_NVS_NOT_INITIALIZED) {
    if (nvs_flash_init() != ESP_OK) return e;
    e = nvs_open(NS, mode, h);
  }
  return e;
}

MarkerState markerRead() {
  nvs_handle_t h;
  esp_err_t e = openNs(NVS_READONLY, &h);
  if (e == ESP_ERR_NVS_NOT_FOUND) return MK_ABSENT;   // the namespace was never created
  if (e != ESP_OK) return MK_PRESENT;
  uint8_t v = 0;
  e = nvs_get_u8(h, KEY, &v);
  nvs_close(h);
  if (e == ESP_ERR_NVS_NOT_FOUND) return MK_ABSENT;
  return MK_PRESENT;   // found, or unreadable: fail closed
}

bool markerSet() {
  nvs_handle_t h;
  if (openNs(NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = nvs_set_u8(h, KEY, 1) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok && markerRead() == MK_PRESENT;
}

bool markerClear() {
  nvs_handle_t h;
  esp_err_t e = openNs(NVS_READWRITE, &h);
  if (e != ESP_OK) return markerRead() == MK_ABSENT;
  e = nvs_erase_key(h, KEY);
  const bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok && markerRead() == MK_ABSENT;
}

}  // namespace Dep

#elif defined(NRF52_PLATFORM) && defined(MECON_MARKER_PAGE)
// One flash page outside every file system, named by the env (MECON_MARKER_PAGE): it must lie in the bootloader's
// preserved app-data region and in no file system of that build (docs/contract/AUTHORITY.md section 4). Written
// through the core's flash cache, which uses the SoftDevice's flash calls while BLE runs and is shared with InternalFS.
#include "DepMarkerPage.h"
#include <flash/flash_nrf5x.h>

extern "C" uint32_t __etext, __data_start__, __data_end__;

namespace Dep {

namespace {

class NrfMarkerPage : public FlashPage {
public:
  size_t size() const override { return FLASH_NRF52_PAGE_SIZE; }
  bool read(uint32_t off, void* buf, size_t n) override {
    return flash_nrf5x_read(buf, (uint32_t)MECON_MARKER_PAGE + off, n) == (int)n;
  }
  bool write(uint32_t off, const void* buf, size_t n) override {
    if (flash_nrf5x_write((uint32_t)MECON_MARKER_PAGE + off, buf, n) != (int)n) return false;
    flash_nrf5x_flush();   // programmed now; the read-back is what proves it
    return true;
  }
  bool erase() override { return flash_nrf5x_erase((uint32_t)MECON_MARKER_PAGE); }
};

// The page must be page-aligned and above the whole firmware image (code and the .data image after it); an image that
// has grown into it makes the page code, and the platform then has no marker store at all.
bool pageUsable() {
  const uint32_t image_end = (uint32_t)&__etext + ((uint32_t)&__data_end__ - (uint32_t)&__data_start__);
  return ((uint32_t)MECON_MARKER_PAGE % FLASH_NRF52_PAGE_SIZE) == 0 && image_end <= (uint32_t)MECON_MARKER_PAGE;
}

}  // namespace

MarkerState markerRead() {
  if (!pageUsable()) return MK_UNSUPPORTED;
  NrfMarkerPage p;
  return markerPageRead(p);
}

bool markerSet() {
  if (!pageUsable()) return false;
  NrfMarkerPage p;
  return markerPageWrite(p, true);
}

bool markerClear() {
  if (!pageUsable()) return false;
  NrfMarkerPage p;
  return markerPageWrite(p, false);
}

}  // namespace Dep

#else

namespace Dep {
MarkerState markerRead() { return MK_UNSUPPORTED; }
bool markerSet() { return false; }
bool markerClear() { return false; }
}  // namespace Dep

#endif
