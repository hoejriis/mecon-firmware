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

#else

namespace Upd {
bool floorRead(uint32_t* out) { *out = 0; return true; }   // not kept here (see UpdFloor.h)
bool floorRaise(uint32_t) { return false; }
}  // namespace Upd

#endif
