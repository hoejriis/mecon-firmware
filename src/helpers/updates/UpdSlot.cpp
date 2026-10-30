#include "UpdSlot.h"
#include <string.h>
#include "helpers/MeconVersion.h"
#include "helpers/deployment/DepHmac.h"

#if defined(SIM_FREE_HEAP)   // the simulated board: sim_base sets it, no firmware build does
#include "sim_config.h"
#include <stdio.h>
#include <string>

namespace Upd {

namespace {

// The slot table: the slot running now, the slot the next boot runs, and the state of the running image, as the ESP32
// bootloader keeps them in its otadata.
enum { ST_VALID = 0, ST_NEW = 1, ST_PENDING = 2 };
struct OtaData { int run, boot, state; };

uint32_t g_size = 0, g_written = 0;
bool g_open = false;

std::string base() {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d;
}
std::string slotPath(int s) { return base() + (s ? ".ota1" : ".ota0"); }

OtaData readOta() {
  OtaData o = {0, 0, ST_VALID};
  FILE* f = fopen((base() + ".otadata").c_str(), "r");
  if (f) {
    if (fscanf(f, "%d %d %d", &o.run, &o.boot, &o.state) != 3 || o.run < 0 || o.run > 1 || o.boot < 0 || o.boot > 1)
      o = {0, 0, ST_VALID};
    fclose(f);
  }
  return o;
}
bool writeOta(const OtaData& o) {
  FILE* f = fopen((base() + ".otadata").c_str(), "w");
  if (!f) return false;
  fprintf(f, "%d %d %d\n", o.run, o.boot, o.state);
  return fclose(f) == 0;
}
int inactive() { return 1 - readOta().run; }

}  // namespace

void slotBoot() {
  OtaData o = readOta();
  if (o.state == ST_PENDING) {   // reset before the image was marked valid: back to the previous slot
    o.run = o.boot = 1 - o.run;
    o.state = ST_VALID;
  } else if (o.boot != o.run) {
    o.run = o.boot;
    o.state = o.state == ST_NEW ? ST_PENDING : ST_VALID;
  }
  writeOta(o);
}

uint32_t slotCapacity() { return 0x640000; }   // the Heltec V4's app partition, so the simulated board answers as one

bool slotOpen(uint32_t size) {
  if (size > slotCapacity()) return false;
  FILE* f = fopen(slotPath(inactive()).c_str(), "wb");
  if (!f) return false;
  fclose(f);
  g_open = true;
  g_size = size;
  g_written = 0;
  return true;
}

bool slotWrite(uint32_t offset, const uint8_t* p, size_t n) {
  if (!g_open || offset != g_written || n > g_size - g_written) return false;
  FILE* f = fopen(slotPath(inactive()).c_str(), "ab");
  if (!f) return false;
  const bool ok = fwrite(p, 1, n, f) == n;
  if (fclose(f) != 0 || !ok) return false;
  g_written += (uint32_t)n;
  return true;
}

bool slotRead(uint32_t offset, uint8_t* p, size_t n) {
  FILE* f = fopen(slotPath(inactive()).c_str(), "rb");
  if (!f) return false;
  const bool ok = fseek(f, (long)offset, SEEK_SET) == 0 && fread(p, 1, n, f) == n;
  fclose(f);
  return ok;
}

// As esp_ota_end, which refuses anything that is not an ESP32 image: the first byte is the image magic, 0xE9.
bool slotFinish() {
  if (!g_open || g_written != g_size || g_size == 0) return false;
  uint8_t magic = 0;
  g_open = false;
  return slotRead(0, &magic, 1) && magic == 0xE9;
}

void slotAbandon() {
  g_open = false;
  FILE* f = fopen(slotPath(inactive()).c_str(), "wb");   // emptied: no bootable image left
  if (f) fclose(f);
}

bool slotEraseStep() { return false; }   // abandon() already emptied the file

bool slotActivate() {
  OtaData o = readOta();
  o.boot = 1 - o.run;
  o.state = ST_NEW;
  return writeOta(o);
}

bool slotRevert() {
  OtaData o = readOta();
  o.boot = 1 - o.run;
  o.state = ST_VALID;   // the previous image ran before: nothing to verify
  return writeOta(o);
}

bool runningHash(uint8_t out[32]) {
  Dep::Sha256Ctx c;
  Dep::sha256Init(&c);
  FILE* f = fopen(slotPath(readOta().run).c_str(), "rb");
  size_t total = 0;
  if (f) {
    uint8_t buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) { Dep::sha256Update(&c, buf, n); total += n; }
    fclose(f);
  }
  if (!total) {   // the image the node was started with: no file stands for it
    static const char FACTORY[] = "mecon-sim-factory-image";
    Dep::sha256Update(&c, reinterpret_cast<const uint8_t*>(FACTORY), sizeof(FACTORY) - 1);
  }
  Dep::sha256Finish(&c, out);
  return true;
}

RunState runningState() { return readOta().state == ST_PENDING ? RS_PENDING : RS_VALID; }

const char* runningVersion() {
  static bool read = false;
  static char v[48];
  if (!read) {
    read = true;
    v[0] = 0;
    FILE* f = fopen((base() + ".version").c_str(), "r");
    if (f) {
      if (fgets(v, sizeof(v), f)) v[strcspn(v, "\r\n")] = 0;
      fclose(f);
    }
  }
  return v[0] ? v : mecon::versionFull();
}

bool markValid() {
  OtaData o = readOta();
  o.state = ST_VALID;
  return writeOta(o);
}

}  // namespace Upd

#elif defined(ESP_PLATFORM)
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// The Arduino core marks a freshly installed image valid at once unless this says otherwise (esp32-hal-misc.c). The
// update service marks it valid itself, once its start-up checks pass (UPDATES.md section 6, stage 1).
extern "C" bool verifyRollbackLater() { return true; }

namespace Upd {

namespace {

const uint32_t SECTOR = 4096;
esp_ota_handle_t g_h = 0;
const esp_partition_t* g_part = nullptr;
uint32_t g_written = 0, g_erase_at = 0, g_erase_end = 0;

}  // namespace

void slotBoot() {}   // the bootloader did it

uint32_t slotCapacity() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

bool slotOpen(uint32_t size) {
  if (g_h) { esp_ota_abort(g_h); g_h = 0; }
  g_part = esp_ota_get_next_update_partition(nullptr);
  if (!g_part || size == 0 || size > g_part->size) return false;
  g_written = 0;
  g_erase_at = g_erase_end = 0;
  // Sequential writes: each sector is erased when the write reaches it, so opening never erases the whole slot at once.
  if (esp_ota_begin(g_part, OTA_WITH_SEQUENTIAL_WRITES, &g_h) != ESP_OK) { g_h = 0; return false; }
  return true;
}

bool slotWrite(uint32_t offset, const uint8_t* p, size_t n) {
  if (!g_h || offset != g_written) return false;
  if (esp_ota_write(g_h, p, n) != ESP_OK) return false;
  g_written += (uint32_t)n;
  return true;
}

bool slotRead(uint32_t offset, uint8_t* p, size_t n) {
  return g_part && esp_partition_read(g_part, offset, p, n) == ESP_OK;
}

bool slotFinish() {
  if (!g_h) return false;
  const esp_err_t e = esp_ota_end(g_h);   // checks the image's own format and checksum
  g_h = 0;
  return e == ESP_OK;
}

void slotAbandon() {
  if (g_h) { esp_ota_abort(g_h); g_h = 0; }
  if (!g_part) return;
  esp_partition_erase_range(g_part, 0, SECTOR);   // the image header: the slot holds nothing bootable from here on
  g_erase_at = SECTOR;
  g_erase_end = (g_written + SECTOR - 1) / SECTOR * SECTOR;
  if (g_erase_end > g_part->size) g_erase_end = g_part->size;
}

bool slotEraseStep() {
  if (!g_part || g_erase_at >= g_erase_end) return false;
  esp_partition_erase_range(g_part, g_erase_at, SECTOR);
  g_erase_at += SECTOR;
  return g_erase_at < g_erase_end;
}

bool slotActivate() { return g_part && esp_ota_set_boot_partition(g_part) == ESP_OK; }

bool slotRevert() {
  const esp_partition_t* prev = esp_ota_get_next_update_partition(nullptr);   // the other slot: the image before this one
  return prev && esp_ota_set_boot_partition(prev) == ESP_OK;
}

bool runningHash(uint8_t out[32]) {
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (!run) return false;
  const esp_partition_pos_t pos = {run->address, run->size};
  esp_image_metadata_t md;
  if (esp_image_get_metadata(&pos, &md) != ESP_OK || md.image_len == 0 || md.image_len > run->size) return false;
  Dep::Sha256Ctx c;
  Dep::sha256Init(&c);
  static uint8_t buf[1024];
  for (uint32_t at = 0; at < md.image_len; at += sizeof(buf)) {
    const uint32_t n = md.image_len - at < sizeof(buf) ? md.image_len - at : sizeof(buf);
    if (esp_partition_read(run, at, buf, n) != ESP_OK) return false;
    Dep::sha256Update(&c, buf, n);
  }
  Dep::sha256Finish(&c, out);
  return true;
}

RunState runningState() {
  esp_ota_img_states_t st;
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (!run || esp_ota_get_state_partition(run, &st) != ESP_OK) return RS_UNKNOWN;
  return st == ESP_OTA_IMG_PENDING_VERIFY ? RS_PENDING : RS_VALID;
}

bool markValid() { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }

const char* runningVersion() { return mecon::versionFull(); }

}  // namespace Upd

#else

namespace Upd {
void slotBoot() {}
uint32_t slotCapacity() { return 0; }
bool slotOpen(uint32_t) { return false; }
bool slotWrite(uint32_t, const uint8_t*, size_t) { return false; }
bool slotRead(uint32_t, uint8_t*, size_t) { return false; }
bool slotFinish() { return false; }
void slotAbandon() {}
bool slotEraseStep() { return false; }
bool slotActivate() { return false; }
bool slotRevert() { return false; }
bool runningHash(uint8_t*) { return false; }
RunState runningState() { return RS_UNKNOWN; }
bool markValid() { return false; }
const char* runningVersion() { return mecon::versionFull(); }
}  // namespace Upd

#endif
