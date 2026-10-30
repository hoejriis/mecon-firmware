#include "UpdRecord.h"
#include <string.h>

namespace Upd {

inline bool intact(const Record& r) {
  return r.magic == Record::MAGIC && memchr(r.job, 0, sizeof(r.job)) && r.job[0] &&
         memchr(r.version, 0, sizeof(r.version)) && memchr(r.prev_version, 0, sizeof(r.prev_version));
}

}  // namespace Upd

#if defined(SIM_FREE_HEAP)
#include "sim_config.h"
#include <errno.h>
#include <stdio.h>
#include <string>

namespace Upd {

static std::string recordPath() {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d + ".updjob";
}

bool recordLoad(Record* out) {
  memset(out, 0, sizeof(*out));
  FILE* f = fopen(recordPath().c_str(), "rb");
  if (!f) return false;
  const bool ok = fread(out, 1, sizeof(*out), f) == sizeof(*out) && fgetc(f) == EOF;
  fclose(f);
  if (ok && intact(*out)) return true;
  memset(out, 0, sizeof(*out));
  return false;
}

bool recordSave(const Record& r) {
  FILE* f = fopen(recordPath().c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(&r, 1, sizeof(r), f) == sizeof(r);
  return fclose(f) == 0 && ok;
}

bool recordClear() { return remove(recordPath().c_str()) == 0 || errno == ENOENT; }

}  // namespace Upd

#elif defined(ESP_PLATFORM)
#include <nvs.h>

namespace Upd {

static const char NS[] = "mecon";
static const char KEY[] = "updjob";

bool recordLoad(Record* out) {
  memset(out, 0, sizeof(*out));
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = sizeof(*out);
  const esp_err_t e = nvs_get_blob(h, KEY, out, &len);
  nvs_close(h);
  if (e == ESP_OK && len == sizeof(*out) && intact(*out)) return true;
  memset(out, 0, sizeof(*out));
  return false;
}

bool recordSave(const Record& r) {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_set_blob(h, KEY, &r, sizeof(r)) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

bool recordClear() {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
  const esp_err_t e = nvs_erase_key(h, KEY);
  const bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace Upd

#else

namespace Upd {
bool recordLoad(Record* out) { memset(out, 0, sizeof(*out)); return false; }
bool recordSave(const Record&) { return false; }
bool recordClear() { return true; }
}  // namespace Upd

#endif
