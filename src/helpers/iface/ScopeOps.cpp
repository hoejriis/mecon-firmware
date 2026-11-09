#include "ScopeOps.h"
#include <stdio.h>
#include <string.h>
#include "Wipe.h"
#include "../RegionMap.h"
#include "../TransportKeyStore.h"

namespace Iface {

namespace {

int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}
void put32(uint8_t* o, size_t& at, uint32_t v) { for (int i = 0; i < 4; i++) o[at++] = (uint8_t)(v >> (8 * i)); }
uint32_t get32(const uint8_t* p, size_t at) {
  return (uint32_t)p[at] | ((uint32_t)p[at + 1] << 8) | ((uint32_t)p[at + 2] << 16) | ((uint32_t)p[at + 3] << 24);
}

// The previous scope, bound to the job: magic, job id, whether there was one, its name and key, a CRC.
const uint32_t SNAP_MAGIC = 0x31535053;   // "SPS1"
const size_t SNAP_MAX = 4 + 1 + Job::ID_CAP + 1 + (SCOPE_NAME_MAX + 1) + SCOPE_KEY_BYTES + 4;

size_t snapshot(ScopeHost& h, const char* job, uint8_t* out) {
  char name[SCOPE_NAME_MAX + 1] = {0};
  uint8_t key[SCOPE_KEY_BYTES] = {0};
  const bool had = h.get(h.role_user, name, key);
  size_t at = 0;
  put32(out, at, SNAP_MAGIC);
  const size_t jl = strlen(job);
  out[at++] = (uint8_t)jl;
  memcpy(out + at, job, jl); at += jl;
  out[at++] = had ? 1 : 0;
  memcpy(out + at, name, sizeof(name)); at += sizeof(name);
  memcpy(out + at, key, sizeof(key)); at += sizeof(key);
  wipe(key, sizeof(key));
  put32(out, at, crc32(out, at));
  return at;
}

bool parse(const uint8_t* in, size_t n, const char* job, bool* had, char name[SCOPE_NAME_MAX + 1], uint8_t key[SCOPE_KEY_BYTES]) {
  if (n < 4 + 1 + 1 + SCOPE_NAME_MAX + 1 + SCOPE_KEY_BYTES + 4 || get32(in, 0) != SNAP_MAGIC || get32(in, n - 4) != crc32(in, n - 4))
    return false;
  size_t at = 4;
  const size_t jl = in[at++];
  if (jl > Job::ID_CAP || strlen(job) != jl || at + jl + 1 + SCOPE_NAME_MAX + 1 + SCOPE_KEY_BYTES + 4 != n ||
      memcmp(in + at, job, jl) != 0)
    return false;
  at += jl;
  *had = in[at++] == 1;
  memcpy(name, in + at, SCOPE_NAME_MAX + 1); at += SCOPE_NAME_MAX + 1;
  name[SCOPE_NAME_MAX] = 0;
  memcpy(key, in + at, SCOPE_KEY_BYTES);
  return true;
}

bool hexKey(const char* s, uint8_t out[SCOPE_KEY_BYTES]) {
  if (strlen(s) != 2 * SCOPE_KEY_BYTES) return false;
  uint8_t any = 0;
  for (unsigned i = 0; i < 2 * SCOPE_KEY_BYTES; i++) {
    const char ch = s[i];
    const int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (v < 0) return false;
    if (i & 1) out[i / 2] |= (uint8_t)v; else out[i / 2] = (uint8_t)(v << 4);
  }
  for (unsigned i = 0; i < SCOPE_KEY_BYTES; i++) any |= out[i];
  return any != 0;   // an all-zero key is no scope
}

char g_error[16 + Job::ID_CAP];

struct Pending {
  Job* job = nullptr;
  JobTable* table = nullptr;
  uint32_t within = 300;
};

// As RegionOps and the management channel: the previous scope is written before the job is opened, and the job before the
// scope changes.
Status beginChange(OpContext& c, ScopeHost& h, const char* op, Pending& p, bool* answered) {
  *answered = false;
  const JsonDoc& d = *c.doc;
  const int conf = argOf(c, "confirm");
  if (conf >= 0) {
    if (!isObject(d, conf)) return ST_INVALID;
    int64_t within = 300;
    const int wt = jsonFind(d, conf, "within_s");
    if (wt >= 0 && !jsonGetInt(d, wt, 30, 900, &within)) return ST_INVALID;
    p.within = (uint32_t)within;
  }
  const bool cable = h.cable && h.cable(h.user);
  if (conf < 0 && !cable) { c.error = "confirm-required"; return ST_INVALID; }
  if (conf < 0) return ST_OK;
  if (!h.jobs || !h.snap || !h.job_id || !h.record_caller) { c.error = "confirm-unsupported"; return ST_INVALID; }
  char jid[Job::ID_CAP + 1];
  if (!h.job_id(h.user, *c.req, jid, sizeof(jid))) return ST_INVALID;
  p.table = h.jobs->table();
  if (!p.table) { c.error = "no-memory"; return ST_BUSY; }
  Job* known = nullptr;
  const JobTable::Lookup l = p.table->find(jid, &known);
  if (l == JobTable::FOUND) { JobService::writeSummary(*c.out, *known); *answered = true; return ST_OK; }
  if (l == JobTable::EXPIRED) { c.error = "result-expired"; return ST_FAILED; }
  if (const Job* open = p.table->openJob()) {
    snprintf(g_error, sizeof(g_error), "job-open:%s", open->id);
    c.error = g_error;
    return ST_BUSY;
  }
  uint8_t img[SNAP_MAX];
  const size_t n = snapshot(h, jid, img);
  bool ok = h.snap->save(img, n);
  wipe(img, sizeof(img));
  if (ok) {
    p.job = p.table->open(jid, op);
    ok = p.job != nullptr;
  }
  if (ok) {
    p.job->cls = h.cls;
    h.record_caller(h.user, *p.job);
    ok = h.jobs->persist();
    if (!ok) p.table->setState(p.job, JOB_FAILED);
  }
  if (!ok) { h.snap->discard(); p.job = nullptr; c.error = "storage"; return ST_BUSY; }
  return ST_OK;
}

const char APPLY_FAILED_REVERTED[] = "{\"error\":\"apply-failed\",\"reverted\":true}";
const char APPLY_FAILED_KEPT[] = "{\"error\":\"apply-failed\",\"reverted\":false}";

Status finish(OpContext& c, ScopeHost& h, Pending& p, bool applied) {
  if (!p.job) {
    if (!applied) { c.error = "storage"; return ST_FAILED; }
    return ST_OK;
  }
  JsonWriter& w = *c.out;
  w.key("job"); w.str(p.job->id);
  if (applied) {
    p.table->awaitConfirm(p.job, p.within, true);
    w.key("job_state"); w.str("awaiting-confirm");
    w.key("confirm_left_s"); w.unum(p.within);
  } else {
    const bool back = scopeRevert(h, *p.job);
    const char* r = back ? APPLY_FAILED_REVERTED : APPLY_FAILED_KEPT;
    p.table->setState(p.job, JOB_FAILED, r, strlen(r));
    w.key("job_state"); w.str("failed");
    w.key("reverted"); w.boolean(back);
  }
  h.jobs->persist();
  return ST_OK;
}

}  // namespace

Status opScopeGet(OpContext& c, ScopeHost& h) {
  char name[SCOPE_NAME_MAX + 1] = {0};
  uint8_t key[SCOPE_KEY_BYTES];
  const bool has = h.get(h.role_user, name, key);
  wipe(key, sizeof(key));
  JsonWriter& w = *c.out;
  w.key("name"); if (has && name[0]) w.str(name); else w.null();
  w.key("kind"); if (has && name[0]) w.str(name[0] == '$' ? "private" : "hashtag"); else w.null();
  return ST_OK;
}

bool scopeOf(OpContext& c, int name_tok, int key_tok, char name[SCOPE_NAME_MAX + 1], uint8_t key[SCOPE_KEY_BYTES]) {
  char buf[64];
  if (!jsonGetString(*c.doc, name_tok, buf, sizeof(buf), CS_ANY, 1)) { c.error = "bad-name"; return false; }
  const char* bare = buf[0] == '#' ? buf + 1 : buf;   // "#city" and "city" are one hashtag scope, as region names are
  if (!*bare) { c.error = "bad-name"; return false; }
  if (strlen(bare) > SCOPE_NAME_MAX) { c.error = "name-too-long"; return false; }
  for (const char* q = bare; *q; q++)
    if (!RegionMap::is_name_char((uint8_t)*q)) { c.error = "bad-char"; return false; }
  snprintf(name, SCOPE_NAME_MAX + 1, "%s", bare);
  memset(key, 0, SCOPE_KEY_BYTES);
  if (key_tok >= 0) {
    char hex[2 * SCOPE_KEY_BYTES + 1];
    const bool ok = jsonGetString(*c.doc, key_tok, hex, sizeof(hex), CS_NAME, 1) && hexKey(hex, key);
    wipe(hex, sizeof(hex));
    if (!ok) { wipe(key, SCOPE_KEY_BYTES); c.error = "bad-key"; return false; }
  } else if (name[0] == '$') {
    c.error = "key-required";   // a private scope's key does not follow from its name
    return false;
  } else {
    char tagged[SCOPE_NAME_MAX + 2];
    snprintf(tagged, sizeof(tagged), "#%s", name);
    TransportKeyStore store;
    TransportKey k;
    store.getAutoKeyFor(0, tagged, k);
    memcpy(key, k.key, SCOPE_KEY_BYTES);
    wipe(&k, sizeof(k));
  }
  return true;
}

Status opScopeSet(OpContext& c, ScopeHost& h) {
  char name[SCOPE_NAME_MAX + 1];
  uint8_t key[SCOPE_KEY_BYTES];
  if (!scopeOf(c, argOf(c, "name"), argOf(c, "key"), name, key)) return ST_INVALID;
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, "scope.default.set", p, &answered);
  if (st != ST_OK || answered) { wipe(key, sizeof(key)); return st; }
  const bool ok = h.set(h.role_user, name, key);
  wipe(key, sizeof(key));
  if (ok) {
    JsonWriter& w = *c.out;
    w.key("name"); w.str(name);
    w.key("kind"); w.str(name[0] == '$' ? "private" : "hashtag");
  }
  return finish(c, h, p, ok);
}

Status opScopeClear(OpContext& c, ScopeHost& h) {
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, "scope.default.clear", p, &answered);
  if (st != ST_OK || answered) return st;
  return finish(c, h, p, h.set(h.role_user, nullptr, nullptr));
}

bool scopeOwnsJob(const Job& j) { return strcmp(j.op, "scope.default.set") == 0 || strcmp(j.op, "scope.default.clear") == 0; }

bool scopeRevert(ScopeHost& h, const Job& j) {
  uint8_t img[SNAP_MAX];
  const size_t n = h.snap->load(img, sizeof(img));
  bool had = false;
  char name[SCOPE_NAME_MAX + 1];
  uint8_t key[SCOPE_KEY_BYTES];
  bool ok = n && parse(img, n, j.id, &had, name, key);
  wipe(img, sizeof(img));
  if (ok) ok = had && name[0] ? h.set(h.role_user, name, key) : h.set(h.role_user, nullptr, nullptr);
  wipe(key, sizeof(key));
  h.snap->discard();   // a terminal job never retries
  return ok;
}

void scopeConfirmed(ScopeHost& h) { h.snap->discard(); }

void scopeSnapBoot(ScopeHost& h) {
  JobTable* t = h.jobs ? h.jobs->peek() : nullptr;
  const Job* open = t ? t->openJob() : nullptr;
  bool keep = false;
  if (open && open->state == JOB_AWAITING && scopeOwnsJob(*open)) {
    uint8_t img[SNAP_MAX];
    const size_t n = h.snap->load(img, sizeof(img));
    bool had;
    char name[SCOPE_NAME_MAX + 1];
    uint8_t key[SCOPE_KEY_BYTES];
    keep = n && parse(img, n, open->id, &had, name, key);
    wipe(img, sizeof(img));
    wipe(key, sizeof(key));
  }
  if (!keep) h.snap->discard();   // a previous private key no job owns goes
}

}  // namespace Iface
