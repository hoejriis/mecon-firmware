#include "RegionOps.h"
#include <new>
#include <stdio.h>
#include <string.h>

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
void put16(uint8_t* o, size_t& at, uint16_t v) { o[at++] = (uint8_t)v; o[at++] = (uint8_t)(v >> 8); }
uint16_t get16(const uint8_t* p, size_t at) { return (uint16_t)(p[at] | (p[at + 1] << 8)); }
void put32(uint8_t* o, size_t& at, uint32_t v) { for (int i = 0; i < 4; i++) o[at++] = (uint8_t)(v >> (8 * i)); }
uint32_t get32(const uint8_t* p, size_t at) {
  return (uint32_t)p[at] | ((uint32_t)p[at + 1] << 8) | ((uint32_t)p[at + 2] << 16) | ((uint32_t)p[at + 3] << 24);
}

// The whole map, as the job's snapshot: every region with its id, the wildcard's flags, home and default, bound to the job
// by its id and checked with a CRC.
const uint32_t SNAP_MAGIC = 0x31534752;   // "RGS1"
const size_t ENTRY = 2 + 2 + 1 + sizeof(((RegionEntry*)nullptr)->name);
const size_t SNAP_MAX = 4 + 1 + Job::ID_CAP + 1 + 2 + 2 + 1 + MAX_REGION_ENTRIES * ENTRY + 4;

size_t snapshot(const RegionMap& m, const char* job, uint8_t* out) {
  size_t at = 0;
  put32(out, at, SNAP_MAGIC);
  const size_t jl = strlen(job);
  out[at++] = (uint8_t)jl;
  memcpy(out + at, job, jl); at += jl;
  RegionMap& mm = const_cast<RegionMap&>(m);
  out[at++] = mm.getWildcard().flags;
  const RegionEntry* home = mm.getHomeRegion();
  const RegionEntry* def = mm.getDefaultRegion();
  put16(out, at, home ? home->id : 0);
  put16(out, at, def ? def->id : 0);
  const int n = m.getCount();
  out[at++] = (uint8_t)n;
  for (int i = 0; i < n; i++) {
    const RegionEntry* e = m.getByIdx(i);
    put16(out, at, e->id);
    put16(out, at, e->parent);
    out[at++] = e->flags;
    memcpy(out + at, e->name, sizeof(e->name)); at += sizeof(e->name);
  }
  put32(out, at, crc32(out, at));
  return at;
}

// Checks a snapshot and that it is this job's; with apply, puts it in the map.
bool restore(RegionMap& m, const uint8_t* in, size_t n, const char* job, bool apply) {
  if (n < 4 + 1 + 1 + 2 + 2 + 1 + 4 || get32(in, 0) != SNAP_MAGIC || get32(in, n - 4) != crc32(in, n - 4)) return false;
  size_t at = 4;
  const size_t jl = in[at++];
  if (jl > Job::ID_CAP || at + jl > n || strlen(job) != jl || memcmp(in + at, job, jl) != 0) return false;
  at += jl;
  if (at + 1 + 2 + 2 + 1 > n) return false;
  const uint8_t wild = in[at++];
  const uint16_t home = get16(in, at); at += 2;
  const uint16_t def = get16(in, at); at += 2;
  const int count = in[at++];
  if (count > MAX_REGION_ENTRIES || at + (size_t)count * ENTRY + 4 != n) return false;
  if (!apply) return true;
  m.clear();
  for (int i = 0; i < count; i++) {
    const uint16_t id = get16(in, at), parent = get16(in, at + 2);
    const uint8_t flags = in[at + 4];
    char name[sizeof(((RegionEntry*)nullptr)->name)];
    memcpy(name, in + at + 5, sizeof(name));
    name[sizeof(name) - 1] = 0;
    at += ENTRY;
    RegionEntry* e = m.putRegion(name, parent, id);   // an explicit id: the table's next id stays above it
    if (!e) return false;
    e->flags = flags;
  }
  m.getWildcard().flags = wild;
  m.setHomeRegion(home ? m.findById(home) : nullptr);
  m.setDefaultRegion(def ? m.findById(def) : nullptr);
  return true;
}

// A region's name as asked for: the native characters, at most 30 bytes, no truncation. A leading '#' is the same region as
// the name without it, as the console treats it.
bool nameArg(OpContext& c, const char* key, char* out, size_t cap, bool allow_wildcard) {
  char buf[64];
  if (!jsonGetString(*c.doc, argOf(c, key), buf, sizeof(buf), CS_ANY, 1)) { c.error = "bad-name"; return false; }
  if (allow_wildcard && strcmp(buf, "*") == 0) { snprintf(out, cap, "*"); return true; }
  if (strlen(buf) > REGION_NAME_MAX) { c.error = "name-too-long"; return false; }
  for (const char* p = buf; *p; p++)
    if (!RegionMap::is_name_char((uint8_t)*p)) { c.error = "bad-char"; return false; }
  snprintf(out, cap, "%s", buf);
  return true;
}

RegionEntry* exact(RegionHost& h, const char* name) {
  if (strcmp(name, "*") == 0) return &h.map->getWildcard();
  return h.map->findByName(name);
}

const char* parentName(RegionHost& h, const RegionEntry& e) {
  if (e.parent == 0) return "*";
  const RegionEntry* p = h.map->findById(e.parent);
  return p ? p->name : "*";
}

void writeEntry(JsonWriter& w, RegionHost& h, const RegionEntry& e) {
  w.key("name"); w.str(e.name);
  w.key("parent"); w.str(parentName(h, e));
  w.key("flood"); w.str((e.flags & REGION_DENY_FLOOD) ? "deny" : "allow");
  w.key("kind"); w.str(e.name[0] == '$' ? "private" : "hashtag");
}

// Whether moving region `id` under `parent` would close a loop: the parent's chain reaches the region itself.
bool makesCycle(RegionHost& h, uint16_t id, uint16_t parent) {
  for (int n = 0; parent != 0 && n <= MAX_REGION_ENTRIES; n++) {
    if (parent == id) return true;
    const RegionEntry* p = h.map->findById(parent);
    if (!p) return false;
    parent = p->parent;
  }
  return parent == id;
}

char g_error[16 + Job::ID_CAP];

struct Pending {
  Job* job = nullptr;
  JobTable* table = nullptr;
  uint32_t within = 300;
};

// As the management channel's: the copy is written before the job is opened, and the job before anything changes, so a
// cut anywhere leaves either nothing changed or a job that reverts, and an orphan copy goes at the next boot. ST_OK with
// *answered false means: apply it.
Status beginChange(OpContext& c, RegionHost& h, const char* op, Pending& p, bool* answered) {
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
  if (l == JobTable::FOUND) { JobService::writeSummary(*c.out, *known); *answered = true; return ST_OK; }   // never twice
  if (l == JobTable::EXPIRED) { c.error = "result-expired"; return ST_FAILED; }
  if (const Job* open = p.table->openJob()) {
    snprintf(g_error, sizeof(g_error), "job-open:%s", open->id);
    c.error = g_error;
    return ST_BUSY;
  }
  uint8_t* img = new (std::nothrow) uint8_t[SNAP_MAX];
  bool ok = img != nullptr;
  if (ok) ok = h.snap->save(img, snapshot(*h.map, jid, img));
  delete[] img;
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

void finishChange(OpContext& c, RegionHost& h, Pending& p, bool applied) {
  if (!p.job) return;
  JsonWriter& w = *c.out;
  w.key("job"); w.str(p.job->id);
  if (applied) {
    p.table->awaitConfirm(p.job, p.within, true);
    w.key("job_state"); w.str("awaiting-confirm");
    w.key("confirm_left_s"); w.unum(p.within);
  } else {
    const bool back = regionRevert(h, *p.job);
    const char* r = back ? APPLY_FAILED_REVERTED : APPLY_FAILED_KEPT;
    p.table->setState(p.job, JOB_FAILED, r, strlen(r));
    w.key("job_state"); w.str("failed");
    w.key("reverted"); w.boolean(back);
  }
  h.jobs->persist();
}

// Saves the map and tells the role when the default changed. A failed save is the change's failure: the map in RAM would
// otherwise differ from the file, and a reboot would bring the old one back without saying so.
bool commit(RegionHost& h, bool default_changed) {
  if (default_changed && h.default_changed) h.default_changed(h.role_user, h.map->getDefaultRegion());
  return h.save(h.role_user);
}

}  // namespace

Status opRegionList(OpContext& c, RegionHost& h) {
  int64_t cursor = 0;
  const int ct = argOf(c, "cursor");
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, MAX_REGION_ENTRIES, &cursor)) return ST_INVALID;
  JsonWriter& w = *c.out;
  RegionMap& m = *h.map;
  if (cursor == 0) {
    w.key("capacity"); w.unum(MAX_REGION_ENTRIES);
    w.key("wildcard"); w.beginObject();
    w.key("flood"); w.str((m.getWildcard().flags & REGION_DENY_FLOOD) ? "deny" : "allow");
    w.endObject();
    const RegionEntry* home = m.getHomeRegion();
    const RegionEntry* def = m.getDefaultRegion();
    w.key("home"); if (home && !home->isWildcard()) w.str(home->name); else w.null();
    w.key("default"); if (def) w.str(def->name); else w.null();
  }
  w.key("regions"); w.beginArray();
  int i = (int)cursor;
  // At most REGION_PAGE a page, and fewer when the next would not leave room for the rest of the answer and its
  // signature, with a margin: over LoRa a whole reply is 1,024 bytes, and the manager's Companion then carries it
  // JSON-escaped inside an rf event of at most 1,536 bytes, so a LoRa page stays near 600 bytes.
  const size_t ENTRY_MAX = 110, AFTER = 400;
  for (unsigned n = 0; i < m.getCount() && n < REGION_PAGE && (n == 0 || w.size() + ENTRY_MAX + AFTER <= w.cap()); i++, n++) {
    w.beginObject(); writeEntry(w, h, *m.getByIdx(i)); w.endObject();
  }
  w.endArray();
  w.key("next"); if (i < m.getCount()) w.num(i); else w.null();
  return ST_OK;
}

Status opRegionPut(OpContext& c, RegionHost& h) {
  char name[REGION_NAME_MAX + 1], parent_name[REGION_NAME_MAX + 1] = "*", flood[8] = "allow";
  if (!nameArg(c, "name", name, sizeof(name), false)) return ST_INVALID;
  if (argOf(c, "parent") >= 0 && !nameArg(c, "parent", parent_name, sizeof(parent_name), true)) return ST_INVALID;
  if (argOf(c, "flood") >= 0 && !jsonGetString(*c.doc, argOf(c, "flood"), flood, sizeof(flood), CS_NAME, 1)) return ST_INVALID;
  if (strcmp(flood, "allow") != 0 && strcmp(flood, "deny") != 0) return ST_INVALID;
  const RegionEntry* parent = exact(h, parent_name);
  if (!parent) { c.error = "not-found"; return ST_FAILED; }
  RegionEntry* existing = h.map->findByName(name);
  if (existing && (existing->id == parent->id || makesCycle(h, existing->id, parent->id))) {
    c.error = "cycle";
    return ST_INVALID;
  }
  if (!existing && h.map->getCount() >= MAX_REGION_ENTRIES) { c.error = "full"; return ST_BUSY; }
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, "region.put", p, &answered);
  if (st != ST_OK || answered) return st;
  RegionEntry* e = h.map->putRegion(name, parent->id);
  bool ok = e != nullptr;
  if (ok) {
    e->flags = strcmp(flood, "deny") == 0 ? REGION_DENY_FLOOD : 0;
    ok = commit(h, false);
  }
  if (ok && !p.job) { writeEntry(*c.out, h, *e); return ST_OK; }
  if (ok) writeEntry(*c.out, h, *e);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "storage"; return ST_FAILED; }
  return ST_OK;
}

Status opRegionRemove(OpContext& c, RegionHost& h) {
  char name[REGION_NAME_MAX + 1];
  if (!nameArg(c, "name", name, sizeof(name), false)) return ST_INVALID;
  RegionEntry* e = h.map->findByName(name);
  if (!e) { c.error = "not-found"; return ST_FAILED; }
  for (int i = 0; i < h.map->getCount(); i++)
    if (h.map->getByIdx(i)->parent == e->id) { c.error = "not-empty"; return ST_BUSY; }
  const RegionEntry* home = h.map->getHomeRegion();
  const RegionEntry* def = h.map->getDefaultRegion();
  if ((home && home->id == e->id) || (def && def->id == e->id)) { c.error = "referenced"; return ST_BUSY; }
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, "region.remove", p, &answered);
  if (st != ST_OK || answered) return st;
  const bool ok = h.map->removeRegion(*e) && commit(h, false);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "storage"; return ST_FAILED; }
  return ST_OK;
}

Status opRegionFlood(OpContext& c, RegionHost& h) {
  char name[REGION_NAME_MAX + 1], flood[8];
  if (!nameArg(c, "name", name, sizeof(name), true)) return ST_INVALID;
  if (!jsonGetString(*c.doc, argOf(c, "flood"), flood, sizeof(flood), CS_NAME, 1)) return ST_INVALID;
  if (strcmp(flood, "allow") != 0 && strcmp(flood, "deny") != 0) return ST_INVALID;
  RegionEntry* e = exact(h, name);
  if (!e) { c.error = "not-found"; return ST_FAILED; }
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, "region.flood", p, &answered);
  if (st != ST_OK || answered) return st;
  if (strcmp(flood, "deny") == 0) e->flags |= REGION_DENY_FLOOD; else e->flags &= ~REGION_DENY_FLOOD;
  const bool ok = commit(h, false);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "storage"; return ST_FAILED; }
  return ST_OK;
}

// region.home and region.default share their shape: a name, or null for none.
static Status setRef(OpContext& c, RegionHost& h, const char* op, bool is_default) {
  const int t = argOf(c, "name");
  if (t < 0) return ST_INVALID;
  RegionEntry* e = nullptr;
  if (!jsonIsNull(*c.doc, t)) {
    char name[REGION_NAME_MAX + 1];
    if (!nameArg(c, "name", name, sizeof(name), false)) return ST_INVALID;
    e = h.map->findByName(name);
    if (!e) { c.error = "not-found"; return ST_FAILED; }
  }
  Pending p;
  bool answered;
  const Status st = beginChange(c, h, op, p, &answered);
  if (st != ST_OK || answered) return st;
  if (is_default) {
    if (e) e->flags = 0;   // the native `region default` makes its region allow floods: its scope must be forwarded
    h.map->setDefaultRegion(e);
  } else {
    h.map->setHomeRegion(e);
  }
  const bool ok = commit(h, is_default);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "storage"; return ST_FAILED; }
  return ST_OK;
}

Status opRegionHome(OpContext& c, RegionHost& h) { return setRef(c, h, "region.home", false); }
Status opRegionDefault(OpContext& c, RegionHost& h) { return setRef(c, h, "region.default", true); }

bool regionOwnsJob(const Job& j) {
  static const char* const OPS[] = {"region.put", "region.remove", "region.flood", "region.home", "region.default"};
  for (const char* o : OPS)
    if (strcmp(j.op, o) == 0) return true;
  return false;
}

bool regionRevert(RegionHost& h, const Job& j) {
  uint8_t* img = new (std::nothrow) uint8_t[SNAP_MAX];
  bool ok = img != nullptr;
  size_t n = 0;
  if (ok) {
    n = h.snap->load(img, SNAP_MAX);
    ok = n && restore(*h.map, img, n, j.id, false) && restore(*h.map, img, n, j.id, true);
  }
  delete[] img;
  if (ok) ok = commit(h, true);
  h.snap->discard();   // a terminal job never retries
  return ok;
}

void regionConfirmed(RegionHost& h) { h.snap->discard(); }

void regionSnapBoot(RegionHost& h) {
  JobTable* t = h.jobs ? h.jobs->peek() : nullptr;   // at boot: no table, no job to keep
  const Job* open = t ? t->openJob() : nullptr;
  bool keep = false;
  if (open && open->state == JOB_AWAITING && regionOwnsJob(*open)) {
    uint8_t* img = new (std::nothrow) uint8_t[SNAP_MAX];
    if (img) {
      const size_t n = h.snap->load(img, SNAP_MAX);
      keep = n && restore(*h.map, img, n, open->id, false);
      delete[] img;
    }
  }
  if (!keep) h.snap->discard();
}

}  // namespace Iface
