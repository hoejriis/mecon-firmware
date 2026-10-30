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
#include "helpers/cli/CliPolicy.h"
#include "helpers/deployment/DepAuthority.h"
#include "helpers/iface/IfaceJson.h"

namespace Upd {

using namespace Iface;

namespace {

// The paths this build supports.
uint8_t supportedKinds() {
#if defined(SIM_FREE_HEAP) || defined(ESP_PLATFORM)
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

void UpdateService::begin(const char* hardware, const char* role) {
  _hardware = hardware ? hardware : "";
  _role = role ? role : "";
  slotBoot();
  _running_known = runningHash(_running_sha);
  // The floor rises to this image's compiled security version (section 4), at once unless the image is on trial: a fresh
  // install raises it once its start-up checks pass or it is confirmed, so an image the bootloader rolls back never does.
  if (runningState() != RS_PENDING) floorAtBoot();
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
  _phase = IDLE;
  _job[0] = 0;
  _size = _received = _readback_at = 0;
}

void UpdateService::failJob(const char* error) {
  JobTable* t = table();
  Job* j = nullptr;
  if (t && _job[0] && t->find(_job, &j) == JobTable::FOUND && !jobTerminal(j->state)) {
    char m[48];
    snprintf(m, sizeof(m), "\"error\":\"%s\"", error);
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
  w.key("job");
  const Job* j = openUpdateJob();
  if (!j) {
    w.null();
  } else {
    w.beginObject();
    w.key("job"); w.str(j->id);
    w.key("state"); w.str(jobStateName(j->state));
    if (_phase != IDLE && strcmp(_job, j->id) == 0) {
      w.key("received"); w.unum(_received);
      w.key("next"); w.unum(_received);
      w.key("size"); w.unum(_size);
      w.key("version"); w.str(_version);
      w.key("sha256"); hex(w, _image_sha, 32);
    } else if (_attested && strcmp(_attested_job, j->id) == 0) {
      w.key("version"); w.str(runningVersion());
      w.key("sha256"); hex(w, _running_sha, 32);
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
  JobTable* t = table();
  if (!t) { c.error = "no-memory"; return ST_BUSY; }
  char jid[Job::ID_CAP + 1];
  if (!_host.job_id || !_host.job_id(_host.user, *c.req, jid, sizeof(jid))) { c.error = "bad-args"; return ST_INVALID; }
  Job* known;
  const JobTable::Lookup l = t->find(jid, &known);
  if (l == JobTable::FOUND) { JobService::writeSummary(*c.out, *known); return ST_OK; }   // a job never runs twice
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
  if (jsonFind(d, src, "url") >= 0) { c.error = "unsupported-source"; return ST_REFUSED; }   // IP delivery: a later stage
  if (tp < 0 || !jsonGetBool(d, tp, &push) || !push || d.tok[src].size != 1) { c.error = "bad-args"; return ST_INVALID; }
  int64_t within = CONFIRM_DEFAULT_S;
  const int cf = jsonFind(d, a, "confirm");
  if (cf >= 0 && (!isObject(d, cf) || !jsonGetInt(d, jsonFind(d, cf, "within_s"), CONFIRM_MIN_S, CONFIRM_MAX_S, &within))) {
    c.error = "bad-args";
    return ST_INVALID;
  }

  // Preflight (section 4); step 1, authority, was the dispatcher's.
  if (_offered_once && (uint32_t)(now_ms - _last_offer_ms) < OFFER_GAP_MS) { c.error = "rate"; return ST_BUSY; }   // 2
  // An image on trial (installed, its start-up checks not yet passed): the slot an install writes holds the image the
  // bootloader would return to, so nothing is written until this one is proven (esp_ota_begin refuses then too).
  if (runningState() == RS_PENDING) { c.error = "verifying"; return ST_BUSY; }
  Job* open = t->openJob();                                                                                         // 3
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
  const Device dev = device();
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

  CryptoRNG rng;
  uint8_t token[16];
  memset(token, 0, sizeof(token));
  rng.random(token, sizeof(token));
  bool nonzero = false;
  for (uint8_t b : token) nonzero = nonzero || b != 0;
  if (!nonzero) { free(m); c.error = "rng"; return ST_FAILED; }
  // The slot first: an offer that cannot open it supersedes nothing. A transfer it replaced cannot go on (the slot's
  // write was abandoned), so that job ends failed.
  _erasing = false;   // the new write takes the slot over
  if (!slotOpen(m->image_size)) {
    free(m);
    if (open && open->state != JOB_AWAITING) failJob("slot");
    c.error = "slot";
    return ST_FAILED;
  }
  // Passed: an open update job is superseded (section 7, step 7).
  if (open) {
    if (open->state == JOB_AWAITING) {   // its image runs, attested and proven (not on trial): confirmed by this offer
      Record r;
      if (recordLoad(&r) && strcmp(r.job, open->id) == 0) {
        r.confirmed = 1;
        recordSave(r);
      }
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

  _phase = RECEIVING;
  strncpy(_job, jid, sizeof(_job) - 1);
  _job[sizeof(_job) - 1] = 0;
  memcpy(_token, token, sizeof(_token));
  memcpy(_image_sha, m->image_sha256, 32);
  strncpy(_version, m->version, sizeof(_version) - 1);
  _version[sizeof(_version) - 1] = 0;
  _size = m->image_size;
  _received = 0;
  _within_s = (uint32_t)within;
  _last_chunk_ms = now_ms;
  _offered_once = true;
  _last_offer_ms = now_ms;
  free(m);
  persist();

  char tok[33];
  hexOf(tok, token, sizeof(token));
  memset(token, 0, sizeof(token));
  JsonWriter& w = *c.out;
  w.key("job"); w.str(j->id);
  w.key("state"); w.str(jobStateName(j->state));
  w.key("transfer_token"); w.str(tok);   // only here: never in the job, its events, update.status or a log
  w.key("next"); w.unum(0);
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
  // Stage 1 of the rollback: the image has run its start-up for VALID_AFTER_MS (the radio, the mesh and the interface
  // are up and the loop turns), so the bootloader stops reverting it. From here the confirmation deadline decides.
  if (!_valid_marked && now_ms >= VALID_AFTER_MS) {
    _valid_marked = true;
    if (runningState() == RS_PENDING) markProven();
  }
  if (_erasing) { _erasing = slotEraseStep(); return; }
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

  if (_phase == RECEIVING) {
    if ((uint32_t)(now_ms - _last_chunk_ms) >= STALL_MS) failJob("stalled");
    return;
  }
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

// ---- boot, attestation, confirmation and revert
//
// Which image runs, and which the next boot runs, is read from the slots (UpdSlot.h) and the record that names them,
// never inferred from a hash alone. An update's image counts as proven only when its slot runs, the record names that
// slot, and the running image hashes to the manifest's digest. Anything less ends with the previous image as the one the
// device runs, or, where the device cannot tell which that is, with a failed job that says nothing was reverted.

void UpdateService::markProven() {
  if (runningState() == RS_PENDING) markValid();
  floorAtBoot();   // the floor follows an image once it is proven (section 4)
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
  // Kept across a reboot to the previous image: the boot that finds the previous slot running clears it, so a power cut
  // before that reboot still reads as a revert.
  if (b != BACK_REBOOT) recordClear();
  if (b == BACK_REBOOT && _host.reboot) _host.reboot(_host.user);
  return true;
}

// The record's job was confirmed before the job table could be saved (a power cut after job.confirm): confirmed.
void UpdateService::confirmFromRecord(Job& j) {
  finish(j, JOB_CONFIRMED, "\"confirmed_by\":\"record\"");
  recordClear();
  markProven();
}

void UpdateService::bootReconcile() {
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
    if (r.confirmed) { confirmFromRecord(*j); persist(); return; }
    if (run && run == r.new_id && _running_known && memcmp(_running_sha, r.image_sha, 32) == 0) {
      // The new image runs and is the one installed: it attests, and awaits confirmation.
      _attested = true;
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
      return;
    }
    // The new slot runs but cannot show it holds the installed image: back to the previous one.
    if (!revertJob(*j, &r, "unproven")) finish(*j, JOB_FAILED, "\"error\":\"revert-failed\"");
    persist();
    return;
  }
  // The job is over, or gone from the table. A confirmed image stays; an image never confirmed does not, whatever the
  // job table lost.
  if (r.confirmed || run != r.new_id) {
    if (!r.confirmed && run == r.prev_id && slotBooting() != run) slotSetBoot(run);
    recordClear();
    return;
  }
  if (backToPrevious(&r) == BACK_REBOOT && _host.reboot) _host.reboot(_host.user);
}

bool UpdateService::jobRevert(Job& j) {
  if (j.state != JOB_AWAITING) {   // cut while the image was arriving (the boot rule): the transfer is lost
    if (_phase != IDLE && strcmp(_job, j.id) == 0) endTransfer(true);
    finish(j, JOB_FAILED, "\"error\":\"interrupted\"");
    return true;
  }
  Record r;
  const bool have = recordLoad(&r) && strcmp(r.job, j.id) == 0;
  if (have && r.confirmed) { confirmFromRecord(j); return true; }   // confirmed before a power cut: not reverted
  return revertJob(j, have ? &r : nullptr, nullptr);
}

bool UpdateService::jobConfirm(Job& j) {
  // Only once the new image runs and shows it is the one installed: reachability alone never confirms (section 7, step 4).
  if (!_attested || strcmp(_attested_job, j.id) != 0) return false;
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
  if (!_attested) return;
  w.key("update"); w.beginObject();
  w.key("job"); w.str(_attested_job);
  w.key("version"); w.str(runningVersion());
  w.key("sha256"); hex(w, _running_sha, 32);
  w.endObject();
}

}  // namespace Upd
