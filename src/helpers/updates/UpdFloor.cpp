#include "UpdFloor.h"

#if defined(SIM_FREE_HEAP)   // the simulated board: sim_base sets it, no firmware build does
#include "sim_config.h"
#include <stdio.h>
#include <string>

namespace Upd {

static std::string floorPath() {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d + ".floor";   // beside the data directory, so a simulated format leaves it
}

uint32_t floorRead() {
  FILE* f = fopen(floorPath().c_str(), "rb");
  if (!f) return 0;
  unsigned long v = 0;
  const int n = fscanf(f, "%lu", &v);
  fclose(f);
  return n == 1 ? (uint32_t)v : 0;
}

bool floorRaise(uint32_t v) {
  if (floorRead() >= v) return true;
  FILE* f = fopen(floorPath().c_str(), "wb");
  if (!f) return false;
  fprintf(f, "%lu\n", (unsigned long)v);
  fclose(f);
  return floorRead() >= v;
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

uint32_t floorRead() {
  nvs_handle_t h;
  if (openNs(NVS_READONLY, &h) != ESP_OK) return 0;
  uint32_t v = 0;
  if (nvs_get_u32(h, KEY, &v) != ESP_OK) v = 0;
  nvs_close(h);
  return v;
}

bool floorRaise(uint32_t v) {
  if (floorRead() >= v) return true;
  nvs_handle_t h;
  if (openNs(NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_set_u32(h, KEY, v) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok && floorRead() >= v;
}

}  // namespace Upd

#else

namespace Upd {
uint32_t floorRead() { return 0; }
bool floorRaise(uint32_t) { return false; }
}  // namespace Upd

#endif
