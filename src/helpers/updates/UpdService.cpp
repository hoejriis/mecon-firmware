#include "UpdService.h"
#include <stdlib.h>
#include <string.h>
#include "UpdFloor.h"
#include "helpers/MeconVersion.h"
#include "helpers/MemStats.h"
#include "helpers/deployment/DepAuthority.h"
#include "helpers/deployment/DepHmac.h"
#include "helpers/iface/IfaceJson.h"

#if defined(ESP_PLATFORM) && !defined(SIM_FREE_HEAP)
#include <esp_ota_ops.h>
#endif

namespace Upd {

using namespace Iface;

namespace {

// The paths this build supports and the size of its update slot.
uint8_t supportedKinds() {
#if defined(SIM_FREE_HEAP) || defined(ESP_PLATFORM)
  return 1u << KIND_APP;
#elif defined(NRF52_PLATFORM)
  return 1u << KIND_BOOTLOADER_DFU;
#else
  return 0;
#endif
}

uint32_t slotSize() {
#if defined(SIM_FREE_HEAP)
  return 0x640000;   // the Heltec V4's app partition, so the simulated board answers as one
#elif defined(ESP_PLATFORM)
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
#else
  return 0;
#endif
}

void hex(JsonWriter& w, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  char s[65];
  for (size_t i = 0; i < n && i < 32; i++) { s[2 * i] = H[b[i] >> 4]; s[2 * i + 1] = H[b[i] & 15]; }
  s[2 * (n < 32 ? n : 32)] = 0;
  w.str(s);
}

}  // namespace

void UpdateService::begin(const char* hardware, const char* role) {
  _hardware = hardware ? hardware : "";
  _role = role ? role : "";
}

Device UpdateService::device() const {
  Device d = {};
  d.line = "public";
  d.hardware = _hardware;
  d.hardware_rev = MECON_HARDWARE_REV;
  d.role = _role;
  d.variant = MECON_VARIANT;
  d.layout = MECON_LAYOUT;
  d.has_bootloader = false;   // no target here reports a bootloader version yet
  d.kinds = supportedKinds();
  d.security_floor = floorRead();
  d.running_known = parseVersion(mecon::versionFull(), &d.running);
  d.slot_size = slotSize();
  const MemStats m = memStats();
  d.free_heap = m.known ? m.free : 0;
  d.heap_needed = HEAP_NEEDED;
  return d;
}

const UpdateService::Slot* UpdateService::staged(const uint8_t sha256[32]) const {
  for (const Slot& s : _s)
    if (s.text && memcmp(s.sha256, sha256, 32) == 0) return &s;
  return nullptr;
}

Status UpdateService::opManifest(OpContext& c, uint32_t now_ms) {
  if (_staged_once && (uint32_t)(now_ms - _last_stage_ms) < STAGE_GAP_MS) { c.error = "rate"; return ST_BUSY; }

  char* text = static_cast<char*>(malloc(MANIFEST_MAX + 1));
  if (!text) { c.error = "low-memory"; return ST_BUSY; }
  char sig_b64[89];
  uint8_t sig[SIG_LEN];
  const int ta = c.req->args;
  if (!jsonGetString(*c.doc, jsonFind(*c.doc, ta, "manifest"), text, MANIFEST_MAX + 1, CS_ANY, 1) ||
      !jsonGetString(*c.doc, jsonFind(*c.doc, ta, "manifest_sig"), sig_b64, sizeof(sig_b64), CS_ANY, 88) ||
      !Dep::base64ToSig(sig_b64, strlen(sig_b64), sig)) {
    free(text);
    c.error = "bad-args";
    return ST_INVALID;
  }
  const size_t len = strlen(text);
  _staged_once = true;
  _last_stage_ms = now_ms;   // a refused stage counts too: each costs a signature check

  Manifest* m = static_cast<Manifest*>(malloc(sizeof(Manifest)));
  if (!m) { free(text); c.error = "low-memory"; return ST_BUSY; }
  const Device dev = device();
  const Anchor* a = nullptr;
  Verdict v = {true, nullptr, nullptr};
  if (!parseManifest(text, len, m)) v = {false, "invalid", "bad-manifest"};
  if (v.ok) v = checkKey(*m, nullptr, 0, &a);                                                   // 5
  if (v.ok && !verifyManifest(text, len, sig, a->key)) v = refuse("bad-release-signature");    // 6
  if (v.ok) v = checkTarget(*m, dev);                                                            // 7
  if (!v.ok) {
    free(m);
    free(text);
    c.error = v.error;
    return strcmp(v.status, "invalid") == 0 ? ST_INVALID : ST_REFUSED;
  }

  uint8_t h[32];
  Dep::sha256(reinterpret_cast<const uint8_t*>(text), len, h);
  Slot* slot = nullptr;
  for (Slot& s : _s)
    if (s.text && memcmp(s.sha256, h, 32) == 0) slot = &s;   // the same manifest again: refresh it
  if (!slot)
    for (Slot& s : _s)
      if (!s.text) { slot = &s; break; }
  if (!slot) slot = (int32_t)(_s[0].staged_ms - _s[1].staged_ms) <= 0 ? &_s[0] : &_s[1];   // evict the older
  free(slot->text);
  slot->text = text;
  slot->len = (uint16_t)len;
  memcpy(slot->sha256, h, 32);
  memcpy(slot->sig, sig, SIG_LEN);
  slot->staged_ms = now_ms;

  const Verdict rest = checkInstall(*m, dev, false);   // what steps 8 to 12 would say for an unsigned offer now
  JsonWriter& w = *c.out;
  w.key("sha256"); hex(w, h, 32);
  w.key("version"); w.str(m->version);
  w.key("preflight"); w.str(rest.ok ? "ok" : rest.error);
  free(m);
  return ST_OK;
}

Status UpdateService::opStatus(OpContext& c) {
  JsonWriter& w = *c.out;
  w.key("job"); w.null();   // no update job runs yet: update.offer is the next stage of this work
  w.key("security_floor"); w.unum(floorRead());
  w.key("staged"); w.beginArray();
  Manifest* m = static_cast<Manifest*>(malloc(sizeof(Manifest)));
  for (const Slot& s : _s) {
    if (!s.text) continue;
    w.beginObject();
    w.key("sha256"); hex(w, s.sha256, 32);
    w.key("version");
    if (m && parseManifest(s.text, s.len, m)) w.str(m->version); else w.null();
    w.endObject();
  }
  free(m);
  w.endArray();
  return ST_OK;
}

}  // namespace Upd
