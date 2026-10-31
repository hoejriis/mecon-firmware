#include "UpdFloor.h"

#if defined(SIM_FREE_HEAP)   // the simulated board: sim_base sets it, no firmware build does
#include "sim_config.h"
#include <errno.h>
#include <stdio.h>
#include <string>

namespace Upd {

static std::string floorPath() {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d + ".floor";   // beside the data directory, so a simulated format leaves it
}

bool floorRead(uint32_t* out) {
  *out = 0;
  FILE* f = fopen(floorPath().c_str(), "rb");
  if (!f) return errno == ENOENT;   // nothing stored; any other failure is unreadable
  unsigned long v = 0;
  const int n = fscanf(f, "%lu", &v);
  fclose(f);
  if (n != 1) return false;
  *out = (uint32_t)v;
  return true;
}

bool floorSupported() { return true; }

bool floorRaise(uint32_t v) {
  uint32_t cur;
  if (!floorRead(&cur)) return false;
  if (cur >= v) return true;
  FILE* f = fopen(floorPath().c_str(), "wb");
  if (!f) return false;
  fprintf(f, "%lu\n", (unsigned long)v);
  fclose(f);
  return floorRead(&cur) && cur >= v;
}

}  // namespace Upd

#elif defined(ESP_PLATFORM)
#include <nvs.h>
#include <nvs_flash.h>

namespace Upd {

static const char NS[] = "mecon";
static const char KEY[] = "secfloor";

static esp_err_t openNs(nvs_open_mode_t mode, nvs_handle_t* h) {
  esp_err_t e = nvs_open(NS, mode, h);
  if (e == ESP_ERR_NVS_NOT_INITIALIZED) {   // right after a factory reset's erase, in the same boot
    if (nvs_flash_init() != ESP_OK) return e;
    e = nvs_open(NS, mode, h);
  }
  return e;
}

bool floorRead(uint32_t* out) {
  *out = 0;
  nvs_handle_t h;
  const esp_err_t o = openNs(NVS_READONLY, &h);
  if (o == ESP_ERR_NVS_NOT_FOUND) return true;   // the namespace was never created: nothing stored
  if (o != ESP_OK) return false;
  uint32_t v = 0;
  const esp_err_t e = nvs_get_u32(h, KEY, &v);
  nvs_close(h);
  if (e == ESP_ERR_NVS_NOT_FOUND) return true;
  if (e != ESP_OK) return false;
  *out = v;
  return true;
}

bool floorSupported() { return true; }

bool floorRaise(uint32_t v) {
  uint32_t cur;
  if (!floorRead(&cur)) return false;
  if (cur >= v) return true;
  nvs_handle_t h;
  if (openNs(NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_set_u32(h, KEY, v) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok && floorRead(&cur) && cur >= v;
}

}  // namespace Upd

#elif defined(NRF52_PLATFORM) && defined(MECON_FLOOR_PAGE)
// One flash page named by the env (MECON_FLOOR_PAGE): in the bootloader's preserved app-data region, below the
// provisioned marker's page, and in no file system of that build. Written through the core's flash cache, as the marker.
#include <flash/flash_nrf5x.h>
#include "UpdFloorPage.h"

extern "C" uint32_t __etext, __data_start__, __data_end__;

namespace Upd {

namespace {

class NrfFloorPage : public Dep::FlashPage {
public:
  size_t size() const override { return FLASH_NRF52_PAGE_SIZE; }
  bool read(uint32_t off, void* buf, size_t n) override {
    return flash_nrf5x_read(buf, (uint32_t)MECON_FLOOR_PAGE + off, n) == (int)n;
  }
  bool write(uint32_t off, const void* buf, size_t n) override {
    if (flash_nrf5x_write((uint32_t)MECON_FLOOR_PAGE + off, buf, n) != (int)n) return false;
    flash_nrf5x_flush();   // programmed now; the read-back is what proves it
    return true;
  }
  bool erase() override { return flash_nrf5x_erase((uint32_t)MECON_FLOOR_PAGE); }
};

// Page-aligned and above the whole firmware image: an image grown into the page would make it code.
bool pageUsable() {
  const uint32_t image_end = (uint32_t)&__etext + ((uint32_t)&__data_end__ - (uint32_t)&__data_start__);
  return ((uint32_t)MECON_FLOOR_PAGE % FLASH_NRF52_PAGE_SIZE) == 0 && image_end <= (uint32_t)MECON_FLOOR_PAGE;
}

}  // namespace

bool floorSupported() { return pageUsable(); }

bool floorRead(uint32_t* out) {
  *out = 0;
  if (!pageUsable()) return false;   // no store: unreadable, so no install passes
  NrfFloorPage p;
  return floorPageRead(p, out);
}

bool floorRaise(uint32_t v) {
  if (!pageUsable()) return false;
  NrfFloorPage p;
  return floorPageRaise(p, v);
}

}  // namespace Upd

#elif defined(NRF52_PLATFORM)
// An nRF52 env that names no floor page: the T1000-E Companion, whose ExtraFS covers the whole preserved region. It has
// nowhere to keep a floor, so it reads as unreadable and every install is refused, never taken without one.

namespace Upd {
bool floorSupported() { return false; }
bool floorRead(uint32_t* out) { *out = 0; return false; }
bool floorRaise(uint32_t) { return false; }
}  // namespace Upd

#else

namespace Upd {
bool floorSupported() { return false; }
bool floorRead(uint32_t* out) { *out = 0; return true; }   // not kept here (see UpdFloor.h)
bool floorRaise(uint32_t) { return false; }
}  // namespace Upd

#endif
