#include "UpdService.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "UpdB64.h"
#include "UpdFloor.h"
#include "UpdRecord.h"
#include "UpdSlot.h"
#include "helpers/CryptoRNG.h"
#include "helpers/MeconVersion.h"
#include "helpers/MemStats.h"
#include "helpers/BootTrace.h"
#include "helpers/cli/CliPolicy.h"
#include "helpers/deployment/DepAuthority.h"
#include "helpers/iface/IfaceJson.h"
#include "helpers/iface/JobLora.h"
#ifdef UPD_DOWNLOAD
#include "helpers/mqtt/MqttNet.h"
#if defined(SIM_FREE_HEAP)
#include <unistd.h>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#endif

namespace Upd {

using namespace Iface;

namespace {

// The paths this build supports.
uint8_t supportedKinds() {
#if defined(SIM_FREE_HEAP)
  return slotIsDfuBoard() ? 1u << KIND_BOOTLOADER_DFU : 1u << KIND_APP;   // a simulated board plays one target or the other
#elif defined(ESP_PLATFORM)
  return 1u << KIND_APP;
#elif defined(NRF52_PLATFORM)
  return 1u << KIND_BOOTLOADER_DFU;
#else
  return 0;
#endif
}

void hexOf(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

void hex(JsonWriter& w, const uint8_t* b, size_t n) {
  char s[65];
  hexOf(s, b, n < 32 ? n : 32);
  w.str(s);
}

int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// n bytes from exactly 2n lowercase hex characters.
bool unhex(const char* s, uint8_t* out, size_t n) {
  if (!s || strlen(s) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    const int hi = hexv(s[2 * i]), lo = hexv(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

}  // namespace

void UpdateService::begin(const char* hardware, const char* role, const char* hardware_names) {
  _hardware = hardware ? hardware : "";
  _hardware_names = hardware_names;
  _role = role ? role : "";
  slotBoot();
  _running_known = runningHash(_running_sha);
}

Device UpdateService::device() const {
  Device d = {};
  d.line = "public";
  d.hardware = _hardware;
  d.hardware_names = _hardware_names;
  d.hardware_rev = MECON_HARDWARE_REV;
  d.role = _role;
  d.variant = MECON_VARIANT;
  d.layout = MECON_LAYOUT;
  d.has_bootloader = false;   // no target here reports a bootloader version yet
  d.kinds = supportedKinds();
  uint32_t floor;
  d.security_floor = floorRead(&floor) ? floor : 0xFFFFFFFFu;   // unreadable: no install passes (fail closed)
  d.running_known = parseVersion(runningVersion(), &d.running);
  d.slot_size = slotCapacity();
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

bool UpdateService::owns(const Job& j) { return strcmp(j.op, OP) == 0; }

Job* UpdateService::openUpdateJob() const {
  JobTable* t = table();
  Job* j = t ? t->openJob() : nullptr;
  return j && owns(*j) ? j : nullptr;
}

// The "running" member every terminal update result carries: the image that runs once the result is in effect. A revert
// names the previous image, which the reboot that follows it runs.
int UpdateService::writeRunning(char* out, size_t cap, bool previous, const char* prev_version,
                                const uint8_t* prev_sha) const {
  char sha[65];
  const bool known = previous ? prev_sha != nullptr : _running_known;
  if (known) hexOf(sha, previous ? prev_sha : _running_sha, 32);
  return snprintf(out, cap, "\"running\":{\"version\":\"%s\",\"sha256\":%s%s%s}",
                  previous ? prev_version : runningVersion(), known ? "\"" : "", known ? sha : "null",
                  known ? "\"" : "");
}

// A job's result is one JSON object (Job::result): {<members>, "running": {...}}. previous: "running" names the image
// before this update (prev_version, prev_sha) rather than the image running now.
size_t UpdateService::resultObject(char* out, size_t cap, const char* members, bool previous, const char* prev_version,
                                   const uint8_t* prev_sha) const {
  int n = snprintf(out, cap, "{%s%s", members ? members : "", members && *members ? "," : "");
  if (n < 0 || (size_t)n >= cap) return 0;
  const int r = writeRunning(out + n, cap - n, previous, prev_version, prev_sha);
  if (r < 0 || (size_t)(n + r + 1) >= cap) return 0;
  out[n + r] = '}';
  out[n + r + 1] = 0;
  return (size_t)(n + r + 1);
}

// A terminal state for an update job, its result the given members and the image running now.
void UpdateService::finish(Job& j, uint8_t state, const char* members) {
  char buf[Job::RESULT_CAP];
  const size_t n = resultObject(buf, sizeof(buf), members, false, nullptr, nullptr);
  JobTable* t = table();
  if (t) t->setState(&j, state, n ? buf : nullptr, n);
}

void UpdateService::endTransfer(bool erase) {
  if (erase) {
    slotAbandon();
    _erasing = true;
  }
  if (_readback) { memset(_readback, 0, sizeof(*_readback)); delete _readback; _readback = nullptr; }
  memset(_token, 0, sizeof(_token));   // the token dies with the transfer
  if (_xfer) { delete _xfer; _xfer = nullptr; }
  bulkKeysWipe(&_keys);
  _ans_ready = false;
  _orphan[0] = 0;
#ifdef UPD_DOWNLOAD
  if (_phase == DOWNLOADING || _dl_started) _dl_close_pending = true;   // closed once the worker is idle (loop)
  _dl_started = _dl_connected = false;
#endif
  _phase = IDLE;
  _job[0] = 0;
  _size = _received = _readback_at = 0;
}

void UpdateService::failJob(const char* error, const char* extra) {
  JobTable* t = table();
  Job* j = nullptr;
  if (t && _job[0] && t->find(_job, &j) == JobTable::FOUND && !jobTerminal(j->state)) {
    char m[96];
    snprintf(m, sizeof(m), "\"error\":\"%s\"%s%s", error, extra ? "," : "", extra ? extra : "");
    finish(*j, JOB_FAILED, m);
  }
  endTransfer(true);
  persist();
}

// ---- update.manifest and update.status

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
  return stage(c, text, strlen(text), sig, now_ms);
}

// The staging itself, for a manifest that came whole or in parts. Takes the text, which it frees.
Status UpdateService::stage(OpContext& c, char* text, size_t len, const uint8_t sig[SIG_LEN], uint32_t now_ms) {
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

// update.manifest.part (LORA_UPDATES.md section 5): the manifest text and its signature in at most 4 parts. A part costs no
// signature check, so only the last one, which runs the staging, counts against the one-stage-a-second limit.
Status UpdateService::opManifestPart(OpContext& c, uint32_t now_ms) {
  const JsonDoc& d = *c.doc;
  const int a = c.req->args;
  char sha_hex[65];
  uint8_t sha[32];
  char b64[(ManifestParts::PART_MAX + 2) / 3 * 4 + 1];
  int64_t part, parts;
  if (a < 0 || !jsonGetString(d, jsonFind(d, a, "sha256"), sha_hex, sizeof(sha_hex), CS_NAME, 64) || !unhex(sha_hex, sha, 32) ||
      !jsonGetInt(d, jsonFind(d, a, "part"), 1, ManifestParts::MAX_PARTS, &part) ||
      !jsonGetInt(d, jsonFind(d, a, "parts"), 1, ManifestParts::MAX_PARTS, &parts) ||
      !jsonGetString(d, jsonFind(d, a, "data"), b64, sizeof(b64), CS_ANY, 4)) {
    c.error = "bad-args";
    return ST_INVALID;
  }
  const ManifestParts::Verdict v = _mp.add(sha, (unsigned)part, (unsigned)parts, b64, strlen(b64));
  if (v == ManifestParts::MP_BAD) { c.error = "bad-args"; return ST_INVALID; }
  if (v == ManifestParts::MP_NO_MEMORY) { c.error = "low-memory"; return ST_BUSY; }
  JsonWriter& w = *c.out;
  if (v == ManifestParts::MP_MORE) {
    w.key("sha256"); hex(w, sha, 32);
    w.key("part"); w.unum((uint32_t)part);
    w.key("parts"); w.unum((uint32_t)parts);
    w.key("held"); w.unum(_mp.held());
    return ST_OK;
  }
  // Whole. A stage too soon after the last keeps the parts: the same part again stages it.
  if (stageRateLimited(now_ms)) { c.error = "rate"; return ST_BUSY; }
  // The text and its signature, joined in one heap block that becomes the staged text.
  char* text = static_cast<char*>(malloc(ManifestParts::TOTAL_MAX + 1));
  if (!text) { c.error = "low-memory"; return ST_BUSY; }
  const size_t n = _mp.join(reinterpret_cast<uint8_t*>(text), ManifestParts::TOTAL_MAX);
  _mp.discard();   // staged or refused, the parts are spent
  uint8_t h[32], sig[SIG_LEN];
  if (n <= SIG_LEN || n - SIG_LEN > MANIFEST_MAX || memchr(text, 0, n - SIG_LEN)) { free(text); c.error = "bad-args"; return ST_INVALID; }
  const size_t len = n - SIG_LEN;
  memcpy(sig, text + len, SIG_LEN);
  text[len] = 0;
  Dep::sha256(reinterpret_cast<const uint8_t*>(text), len, h);
  if (memcmp(h, sha, 32) != 0) { free(text); c.error = "bad-args"; return ST_INVALID; }   // the key names the text it holds
  return stage(c, text, len, sig, now_ms);
}

Status UpdateService::opStatus(OpContext& c) {
  JsonWriter& w = *c.out;
  w.key("job");
  const Job* j = openUpdateJob();
  if (!j) {
    w.null();
  } else {
    w.beginObject();
    w.key("job"); w.str(j->id);
    w.key("state"); w.str(jobStateName(j->state));
    if (_phase != IDLE && strcmp(_job, j->id) == 0) {
      const bool lora = _phase == LORA && _xfer;   // frames, not bytes, are what a LoRa transfer counts
      const uint32_t frames = lora ? _xfer->next() : 0;
      w.key("received"); w.unum(lora ? (frames * BULK_DATA < _size ? frames * (uint32_t)BULK_DATA : _size) : _received);
      w.key("next"); w.unum(lora ? frames : _received);
      w.key("size"); w.unum(_size);
      w.key("version"); w.str(_version);
      w.key("sha256"); hex(w, _image_sha, 32);
    } else if (_attested && strcmp(_attested_job, j->id) == 0) {
      w.key("version"); w.str(runningVersion());
      w.key("sha256"); if (_running_known) hex(w, _running_sha, 32); else w.null();   // the nRF52 attests by version
    }
    w.endObject();
  }
  w.key("running"); w.beginObject();   // the image running now, whatever any job says
  w.key("version"); w.str(runningVersion());
  w.key("sha256"); if (_running_known) hex(w, _running_sha, 32); else w.null();
  w.endObject();
  uint32_t floor;
  w.key("security_floor");
  if (floorRead(&floor)) w.unum(floor); else w.null();   // null: the store cannot be read, and no install passes
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

// ---- update.offer

Status UpdateService::opOffer(OpContext& c, uint32_t now_ms) {
  JobTable* t = tableForNewJob();
  if (!t) { c.error = "no-memory"; return ST_BUSY; }
  char jid[Job::ID_CAP + 1];
  if (!_host.job_id || !_host.job_id(_host.user, *c.req, jid, sizeof(jid))) { c.error = "bad-args"; return ST_INVALID; }
  Job* known;
  const JobTable::Lookup l = t->find(jid, &known);
  // A job never runs twice, but a paused LoRa transfer may be offered again to resume it (a manager that cannot reuse the
  // job id, as a signed request cannot, names the same manifest under a new one: see `paused` below).
  const bool known_paused = l == JobTable::FOUND && known->state == JOB_PAUSED && jobLoraGet(*known, nullptr);
  if (l == JobTable::FOUND && !known_paused) { JobService::writeSummary(*c.out, *known); return ST_OK; }
  if (l == JobTable::EXPIRED) { c.error = "result-expired"; return ST_FAILED; }

  // The arguments: the manifest by hash, a push source, the confirmation window.
  const JsonDoc& d = *c.doc;
  const int a = c.req->args;
  char sha_hex[65];
  uint8_t msha[32];
  const int src = jsonFind(d, a, "source");
  if (!jsonGetString(d, jsonFind(d, a, "manifest_sha256"), sha_hex, sizeof(sha_hex), CS_NAME, 64) ||
      !unhex(sha_hex, msha, 32) || src < 0 || !isObject(d, src)) {
    c.error = "bad-args";
    return ST_INVALID;
  }
  bool push = false;
  const int tp = jsonFind(d, src, "push");
  const int tu = jsonFind(d, src, "url");
  const int tl = jsonFind(d, src, "lora");
  Url url = {};
  LoraSource ls = {};
  const bool by_url = tu >= 0;
  const bool by_lora = tl >= 0 && tu < 0;
  if (by_lora) {   // before preflight (LORA_UPDATES.md section 4)
    uint32_t now_unix = 0;
    const bool clock_known = deviceClock(&now_unix);
    if (d.tok[src].size != 1) { c.error = "bad-args"; return ST_INVALID; }
    const LoraSourceVerdict lv = readLoraSource(d, a, tl, clock_known, now_unix, &ls);
    if (lv == LS_BAD_ARGS) { c.error = "bad-args"; return ST_INVALID; }
    if (lv == LS_UNSUPPORTED) { c.error = "unsupported"; return ST_REFUSED; }   // the fast preset is a later slice
    if (slotCapacity() == 0 || slotIsDfuBoard()) { c.error = "unsupported-source"; return ST_REFUSED; }   // no inactive slot
  } else if (by_url) {
#ifdef UPD_DOWNLOAD
    char text[257];   // at most 256 bytes (section 6)
    if (d.tok[src].size != 1 || !jsonGetString(d, tu, text, sizeof(text), CS_ANY, 1) || !parseUrl(text, &url)) {
      c.error = "bad-args";
      return ST_INVALID;
    }
#else
    c.error = "unsupported-source";   // no network in this build
    return ST_REFUSED;
#endif
  } else if (tp < 0 || !jsonGetBool(d, tp, &push) || !push || d.tok[src].size != 1) {
    c.error = "bad-args";
    return ST_INVALID;
  }
  int64_t within = CONFIRM_DEFAULT_S;
  const int cf = jsonFind(d, a, "confirm");
  if (cf >= 0 && (!isObject(d, cf) || !jsonGetInt(d, jsonFind(d, cf, "within_s"), CONFIRM_MIN_S, CONFIRM_MAX_S, &within))) {
    c.error = "bad-args";
    return ST_INVALID;
  }

  // A board with nowhere to keep a security floor installs nothing (the T1000-E Companion, UPDATES.md section 4).
  if (!floorSupported()) { c.error = "unsupported"; return ST_REFUSED; }
  // Preflight (section 4); step 1, authority, was the dispatcher's.
  Job* open = t->openJob();                                                                                         // 3
  // A paused LoRa transfer of this very manifest: the offer resumes it, which erases nothing, so the rate limit (which
  // protects the slot from being erased again) does not apply.
  JobLora kept = {};
  Job* paused = by_lora && open && owns(*open) && open->state == JOB_PAUSED && jobLoraGet(*open, &kept) &&
                        memcmp(kept.manifest, msha, 32) == 0 ? open : nullptr;
  if (known_paused && !paused) { JobService::writeSummary(*c.out, *known); return ST_OK; }
  if (paused && ls.window_end <= kept.window_end) { c.error = "bad-args"; return ST_INVALID; }   // a resume needs a later window
  if (!paused && _offered_once && (uint32_t)(now_ms - _last_offer_ms) < OFFER_GAP_MS) { c.error = "rate"; return ST_BUSY; }   // 2
  // An image on trial (installed, its start-up checks not yet passed): the slot an install writes holds the image the
  // bootloader would return to, so nothing is written until this one is proven (esp_ota_begin refuses then too).
  if (runningState() == RS_PENDING) { c.error = "verifying"; return ST_BUSY; }
  static char g_open_error[16 + Job::ID_CAP];
  // Another kind of job, or an update whose image is activated but has not booted yet (writing the slot now would
  // overwrite the image the next boot runs): busy. An update job otherwise is superseded below, once preflight passes.
  if (open && (!owns(*open) || (open->state == JOB_AWAITING && !(_attested && strcmp(_attested_job, open->id) == 0)))) {
    snprintf(g_open_error, sizeof(g_open_error), "job-open:%s", open->id);
    c.error = g_open_error;
    return ST_BUSY;
  }
  const Slot* s = staged(msha);                                                                                     // 4
  if (!s) { c.error = "manifest-not-staged"; return ST_REFUSED; }
  Manifest* m = static_cast<Manifest*>(malloc(sizeof(Manifest)));
  if (!m) { c.error = "low-memory"; return ST_BUSY; }
  Device dev = device();
  if (by_url) dev.heap_needed = url.tls ? HEAP_NEEDED_HTTPS : HEAP_NEEDED_HTTP;   // step 12: the download's own connection
  const Anchor* an = nullptr;
  Verdict v = {true, nullptr, nullptr};
  if (!parseManifest(s->text, s->len, m)) v = {false, "invalid", "bad-manifest"};
  if (v.ok) v = checkKey(*m, nullptr, 0, &an);                                                   // 5
  if (v.ok && !verifyManifest(s->text, s->len, s->sig, an->key)) v = refuse("bad-release-signature");   // 6
  if (v.ok) v = checkTarget(*m, dev);                                                             // 7
  if (v.ok) v = checkInstall(*m, dev, _host.signed_now && _host.signed_now(_host.user));         // 8 to 12
  if (!v.ok) {
    free(m);
    c.error = v.error;
    return strcmp(v.status, "invalid") == 0 ? ST_INVALID : strcmp(v.status, "busy") == 0 ? ST_BUSY : ST_REFUSED;
  }
  // The nRF52 path (section 6): the bootloader installs the image, so there is no slot and no transfer here. The job opens
  // awaiting confirmation, since it spans the bootloader step, and the device resets into the bootloader's serial DFU
  // once this answer has gone out. The Reader then sends the package.
  if (m->kind == KIND_BOOTLOADER_DFU) {
    if (by_url) { free(m); c.error = "unsupported-source"; return ST_REFUSED; }   // the bootloader takes it over USB only
    if (by_lora) { free(m); c.error = "unsupported-source"; return ST_REFUSED; }   // and a LoRa source needs a slot
    if (open) {   // no supersede here: a DFU replaces the whole image, and one job at a time
      snprintf(g_open_error, sizeof(g_open_error), "job-open:%s", open->id);
      free(m);
      c.error = g_open_error;
      return ST_BUSY;
    }
    Job* j = t->open(jid, OP);
    if (!j) { free(m); c.error = "busy"; return ST_BUSY; }
    j->cls = (uint8_t)(1u << Cli::C_UPDATE);
    if (_host.record_caller) _host.record_caller(_host.user, *j);
    char res[64];
    const int n = snprintf(res, sizeof(res), "{\"dfu\":{\"version\":\"%s\"}}", m->version);   // what the next boot must run
    t->awaitConfirm(j, (uint32_t)within, false, BOOT_ABNORMAL);
    if (n > 0 && (size_t)n < sizeof(res)) t->setResult(j, res, (size_t)n);
    free(m);
    persist();
    _offered_once = true;
    _last_offer_ms = now_ms;
    _dfu_pending = true;
    snprintf(_dfu_job, sizeof(_dfu_job), "%s", j->id);
    _dfu_at = now_ms + DFU_DELAY_MS;   // counted again from the reply (replyWritten)
    JsonWriter& w = *c.out;
    w.key("job"); w.str(j->id);
    w.key("state"); w.str(jobStateName(j->state));
    w.key("dfu"); w.str("serial");
    return ST_OK;
  }

  if (paused) {
    const Status r = loraResume(c, *paused, ls, (uint32_t)within, *m, now_ms);
    free(m);
    return r;
  }

  CryptoRNG rng;
  uint8_t token[16];
  memset(token, 0, sizeof(token));
  rng.random(token, sizeof(token));
  bool nonzero = false;
  for (uint8_t b : token) nonzero = nonzero || b != 0;
  if (!nonzero) { free(m); c.error = "rng"; return ST_FAILED; }
  // An attested image this offer supersedes is confirmed by it: written down first, as job.confirm does, and undone if
  // the slot cannot be opened.
  Record conf;
  const bool confirming = open && open->state == JOB_AWAITING;
  if (confirming) {
    if (!recordLoad(&conf) || strcmp(conf.job, open->id) != 0) { free(m); c.error = "storage"; return ST_BUSY; }
    conf.confirmed = 1;
    if (!recordSave(conf)) { free(m); c.error = "storage"; return ST_BUSY; }
  }
  // The slot first: an offer that cannot open it supersedes nothing. A transfer it replaced cannot go on (the slot's
  // write was abandoned), so that job ends failed.
  _erasing = false;   // the new write takes the slot over
  if (!slotOpen(m->image_size)) {
    free(m);
    // The confirmation undone; if even that cannot be saved, the job is confirmed as the record already says, so the two
    // never disagree (a record that says confirmed under an open job would confirm it at its deadline).
    if (confirming) {
      conf.confirmed = 0;
      if (!recordSave(conf)) {
        finish(*open, JOB_CONFIRMED, "\"confirmed_by\":\"superseded\"");
        _attested = false;
        persist();
      }
    }
    if (open && !confirming) failJob("slot");
    c.error = "slot";
    return ST_FAILED;
  }
  // Passed: an open update job is superseded (section 7, step 7).
  if (open) {
    if (confirming) {   // its image runs, attested and proven (not on trial): confirmed by this offer
      finish(*open, JOB_CONFIRMED, "\"confirmed_by\":\"superseded\"");
      _attested = false;
    } else {   // never activated: cancelled; the new transfer writes over its slot
      endTransfer(false);
      finish(*open, JOB_CANCELLED, "\"reason\":\"superseded\"");
    }
  }
  Job* j = t->open(jid, OP);
  if (!j) { free(m); slotAbandon(); persist(); c.error = "busy"; return ST_BUSY; }
  j->cls = (uint8_t)(1u << Cli::C_UPDATE);
  if (_host.record_caller) _host.record_caller(_host.user, *j);

  _phase = by_url ? DOWNLOADING : by_lora ? LORA : RECEIVING;
  strncpy(_job, jid, sizeof(_job) - 1);
  _job[sizeof(_job) - 1] = 0;
  if (!by_url) memcpy(_token, token, sizeof(_token));   // a download has no chunks, so no token
#ifdef UPD_DOWNLOAD
  _url = url;
  _accepted_ms = now_ms;
#endif
  memcpy(_image_sha, m->image_sha256, 32);
  strncpy(_version, m->version, sizeof(_version) - 1);
  _version[sizeof(_version) - 1] = 0;
  _size = m->image_size;
  _received = 0;
  _within_s = (uint32_t)within;
  _last_chunk_ms = now_ms;
  _offered_once = true;
  _last_offer_ms = now_ms;
  if (by_lora) {
    if (!loraBegin(j->id, token, ls.block, loraDeadline(ls, now_ms), now_ms, 0)) {
      free(m);
      failJob("low-memory");
      c.error = "low-memory";
      return ST_BUSY;
    }
    JobLora l = {};   // what a reboot or a resume needs, in the job
    l.block = (uint8_t)ls.block;
    l.window_end = ls.window_end;
    l.frames_total = bulkFramesTotal(_size);
    memcpy(l.token, token, sizeof(l.token));
    memcpy(l.manifest, msha, 32);
    jobLoraPut(*j, l);
  }
  free(m);
  persist();

  char tok[33];
  hexOf(tok, token, sizeof(token));
  memset(token, 0, sizeof(token));
  JsonWriter& w = *c.out;
  w.key("job"); w.str(j->id);
  w.key("state"); w.str(jobStateName(j->state));
  if (!by_url) { w.key("transfer_token"); w.str(tok); }   // only here: never in the job, its events, update.status or a log
  w.key("next"); w.unum(0);
  if (by_lora) {   // the shape of the transfer (LORA_UPDATES.md section 4); nothing is compressed in this slice
    w.key("size"); w.unum(_size);
    w.key("transfer_size"); w.unum(_size);
    w.key("encoding"); w.str("raw");
    w.key("frame_bytes"); w.unum((uint32_t)BULK_DATA);
    w.key("frames_total"); w.unum(bulkFramesTotal(_size));
  }
  memset(tok, 0, sizeof(tok));
  return ST_OK;
}

// ---- update.chunk

Status UpdateService::opChunk(OpContext& c, uint32_t now_ms) {
  const JsonDoc& d = *c.doc;
  const int a = c.req->args;
  char job[Job::ID_CAP + 1], tok_hex[33];
  int64_t offset;
  uint8_t tok[16];
  const int td = jsonFind(d, a, "data");
  if (!jsonGetString(d, jsonFind(d, a, "job"), job, sizeof(job), CS_JOB, 1) ||
      !jsonGetString(d, jsonFind(d, a, "token"), tok_hex, sizeof(tok_hex), CS_NAME, 32) || !unhex(tok_hex, tok, 16) ||
      !jsonGetInt(d, jsonFind(d, a, "offset"), 0, 0xFFFFFFFF, &offset) || td < 0) {
    c.error = "bad-args";
    return ST_INVALID;
  }
  // The token is the authority: one answer for a wrong token and for no transfer, so a caller without it learns nothing.
  const bool ours = _phase == RECEIVING && strcmp(job, _job) == 0;
  const bool match = Dep::constTimeEqual(tok, _token, sizeof(tok));
  memset(tok, 0, sizeof(tok));
  if (!ours || !match) { c.error = "bad-token"; return ST_REFUSED; }

  JsonWriter& w = *c.out;
  if ((uint32_t)offset != _received) { w.key("next"); w.unum(_received); return ST_OK; }   // resume where it stands

  const size_t b64_cap = (CHUNK_MAX + 2) / 3 * 4 + 1;
  char* b64 = static_cast<char*>(malloc(b64_cap));
  uint8_t* bytes = static_cast<uint8_t*>(malloc(CHUNK_MAX));
  long n = -1;
  if (b64 && bytes && jsonGetString(d, td, b64, b64_cap, CS_ANY, 4)) n = base64Decode(b64, strlen(b64), bytes, CHUNK_MAX);
  free(b64);
  if (!b64 || !bytes) { free(bytes); c.error = "low-memory"; return ST_BUSY; }
  if (n <= 0) { free(bytes); c.error = "bad-args"; return ST_INVALID; }
  if ((uint32_t)n > _size - _received) { free(bytes); c.error = "too-large"; return ST_REFUSED; }
  const bool wrote = slotWrite(_received, bytes, (size_t)n);
  free(bytes);
  if (!wrote) { failJob("write-failed"); c.error = "write-failed"; return ST_FAILED; }

  JobTable* t = table();
  Job* j = nullptr;
  if (t && t->find(_job, &j) == JobTable::FOUND && j->state == JOB_ACCEPTED) {
    t->setState(j, JOB_PROGRESS);
    persist();
  }
  _received += (uint32_t)n;
  _last_chunk_ms = now_ms;
  if (_received == _size) {   // all of it: the read-back starts on the next loop pass
    _readback = new (std::nothrow) Dep::Sha256Ctx();
    if (!_readback) { failJob("low-memory"); c.error = "low-memory"; return ST_FAILED; }
    Dep::sha256Init(_readback);
    _readback_at = 0;
    _phase = VERIFYING;
  }
  w.key("next"); w.unum(_received);
  return ST_OK;
}

// ---- a transfer over LoRa (docs/contract/LORA_UPDATES.md)

// The number bulk frames name a transfer by: a signed request's own job number ("#<issuer>:<number>"), else a hash of the id.
uint32_t UpdateService::loraNumber(const char* id) {
  if (id[0] == '#') {
    const char* colon = strrchr(id, ':');
    if (colon && colon[1]) {
      uint64_t v = 0;
      const char* p = colon + 1;
      for (; *p >= '0' && *p <= '9' && v <= 0xFFFFFFFFull; p++) v = v * 10 + (uint64_t)(*p - '0');
      if (!*p && v <= 0xFFFFFFFFull) return (uint32_t)v;
    }
  }
  uint32_t h = 2166136261u;
  for (const char* p = id; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return h;
}

bool UpdateService::sinkWrite(void*, uint32_t offset, const uint8_t* data, size_t len) { return slotWrite(offset, data, len); }

// The answer waits for loraAnswer(): while one is waiting, the next cannot be sent, as in a duty window with no room.
bool UpdateService::sinkAnswer(void* ctx, const uint8_t* bytes, size_t len) {
  UpdateService* s = static_cast<UpdateService*>(ctx);
  if (s->_ans_ready || len != ANSWER_LEN) return false;
  memcpy(s->_ans, bytes, len);
  s->_ans_ready = true;
  return true;
}

// `next` is kept in the job and the job table saved: every 64 frames and at a pause (LoraTransfer decides when).
void UpdateService::sinkSave(void* ctx, uint32_t next) {
  UpdateService* s = static_cast<UpdateService*>(ctx);
  JobTable* t = s->table();
  Job* j = nullptr;
  JobLora l = {};
  if (t && t->find(s->_job, &j) == JobTable::FOUND && jobLoraGet(*j, &l)) {
    l.next = next;
    jobLoraPut(*j, l);
    s->persist();
  }
}

// The slot is erased by whoever ends the transfer (endTransfer), which knows why it ended.
void UpdateService::sinkErase(void*) {}

uint32_t UpdateService::loraDeadline(const LoraSource& ls, uint32_t now_ms) const {
  uint32_t u = 0;
  const bool known = deviceClock(&u);
  return windowDeadlineMs(ls.window_end, ls.issued_at, known, u, now_ms);
}

bool UpdateService::loraBegin(const char* job_id, const uint8_t token[16], unsigned block, uint32_t deadline_ms, uint32_t now_ms,
                              uint32_t next) {
  bulkKeys(token, 16, &_keys);
  _lora_no = loraNumber(job_id);
  const TransferSink sink = {this, sinkWrite, sinkAnswer, sinkSave, sinkErase};
  _xfer = new (std::nothrow) LoraTransfer(sink);
  if (!_xfer) return false;
  if (!_xfer->begin(_lora_no, _size, block, _keys, deadline_ms, now_ms, next)) {
    delete _xfer;
    _xfer = nullptr;
    return false;
  }
  return true;
}

// The offer that resumes a paused transfer: the same job, the same token and `next` as kept, a later window. After a
// reboot the transfer is built again from the job and the slot is reopened where the written bytes end.
Status UpdateService::loraResume(OpContext& c, Job& j, const LoraSource& ls, uint32_t within, const Manifest& m,
                                 uint32_t now_ms) {
  JobLora kept;
  if (!jobLoraGet(j, &kept)) { c.error = "bad-args"; return ST_INVALID; }
  JobTable* t = table();
  const uint32_t deadline = loraDeadline(ls, now_ms);
  if (_phase == LORA && _xfer && strcmp(_job, j.id) == 0) {   // paused in this boot: its state machine is still here
    if (!_xfer->resume(deadline, now_ms)) { c.error = "busy"; return ST_BUSY; }
  } else {
    const uint32_t size = m.image_size;
    const uint32_t frames = bulkFramesTotal(size);
    if (kept.frames_total != frames || kept.next > frames) { finish(j, JOB_FAILED, "\"error\":\"slot\""); persist(); c.error = "slot"; return ST_FAILED; }
    const uint32_t at = kept.next >= frames ? size : kept.next * (uint32_t)BULK_DATA;
    if (!slotResume(size, at)) {   // the slot no longer holds what the job says was written: nothing to resume
      finish(j, JOB_FAILED, "\"error\":\"slot\"");
      _orphan[0] = 0;
      persist();
      c.error = "slot";
      return ST_FAILED;
    }
    _erasing = false;
    _orphan[0] = 0;
    snprintf(_job, sizeof(_job), "%s", j.id);
    memcpy(_token, kept.token, sizeof(_token));
    memcpy(_image_sha, m.image_sha256, 32);
    snprintf(_version, sizeof(_version), "%s", m.version);
    _size = size;
    _received = 0;
    _phase = LORA;
    if (!loraBegin(j.id, kept.token, kept.block, deadline, now_ms, kept.next)) {
      failJob("low-memory");
      c.error = "low-memory";
      return ST_BUSY;
    }
  }
  _within_s = within;
  _last_chunk_ms = now_ms;
  _offered_once = true;
  _last_offer_ms = now_ms;
  const uint32_t next = _xfer->next();
  kept.next = next;
  kept.window_end = ls.window_end;
  kept.reason = 0;
  jobLoraPut(j, kept);
  if (t) t->setState(&j, next > 0 ? JOB_PROGRESS : JOB_ACCEPTED);
  j.confirm_left_s = 0;
  uint8_t tok_bytes[16];
  memcpy(tok_bytes, kept.token, sizeof(tok_bytes));
  char id[Job::ID_CAP + 1];
  snprintf(id, sizeof(id), "%s", j.id);
  const bool whole = _xfer->state() == TS_COMPLETE;
  if (whole) loraAfter(now_ms);   // everything was already here: on to the read-back
  persist();
  char tok[33];
  hexOf(tok, tok_bytes, sizeof(tok_bytes));
  JsonWriter& w = *c.out;
  w.key("job"); w.str(id);
  w.key("state"); w.str(jobStateName(whole ? JOB_PROGRESS : next > 0 ? JOB_PROGRESS : JOB_ACCEPTED));
  w.key("transfer_token"); w.str(tok);
  w.key("size"); w.unum(m.image_size);
  w.key("transfer_size"); w.unum(m.image_size);
  w.key("encoding"); w.str("raw");
  w.key("next"); w.unum(next);
  w.key("frame_bytes"); w.unum((uint32_t)BULK_DATA);
  w.key("frames_total"); w.unum(kept.frames_total);
  memset(tok, 0, sizeof(tok));
  memset(tok_bytes, 0, sizeof(tok_bytes));
  return ST_OK;
}

// After a frame or a tick: what the transfer's state says about the job.
void UpdateService::loraAfter(uint32_t now_ms) {
  (void)now_ms;
  if (_phase != LORA || !_xfer) return;
  JobTable* t = table();
  Job* j = nullptr;
  if (!t || t->find(_job, &j) != JobTable::FOUND || jobTerminal(j->state)) return;
  const TransferStatus st = _xfer->status();
  JobLora l = {};
  if (jobLoraGet(*j, &l) && l.next != st.next) {   // job.get reads the live figure; the table is saved by sinkSave
    l.next = st.next;
    jobLoraPut(*j, l);
  }
  switch (st.state) {
    case TS_PAUSED:
      if (j->state != JOB_PAUSED) {
        l.reason = (uint8_t)st.reason;
        jobLoraPut(*j, l);
        t->setState(j, JOB_PAUSED);
        j->confirm_left_s = JobLora::STALL_S;
        persist();
      }
      break;
    case TS_OPEN:
      if (j->state == JOB_ACCEPTED && st.next > 0) { t->setState(j, JOB_PROGRESS); persist(); }
      break;
    case TS_FAILED:
      failJob(st.reason == TR_STALLED ? "stalled" : st.reason == TR_WRITE ? "write-failed" : "cancelled");
      break;
    case TS_COMPLETE:
      loraComplete(*j);
      break;
  }
}

// Every frame is in the slot: the token is spent, and the read-back, activation and confirmation are a push's own.
void UpdateService::loraComplete(Job& j) {
  jobLoraClear(j);
  delete _xfer;
  _xfer = nullptr;
  bulkKeysWipe(&_keys);
  memset(_token, 0, sizeof(_token));
  JobTable* t = table();
  if (t && j.state == JOB_ACCEPTED) t->setState(&j, JOB_PROGRESS);
  _received = _size;
  _readback = new (std::nothrow) Dep::Sha256Ctx();
  if (!_readback) { failJob("low-memory"); return; }
  Dep::sha256Init(_readback);
  _readback_at = 0;
  _phase = VERIFYING;
  persist();
}

void UpdateService::loraFrame(const uint8_t* frame, size_t n, uint32_t now_ms) {
  if (_phase != LORA || !_xfer) return;
  _xfer->onFrame(frame, n, now_ms);
  loraAfter(now_ms);
}

bool UpdateService::loraAnswer(uint8_t out[ANSWER_LEN]) {
  if (!_ans_ready) return false;
  memcpy(out, _ans, ANSWER_LEN);
  _ans_ready = false;
  return true;
}

// ---- the work between requests

void UpdateService::activate() {
  JobTable* t = table();
  Job* j = nullptr;
  if (!t || t->find(_job, &j) != JobTable::FOUND || jobTerminal(j->state)) { endTransfer(true); return; }
  static const char VERIFIED[] = "{\"step\":\"verified\"}";
  t->setResult(j, VERIFIED, sizeof(VERIFIED) - 1);
  if (_host.jobs) _host.jobs->announce(*j);

  Record r;
  memset(&r, 0, sizeof(r));
  r.magic = Record::MAGIC;
  strncpy(r.job, _job, sizeof(r.job) - 1);
  strncpy(r.version, _version, sizeof(r.version) - 1);
  strncpy(r.prev_version, runningVersion(), sizeof(r.prev_version) - 1);
  memcpy(r.image_sha, _image_sha, 32);
  if (_running_known) memcpy(r.prev_sha, _running_sha, 32);
  r.image_size = _size;
  r.new_id = slotWritten();
  r.prev_id = slotRunning();
  if (!r.new_id || !r.prev_id || r.new_id == r.prev_id) { failJob("slot"); return; }
  if (!recordSave(r)) { failJob("storage"); return; }
  // The job spans the reboot awaiting confirmation; only an abnormal reset or the deadline reverts it (section 7).
  t->awaitConfirm(j, _within_s, false, BOOT_ABNORMAL);
  persist();
  if (!slotActivate()) {
    recordClear();
    finish(*j, JOB_FAILED, "\"error\":\"activate-failed\"");
    endTransfer(true);
    persist();
    return;
  }
  endTransfer(false);
  if (_host.reboot) _host.reboot(_host.user);
}

void UpdateService::loop(uint32_t now_ms) {
  // An offer of a bootloader-dfu image resets into the bootloader once its answer has gone out.
  if (_dfu_pending && (int32_t)(now_ms - _dfu_at) >= 0) {
    _dfu_pending = false;
    JobTable* t = table();
    Job* j = nullptr;
    if (t && t->find(_dfu_job, &j) == JobTable::FOUND && !jobTerminal(j->state)) slotEnterDfu();
  }
  // Stage 1 of the rollback: the image has run its start-up for VALID_AFTER_MS (the radio, the mesh and the interface
  // are up and the loop turns), so the bootloader stops reverting it. From here the confirmation deadline decides.
  if (!_valid_marked && now_ms >= VALID_AFTER_MS) {
    _valid_marked = true;
    if (runningState() == RS_PENDING) markProven();
  }
#ifdef UPD_DOWNLOAD
  // A download that ended (finished, failed, cancelled, superseded) gives its connection back once its worker is idle.
  if (_dl_close_pending && updatePlatformNet().asyncIdle()) {
    updatePlatformNet().close();
    _dl_close_pending = false;
  }
#endif
  if (_erasing) { _erasing = slotEraseStep(); return; }
  if (_orphan[0] && _phase == IDLE) {   // a paused LoRa job found at boot: if it was cancelled, its partial image goes
    JobTable* t = table();
    Job* j = nullptr;
    if (t && t->find(_orphan, &j) == JobTable::FOUND && jobTerminal(j->state)) {
      _orphan[0] = 0;
      slotAbandon();
      _erasing = true;
    }
  }
  if (_phase == IDLE) return;

  // A job.cancel ends the transfer: the job service cancelled it, and the slot is erased here.
  JobTable* t = table();
  Job* j = nullptr;
  if (!t || t->find(_job, &j) != JobTable::FOUND || jobTerminal(j->state)) {
    if (j && j->state == JOB_CANCELLED && !j->result_len) {   // every terminal update result names what runs
      char m[Job::RESULT_CAP];
      const size_t n = resultObject(m, sizeof(m), nullptr, false, nullptr, nullptr);
      if (n) { memcpy(j->result, m, n); j->result_len = (uint16_t)n; persist(); }
    }
    endTransfer(true);
    return;
  }

  if (_phase == LORA) {   // the window, the duty window, the manager's silence and the 7 days are the transfer's own timers
    if (_xfer) {
      _xfer->tick(now_ms);
      loraAfter(now_ms);
    }
    return;
  }
  if (_phase == RECEIVING) {
    if ((uint32_t)(now_ms - _last_chunk_ms) >= STALL_MS) failJob("stalled");
    return;
  }
#ifdef UPD_DOWNLOAD
  if (_phase == DOWNLOADING) {
    downloadStep(now_ms);
    return;
  }
#endif
  // VERIFYING: one piece of the slot per pass, hashed as read back from flash.
  static uint8_t buf[1024];
  const uint32_t end = _readback_at + READBACK_PIECE < _size ? _readback_at + READBACK_PIECE : _size;
  while (_readback_at < end) {
    const uint32_t n = end - _readback_at < sizeof(buf) ? end - _readback_at : sizeof(buf);
    if (!slotRead(_readback_at, buf, n)) { failJob("read-failed"); return; }
    Dep::sha256Update(_readback, buf, n);
    _readback_at += n;
  }
  if (_readback_at < _size) return;
  uint8_t got[32];
  Dep::sha256Finish(_readback, got);
  if (!Dep::constTimeEqual(got, _image_sha, 32)) { failJob("digest-mismatch"); return; }
  if (!slotFinish()) { failJob("bad-image"); return; }   // the platform's own check of the image format
  activate();
}

// ---- the download over IP (section 6)

#ifdef UPD_DOWNLOAD
// A short wait on the worker. Not delay(): on the simulated board that also runs the main loop's polling.
static void workerNap() {
#if defined(SIM_FREE_HEAP)
  usleep(5000);
#else
  vTaskDelay(pdMS_TO_TICKS(5));
#endif
}

void UpdateService::dlJob(void* self) {
  UpdateService* s = static_cast<UpdateService*>(self);
  s->_dl_err = s->dlConnect();
}

// On the network's worker: the name, the connection (TLS for https: encrypted, the server not authenticated, since the
// digest alone decides the image), the request, and the response head. The body is the main loop's.
const char* UpdateService::dlConnect() {
  MqttNet& n = updatePlatformNet();
  _dl_status = 0;
  uint32_t addr = 0;
  const bool literal = urlIpv4(_url.host, &addr);
  if (!literal && !n.resolve(_url.host, addr)) return "download-dns";
  const MqttOpenArgs a = {_url.tls, addr, _url.port, nullptr, nullptr, literal ? nullptr : _url.host, HEAD_TIMEOUT_MS};
  if (n.open(a) != MqttOpen::Ok) return "download-connect";
  Client& c = n.client();
  char req[sizeof(_url.path) + sizeof(_url.host) + 96];
  char port[8] = "";   // the Host header names the port when it is not the scheme's own
  if (_url.port != (_url.tls ? 443 : 80)) snprintf(port, sizeof(port), ":%u", (unsigned)_url.port);
  const int len = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s%s\r\nConnection: close\r\nUser-Agent: mecon\r\n\r\n",
                           _url.path, _url.host, port);
  if (len <= 0 || (size_t)len >= sizeof(req) || c.write(reinterpret_cast<const uint8_t*>(req), (size_t)len) != (size_t)len)
    return "download-connect";
  HttpHead h;
  char line[160];
  size_t ln = 0;
  const uint32_t t0 = millis();
  for (;;) {
    if ((uint32_t)(millis() - t0) >= HEAD_TIMEOUT_MS) return "download-head";
    const int ch = c.read();
    if (ch < 0) {
      if (!c.connected() && !c.available()) return "download-head";
      workerNap();
      continue;
    }
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (ln == 0) break;   // the blank line: the body follows
      line[ln] = 0;
      httpHeadLine(h, line);
      ln = 0;
      continue;
    }
    if (ln + 1 < sizeof(line)) line[ln++] = (char)ch;   // a longer line is cut: only the status and two headers matter
  }
  _dl_status = h.status;
  if (h.bad || !h.status) return "download-head";
  if (h.status != 200) return "download-status";
  if (h.chunked) return "download-encoding";
  if (h.has_length && h.length != _size) return "download-size";   // not the image the manifest names
  return nullptr;
}

void UpdateService::downloadStep(uint32_t now_ms) {
  MqttNet& n = updatePlatformNet();
  if ((uint32_t)(now_ms - _accepted_ms) >= DOWNLOAD_MS) { failJob("download-timeout"); return; }
  if (!_dl_started) {   // wait for the link and for an earlier download's connection to close, within the deadline
    if (_dl_close_pending || !n.linkUp() || !n.asyncIdle()) return;
    _dl_err = nullptr;
    if (!n.asyncStart(dlJob, this)) { failJob("download-connect"); return; }
    _dl_started = true;
    return;
  }
  if (!_dl_connected) {
    if (!n.asyncIdle()) return;   // the connect and the head run on the worker
    if (_dl_err) {
      char extra[24];
      snprintf(extra, sizeof(extra), "\"http_status\":%d", _dl_status);
      failJob(_dl_err, strcmp(_dl_err, "download-status") == 0 ? extra : nullptr);   // the status, where it is the reason
      return;
    }
    _dl_connected = true;
    JobTable* t = table();
    Job* j = nullptr;
    if (t && t->find(_job, &j) == JobTable::FOUND && j->state == JOB_ACCEPTED) {
      t->setState(j, JOB_PROGRESS);
      persist();
    }
  }
  // The body: what has arrived, at most one piece a pass, and never beyond image.size.
  Client& c = n.client();
  static uint8_t buf[1024];
  uint32_t taken = 0;
  while (taken < DOWNLOAD_PIECE && _received < _size) {
    const int avail = c.available();
    if (avail <= 0) break;
    uint32_t k = _size - _received;
    if (k > sizeof(buf)) k = sizeof(buf);
    if (k > (uint32_t)avail) k = (uint32_t)avail;
    const int got = c.read(buf, k);
    if (got <= 0) break;
    if (!slotWrite(_received, buf, (size_t)got)) { failJob("write-failed"); return; }
    _received += (uint32_t)got;
    taken += (uint32_t)got;
  }
  if (_received == _size) {   // all of it: the connection goes, the read-back starts
    n.close();
    _dl_started = _dl_connected = false;
    _readback = new (std::nothrow) Dep::Sha256Ctx();
    if (!_readback) { failJob("low-memory"); return; }
    Dep::sha256Init(_readback);
    _readback_at = 0;
    _phase = VERIFYING;
    return;
  }
  if (!taken && !c.connected() && !c.available()) failJob("download-short");   // the server closed before the end
}
#endif

// ---- boot, attestation, confirmation and revert
//
// Which image runs, and which the next boot runs, is read from the slots (UpdSlot.h) and the record that names them,
// never inferred from a hash alone. An update's image counts as proven only when its slot runs, the record names that
// slot, and the running image hashes to the manifest's digest. Anything less ends with the previous image as the one the
// device runs, or, where the device cannot tell which that is, with a failed job that says nothing was reverted.

#ifdef MECON_BOOT_TRACE
bool UpdateService::traceOpenJob(char* id, size_t cap, uint8_t* state) const {
  Job* j = openUpdateJob();
  if (!j) return false;
  snprintf(id, cap, "%s", j->id);
  *state = j->state;
  return true;
}
#endif

void UpdateService::markProven() {
  MECON_TRACE("markProven ms=%lu running=%s", (unsigned long)millis(), runningState() == RS_PENDING ? "pending" : "valid");
  if (runningState() == RS_PENDING) markValid();
  if (!_stranded) floorAtBoot();   // the floor follows a proven image (section 4), unless it runs unconfirmed
  settleRecord();
}

// The record is done with once its job is over and the image running is proven: no reset can bring the other back.
void UpdateService::settleRecord() {
  Record r;
  if (!recordLoad(&r) || runningState() != RS_VALID) return;   // on trial, or the state cannot be read: not settled
  JobTable* t = table();
  Job* j = nullptr;
  if (t && t->find(r.job, &j) == JobTable::FOUND && !jobTerminal(j->state)) return;   // still awaiting confirmation
  if (!r.confirmed && slotRunning() == r.new_id) return;   // an image never confirmed runs: never settled
  recordClear();
}

UpdateService::Back UpdateService::backToPrevious(const Record* r) {
  const uint32_t run = slotRunning();
  if (!run) return BACK_FAILED;
  if (r) {
    // Activated but not yet booted (a cancel in the moment before the reboot): the next boot must run this slot again.
    if (run == r->prev_id) return slotBooting() == run || slotSetBoot(run) ? BACK_RUNS : BACK_FAILED;
    if (run == r->new_id) return slotSetBoot(r->prev_id) ? BACK_REBOOT : BACK_FAILED;
    return BACK_UNKNOWN;
  }
  // No record: only the rollback state tells. An image on trial is a fresh install that never proved itself, and the
  // other slot holds the image it replaced. An image not on trial may be either, so nothing is changed.
  if (runningState() == RS_PENDING) return slotSetBoot(slotWritten()) ? BACK_REBOOT : BACK_FAILED;
  return BACK_UNKNOWN;
}

bool UpdateService::revertJob(Job& j, const Record* r, const char* reason) {
  _attested = false;
  const Back b = backToPrevious(r);
#ifdef MECON_BOOT_TRACE
  _recon = b == BACK_FAILED ? "revert-failed" : "reverted";
#endif
  if (b == BACK_FAILED) return false;
  char members[80];
  if (b == BACK_UNKNOWN) snprintf(members, sizeof(members), "\"error\":\"revert-unknown\",\"reverted\":false");
  else if (reason) snprintf(members, sizeof(members), "\"error\":\"%s\",\"reverted\":true", reason);
  else snprintf(members, sizeof(members), "\"reverted\":true");
  char buf[Job::RESULT_CAP];
  size_t n;
  if (b == BACK_REBOOT) {   // the reboot that follows runs the previous image, which the result names
    bool prev_known = false;
    if (r) for (uint8_t x : r->prev_sha) prev_known = prev_known || x != 0;
    n = resultObject(buf, sizeof(buf), members, true, r ? r->prev_version : "unknown", prev_known ? r->prev_sha : nullptr);
  } else {
    n = resultObject(buf, sizeof(buf), members, false, nullptr, nullptr);
  }
  JobTable* t = table();
  if (t) t->setState(&j, b == BACK_UNKNOWN ? JOB_FAILED : JOB_REVERTED, n ? buf : nullptr, n);
  // The record stays until the previous image is proven (settleRecord): the slot it now boots is on trial too, and a
  // reset in that trial returns to the new image, which the record then sends back again.
  if (b == BACK_REBOOT && _host.reboot) _host.reboot(_host.user);
  return true;
}

// The record's job was confirmed before the job table could be saved (a power cut after job.confirm): confirmed.
void UpdateService::confirmFromRecord(Job& j) {
#ifdef MECON_BOOT_TRACE
  _recon = "confirmed-by-record";
#endif
  finish(j, JOB_CONFIRMED, "\"confirmed_by\":\"record\"");
  markProven();
}

// At boot, after the job table: the install's outcome, then the floor. The floor rises to this image's compiled security
// version (section 4) unless the image is on trial (a fresh install raises it once its start-up checks pass or it is
// confirmed, so an image the bootloader rolls back never does) or runs unconfirmed because the previous one kept failing.
void UpdateService::bootReconcile() {
  reconcile();
  Job* o = openUpdateJob();
  if (o && o->state == JOB_PAUSED && jobLoraGet(*o, nullptr)) snprintf(_orphan, sizeof(_orphan), "%s", o->id);
  if (runningState() != RS_PENDING && !_stranded) floorAtBoot();
}

// The version a bootloader-dfu job expects the next boot to run, from its result ({"dfu":{"version":...}}); "" if none.
static void dfuVersionOf(const Job& j, char* out, size_t cap) {
  out[0] = 0;
  static const char KEY[] = "\"dfu\":{\"version\":\"";
  const char* p = j.result_len ? strstr(j.result, KEY) : nullptr;
  if (!p) return;
  p += sizeof(KEY) - 1;
  size_t n = 0;
  while (p[n] && p[n] != '"' && n + 1 < cap) { out[n] = p[n]; n++; }
  out[n] = 0;
}

// The nRF52 path at boot: the job the offer opened spans the bootloader step. The image that runs now is the installed one
// when its compiled version is the manifest's (the bootloader installs a package the device cannot hash); otherwise the
// DFU did not happen and the job ends.
void UpdateService::dfuReconcile() {
  Job* j = openUpdateJob();
  if (!j || j->state != JOB_AWAITING) return;
  char want[sizeof(_version)];
  dfuVersionOf(*j, want, sizeof(want));
  if (!want[0] || strcmp(want, runningVersion()) != 0) {
    finish(*j, JOB_FAILED, "\"error\":\"not-installed\"");
    persist();
    return;
  }
  _attested = true;
  strncpy(_attested_job, j->id, sizeof(_attested_job) - 1);
  char m[Job::RESULT_CAP];
  const int n = snprintf(m, sizeof(m), "{\"dfu\":{\"version\":\"%s\"},\"update\":{\"job\":\"%s\",\"version\":\"%s\",\"sha256\":null}}",
                         want, j->id, runningVersion());
  JobTable* t = table();
  if (t && n > 0 && (size_t)n < sizeof(m)) t->setResult(j, m, (size_t)n);
  persist();
  if (_host.jobs) _host.jobs->announce(*j);
}

void UpdateService::reconcile() {
  if (slotIsDfuBoard()) { dfuReconcile(); return; }
  JobTable* t = table();
  Record r;
  const bool have = recordLoad(&r);
  Job* j = nullptr;
  if (have) {
    if (!t || t->find(r.job, &j) != JobTable::FOUND) j = nullptr;
  } else {
    Job* o = openUpdateJob();
    if (o && o->state == JOB_AWAITING) j = o;
  }
  const uint32_t run = slotRunning();

  if (!have) {   // an update job awaits confirmation, and nothing names its slots (the record is lost or unreadable)
    if (j && !revertJob(*j, nullptr, "unproven")) finish(*j, JOB_FAILED, "\"error\":\"revert-failed\"");
    if (j) persist();
    return;
  }
  if (j && j->state == JOB_AWAITING) {
    if (r.confirmed && run && run == r.new_id) { confirmFromRecord(*j); persist(); return; }
    // Confirmed, but a reset before the image was marked valid returned to the previous slot: what runs is not what
    // was confirmed. The job ends as the rollback it is.
    if (r.confirmed) {
      if (!revertJob(*j, &r, "rolled-back")) finish(*j, JOB_FAILED, "\"error\":\"revert-failed\"");
      persist();
      settleRecord();
      return;
    }
    if (run && run == r.new_id && _running_known && memcmp(_running_sha, r.image_sha, 32) == 0) {
      // The new image runs and is the one installed: it attests, and awaits confirmation.
      _attested = true;
#ifdef MECON_BOOT_TRACE
      _recon = "attested";
#endif
      strncpy(_attested_job, r.job, sizeof(_attested_job) - 1);
      char sha[65], m[Job::RESULT_CAP];
      hexOf(sha, _running_sha, 32);
      const int n = snprintf(m, sizeof(m), "{\"update\":{\"job\":\"%s\",\"version\":\"%s\",\"sha256\":\"%s\"}}", r.job,
                             runningVersion(), sha);
      if (n > 0 && (size_t)n < sizeof(m)) t->setResult(j, m, (size_t)n);
      persist();
      if (_host.jobs) _host.jobs->announce(*j);
      return;
    }
    if (run && run == r.prev_id) {   // the bootloader returned to the previous slot (a reset before it was marked valid)
      if (!revertJob(*j, &r, "rolled-back")) finish(*j, JOB_FAILED, "\"error\":\"revert-failed\"");
      persist();
      settleRecord();
      return;
    }
    // The new slot runs but cannot show it holds the installed image: back to the previous one.
    if (!revertJob(*j, &r, "unproven")) finish(*j, JOB_FAILED, "\"error\":\"revert-failed\"");
    persist();
    return;
  }
  // The job is over, or gone from the table. A confirmed image stays; an image never confirmed does not, whatever the
  // job table lost, and however it came back (a reset during the previous image's own trial returns to it). Sent back a
  // bounded number of times: a previous image that keeps failing its trial is not swapped in for ever.
  if (!r.confirmed && run && run == r.new_id) {
    if (r.attempts >= MAX_SEND_BACK) {   // the previous image failed its trial each time: stay, and say so
      _stranded = true;
#ifdef MECON_BOOT_TRACE
      _recon = "stranded";
#endif
      strncpy(_stranded_job, r.job, sizeof(_stranded_job) - 1);
      return;
    }
    r.attempts++;
    if (!recordSave(r)) return;
    if (backToPrevious(&r) == BACK_REBOOT && _host.reboot) _host.reboot(_host.user);
    return;
  }
  // The previous image runs after an unconfirmed install: the next boot must run it too, whatever the slots last named.
  if (!r.confirmed && run && run == r.prev_id && slotBooting() != run) slotSetBoot(run);
  settleRecord();
}

bool UpdateService::jobRevert(Job& j) {
  if (j.state == JOB_PAUSED) {   // a paused transfer waited 7 days for its manager (LORA_UPDATES.md section 7)
    if (_phase != IDLE && strcmp(_job, j.id) == 0) {
      endTransfer(true);
    } else {
      _orphan[0] = 0;
      slotAbandon();
      _erasing = true;
    }
    finish(j, JOB_FAILED, "\"error\":\"stalled\"");
    return true;
  }
  JobLora cut;
  if (j.state != JOB_AWAITING && _phase == IDLE && jobLoraGet(j, &cut)) {
    // A LoRa transfer cut by a reboot is not lost: its token and `next` are in the job and its slot holds what was
    // written, so it pauses, and the same manifest offered again resumes it (section 7).
    cut.reason = Iface::TR_MANAGER;
    jobLoraPut(j, cut);
    JobTable* t = table();
    if (t) t->setState(&j, JOB_PAUSED);
    j.confirm_left_s = JobLora::STALL_S;
    snprintf(_orphan, sizeof(_orphan), "%s", j.id);
    return true;
  }
  if (j.state != JOB_AWAITING) {   // cut while the image was arriving (the boot rule): the transfer is lost
    if (_phase != IDLE && strcmp(_job, j.id) == 0) endTransfer(true);
    finish(j, JOB_FAILED, "\"error\":\"interrupted\"");
    return true;
  }
  if (slotIsDfuBoard()) {   // no slot to return to (section 7, step 6): the image stays, and the job says so
    char want[sizeof(_version)];
    dfuVersionOf(j, want, sizeof(want));
    const bool installed = want[0] && strcmp(want, runningVersion()) == 0;
    finish(j, JOB_FAILED, installed ? "\"error\":\"unconfirmed\",\"reverted\":false" : "\"error\":\"not-installed\"");
    _attested = false;
    return true;
  }
  Record r;
  const bool have = recordLoad(&r) && strcmp(r.job, j.id) == 0;
  if (have && r.confirmed && slotRunning() == r.new_id) { confirmFromRecord(j); return true; }   // confirmed before a cut
  return revertJob(j, have ? &r : nullptr, nullptr);
}

bool UpdateService::jobConfirm(Job& j) {
  // Only once the new image runs and shows it is the one installed: reachability alone never confirms (section 7, step 4).
  if (!_attested || strcmp(_attested_job, j.id) != 0) return false;
  if (slotIsDfuBoard()) {   // the version is the attestation here, and there is no record or rollback window
    JobTable* t = table();
    char m[Job::RESULT_CAP];
    const size_t n = resultObject(m, sizeof(m), nullptr, false, nullptr, nullptr);
    if (t && n) t->setResult(&j, m, n);
    _attested = false;
    return true;
  }
  Record r;
  if (!recordLoad(&r) || strcmp(r.job, j.id) != 0) return false;
  r.confirmed = 1;   // saved before the job table: a power cut between the two still reads as confirmed
  if (!recordSave(r)) return false;
  markProven();   // a reset must not take a confirmed image back
  JobTable* t = table();
  char m[Job::RESULT_CAP];
  const size_t n = resultObject(m, sizeof(m), nullptr, false, nullptr, nullptr);
  if (t && n) t->setResult(&j, m, n);
  _attested = false;
  return true;
}

void UpdateService::writeAttestation(JsonWriter& w) const {
  if (!_attested && !_stranded) return;
  w.key("update"); w.beginObject();
  w.key("job"); w.str(_attested ? _attested_job : _stranded_job);
  w.key("version"); w.str(runningVersion());
  w.key("sha256");
  if (_running_known) hex(w, _running_sha, 32); else w.null();   // null where the platform does not hash its image (nRF52)
  if (_stranded) { w.key("unconfirmed"); w.boolean(true); }   // runs although its job ended without a confirmation
  w.endObject();
}

}  // namespace Upd
