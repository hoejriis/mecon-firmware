#include "MgmtChannel.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "Wipe.h"

namespace Iface {

namespace {

constexpr uint32_t REC_MAGIC = 0x31524D4Du;    // "MMR1"
constexpr uint32_t SNAP_MAGIC = 0x31534D4Du;   // "MMS1"

void put32(uint8_t* o, size_t& at, uint32_t v) {
  for (int i = 0; i < 4; i++) o[at++] = (uint8_t)(v >> (8 * i));
}
uint32_t get32(const uint8_t* p, size_t at) {
  return (uint32_t)p[at] | ((uint32_t)p[at + 1] << 8) | ((uint32_t)p[at + 2] << 16) | ((uint32_t)p[at + 3] << 24);
}
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

MgmtHost* hostOf(OpContext& c) { return static_cast<MgmtHost*>(c.user); }
int argOf(OpContext& c, const char* key) { return c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, key) : -1; }

bool hexKey(const char* s, uint8_t out[CHANNEL_SECRET_BYTES]) {
  if (strlen(s) != 2 * CHANNEL_SECRET_BYTES) return false;
  for (unsigned i = 0; i < 2 * CHANNEL_SECRET_BYTES; i++) {
    const char ch = s[i];
    const int v = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
    if (v < 0) return false;
    if (i & 1) out[i / 2] |= (uint8_t)v; else out[i / 2] = (uint8_t)(v << 4);
  }
  return true;
}

// 0: no record; 1: read; -1: a record that cannot be read (it counts as stale, never as absent).
int loadRecord(const MgmtHost& h, MgmtRecord* rec) {
  uint8_t buf[MgmtRecord::IMAGE + 8];
  const size_t n = h.record->load(buf, sizeof(buf));
  if (!n) return 0;
  return rec->parse(buf, n) ? 1 : -1;
}

bool saveRecord(const MgmtHost& h, const MgmtRecord& rec) {
  uint8_t buf[MgmtRecord::IMAGE];
  return h.record->save(buf, rec.serialize(buf));
}

const char* stateName(MgmtState s) { return s == MGMT_BOUND ? "bound" : s == MGMT_STALE ? "stale" : "none"; }

void writeBinding(JsonWriter& w, const MgmtHost& h, MgmtState st, const MgmtRecord& rec) {
  w.key("state"); w.str(stateName(st));
  if (st == MGMT_NONE || rec.slot == MGMT_NO_SLOT) return;   // an unreadable record names no slot
  w.key("slot"); w.unum(rec.slot);
  w.key("generation"); w.unum(rec.generation);
  ChannelInfo ch;
  if (h.channels->get(h.channels->user, rec.slot, ch) && (ch.name[0] || ch.keyed)) {
    static const char H[] = "0123456789abcdef";
    const char hash[3] = {H[ch.hash >> 4], H[ch.hash & 15], 0};
    w.key("name"); w.str(ch.name);
    w.key("hash"); w.str(hash);
    w.key("key_bits"); w.unum((uint64_t)ch.key_bytes * 8);
  }
}

char g_error[16 + Job::ID_CAP];

// A change in progress: its job, when the caller asked to confirm.
struct Pending {
  Job* job = nullptr;
  JobTable* table = nullptr;
  uint32_t within = 300;
};

// Decides whether this change runs at once (the cable, no confirmation asked), becomes a job, or is answered now.
// The snapshot is written before the job is opened, and the job before anything is applied: a cut anywhere leaves
// either nothing applied or a job that reverts, and an orphan snapshot goes at the next boot. ST_OK with *answered
// false means: apply it.
Status beginChange(OpContext& c, MgmtHost& h, const char* op, int slot, uint8_t slot_change, const uint8_t* applied_fp,
                   Pending& p, bool* answered) {
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
  if (!h.jobs || !h.snap || !h.job_id || !h.record_caller || !h.read_slot) { c.error = "confirm-unsupported"; return ST_INVALID; }
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
  MgmtSnap* s = new (std::nothrow) MgmtSnap();
  uint8_t img[MgmtSnap::IMAGE_MAX];
  bool ok = s != nullptr;
  if (ok) {
    snprintf(s->job, sizeof(s->job), "%s", jid);
    s->slot = (uint8_t)slot;
    s->slot_change = slot_change;
    if (applied_fp) memcpy(s->applied_fp, applied_fp, MGMT_FP_BYTES);
    if (slot_change != MgmtSnap::SLOT_NONE && !h.read_slot(h.user, slot, s->name, s->key)) {
      s->clear();
      delete s;
      c.error = "unsupported-key";
      return ST_FAILED;
    }
    if (loadRecord(h, &s->rec) != 1) s->rec = MgmtRecord();   // an unreadable record comes back as no binding
  }
  const size_t n = ok ? s->serialize(img, sizeof(img)) : 0;
  ok = n && h.snap->save(img, n);
  wipe(img, sizeof(img));
  if (s) { s->clear(); delete s; }
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

// After the change: a job that applied awaits confirmation (in effect at once, nothing to reboot); one that did not is
// reverted now and ends failed. Without a job, a failure is the operation's own.
void finishChange(OpContext& c, MgmtHost& h, Pending& p, bool applied) {
  if (!p.job) return;
  JsonWriter& w = *c.out;
  w.key("job"); w.str(p.job->id);
  if (applied) {
    p.table->awaitConfirm(p.job, p.within, true);
    w.key("job_state"); w.str("awaiting-confirm");
    w.key("confirm_left_s"); w.unum(p.within);
  } else {
    const bool back = mgmtRevert(h, *p.job);
    const char* r = back ? APPLY_FAILED_REVERTED : APPLY_FAILED_KEPT;
    p.table->setState(p.job, JOB_FAILED, r, strlen(r));
    w.key("job_state"); w.str("failed");
    w.key("reverted"); w.boolean(back);
  }
  h.jobs->persist();
}

bool sameFp(const uint8_t* a, const uint8_t* b) { return a && b && memcmp(a, b, MGMT_FP_BYTES) == 0; }

}  // namespace

// ---- images

size_t MgmtRecord::serialize(uint8_t* out) const {
  size_t at = 0;
  put32(out, at, REC_MAGIC);
  out[at++] = bound ? 1 : 0;
  out[at++] = slot;
  memcpy(out + at, fp, MGMT_FP_BYTES); at += MGMT_FP_BYTES;
  put32(out, at, generation);
  put32(out, at, crc32(out, at));
  return at;
}

bool MgmtRecord::parse(const uint8_t* in, size_t n) {
  if (n != IMAGE || get32(in, 0) != REC_MAGIC || get32(in, n - 4) != crc32(in, n - 4) || in[4] > 1) return false;
  bound = in[4] == 1;
  slot = in[5];
  memcpy(fp, in + 6, MGMT_FP_BYTES);
  generation = get32(in, 6 + MGMT_FP_BYTES);
  return true;
}

size_t MgmtSnap::serialize(uint8_t* out, size_t cap) const {
  const size_t jl = strlen(job);
  const size_t n = FIXED + jl;
  if (!jl || jl > Job::ID_CAP || cap < n) return 0;
  size_t at = 0;
  put32(out, at, SNAP_MAGIC);
  out[at++] = (uint8_t)jl;
  memcpy(out + at, job, jl); at += jl;
  out[at++] = slot;
  out[at++] = slot_change;
  memcpy(out + at, applied_fp, MGMT_FP_BYTES); at += MGMT_FP_BYTES;
  memset(out + at, 0, 32);
  memcpy(out + at, name, strlen(name)); at += 32;
  memcpy(out + at, key, CHANNEL_SECRET_BYTES); at += CHANNEL_SECRET_BYTES;
  at += rec.serialize(out + at);
  put32(out, at, crc32(out, at));
  return at;
}

bool MgmtSnap::parse(const uint8_t* in, size_t n) {
  clear();
  if (n < FIXED + 1) return false;
  if (get32(in, 0) != SNAP_MAGIC || get32(in, n - 4) != crc32(in, n - 4)) return false;
  const size_t jl = in[4];
  if (!jl || jl > Job::ID_CAP || n != FIXED + jl) return false;
  size_t at = 5;
  memcpy(job, in + at, jl); job[jl] = 0; at += jl;
  slot = in[at++];
  if (in[at] > SLOT_CLEARED) { clear(); return false; }
  slot_change = in[at++];
  memcpy(applied_fp, in + at, MGMT_FP_BYTES); at += MGMT_FP_BYTES;
  memcpy(name, in + at, CHANNEL_NAME_MAX); name[CHANNEL_NAME_MAX] = 0; at += 32;
  memcpy(key, in + at, CHANNEL_SECRET_BYTES); at += CHANNEL_SECRET_BYTES;
  if (!rec.parse(in + at, MgmtRecord::IMAGE)) { clear(); return false; }
  return true;
}

void MgmtSnap::clear() {
  wipe(key, sizeof(key));
  wipe(name, sizeof(name));
  job[0] = 0;
  slot = 0;
  slot_change = SLOT_NONE;
  wipe(applied_fp, sizeof(applied_fp));
  rec = MgmtRecord();
}

// ---- state

MgmtState mgmtState(const MgmtHost& h, MgmtRecord* rec) {
  const int r = loadRecord(h, rec);
  if (r == 0) { *rec = MgmtRecord(); return MGMT_NONE; }
  if (r < 0) {   // unreadable: stale, and naming no slot, so nothing is guarded or erased on its account
    *rec = MgmtRecord();
    rec->bound = true;
    rec->slot = MGMT_NO_SLOT;
    return MGMT_STALE;
  }
  if (!rec->bound) return MGMT_NONE;
  uint8_t fp[MGMT_FP_BYTES];
  if (rec->slot >= h.channels->capacity || !h.slot_fp(h.user, rec->slot, fp) || !sameFp(fp, rec->fp)) return MGMT_STALE;
  return MGMT_BOUND;
}

namespace {

// "confirm" on a change that protects nothing is refused, as config.apply refuses it, rather than ignored.
bool confirmAsked(OpContext& c) { return argOf(c, "confirm") >= 0; }

// The arguments of a channel.put, checked before any job opens: the key, its fingerprint, and that it is neither the
// public key nor another slot's.
Status putArgs(OpContext& c, MgmtHost& h, int slot, uint8_t fp[MGMT_FP_BYTES]) {
  char name[CHANNEL_NAME_MAX + 1], hex[2 * CHANNEL_SECRET_BYTES + 1];
  uint8_t key[CHANNEL_SECRET_BYTES];
  if (!jsonGetString(*c.doc, argOf(c, "name"), name, sizeof(name), CS_ANY, 1)) return ST_INVALID;
  const bool parsed = jsonGetString(*c.doc, argOf(c, "secret"), hex, sizeof(hex), CS_NAME, 1) && hexKey(hex, key);
  wipe(hex, sizeof(hex));
  if (!parsed) { wipe(key, sizeof(key)); return ST_INVALID; }
  uint8_t any = 0;
  for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= key[i];
  Status st = any ? ST_OK : ST_INVALID;
  if (st == ST_OK) {
    h.key_fp(h.user, key, fp);
    const int other = h.channels->find(h.channels->user, key);
    if (sameFp(fp, h.public_fp)) { c.error = "public-channel"; st = ST_FAILED; }
    else if (other >= 0 && other != slot) { c.error = "duplicate-secret"; st = ST_FAILED; }
  }
  wipe(key, sizeof(key));
  return st;
}

}  // namespace

// ---- operations

Status opMgmtGet(OpContext& c) {
  MgmtHost& h = *hostOf(c);
  MgmtRecord rec;
  writeBinding(*c.out, h, mgmtState(h, &rec), rec);
  return ST_OK;
}

Status opMgmtBind(OpContext& c) {
  MgmtHost& h = *hostOf(c);
  int64_t v;
  if (!jsonGetInt(*c.doc, argOf(c, "slot"), 0, (int64_t)h.channels->capacity - 1, &v)) return ST_INVALID;
  const int slot = (int)v;
  uint8_t fp[MGMT_FP_BYTES];
  if (!h.slot_fp(h.user, slot, fp)) { c.error = "empty-slot"; return ST_FAILED; }
  if (sameFp(fp, h.public_fp)) { c.error = "public-channel"; return ST_FAILED; }   // everyone holds that key
  MgmtRecord rec;
  const MgmtState st = mgmtState(h, &rec);
  if (st == MGMT_BOUND && rec.slot == slot) {   // already so: nothing changes, so nothing to confirm
    if (confirmAsked(c)) { c.error = "confirm-not-needed"; return ST_INVALID; }
    writeBinding(*c.out, h, st, rec);
    return ST_OK;
  }
  Pending p;
  bool answered;
  const Status s = beginChange(c, h, "mgmt.channel.bind", slot, MgmtSnap::SLOT_NONE, nullptr, p, &answered);
  if (s != ST_OK || answered) return s;
  MgmtRecord next;
  next.bound = true;
  next.slot = (uint8_t)slot;
  memcpy(next.fp, fp, MGMT_FP_BYTES);
  next.generation = rec.generation + 1;
  const bool ok = saveRecord(h, next);
  if (ok) writeBinding(*c.out, h, mgmtState(h, &rec), rec);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "write-failed"; return ST_FAILED; }
  return ST_OK;
}

Status opMgmtRotate(OpContext& c) {
  MgmtHost& h = *hostOf(c);
  char hex[2 * CHANNEL_SECRET_BYTES + 1], name[CHANNEL_NAME_MAX + 1];
  uint8_t key[CHANNEL_SECRET_BYTES];
  const bool parsed = jsonGetString(*c.doc, argOf(c, "secret"), hex, sizeof(hex), CS_NAME, 1) && hexKey(hex, key);
  wipe(hex, sizeof(hex));
  if (!parsed) { wipe(key, sizeof(key)); return ST_INVALID; }
  const int nt = argOf(c, "name");
  name[0] = 0;
  if (nt >= 0 && !jsonGetString(*c.doc, nt, name, sizeof(name), CS_ANY, 1)) { wipe(key, sizeof(key)); return ST_INVALID; }
  uint8_t any = 0;
  for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= key[i];
  if (!any) return ST_INVALID;
  MgmtRecord rec;
  const MgmtState st = mgmtState(h, &rec);
  Status s = ST_OK;
  uint8_t fp[MGMT_FP_BYTES];
  h.key_fp(h.user, key, fp);
  if (st == MGMT_NONE) { c.error = "not-bound"; s = ST_FAILED; }
  else if (st == MGMT_STALE) { c.error = "stale"; s = ST_FAILED; }   // rebind or unbind first
  else if (sameFp(fp, h.public_fp)) { c.error = "public-channel"; s = ST_FAILED; }
  else {
    const int other = h.channels->find(h.channels->user, key);
    if (other >= 0 && other != rec.slot) { c.error = "duplicate-secret"; s = ST_FAILED; }
  }
  if (s != ST_OK) { wipe(key, sizeof(key)); return s; }
  ChannelInfo cur;
  if (!name[0] && h.channels->get(h.channels->user, rec.slot, cur)) snprintf(name, sizeof(name), "%s", cur.name);
  if (!name[0]) { wipe(key, sizeof(key)); return ST_INVALID; }   // the slot had no name and none was given
  Pending p;
  bool answered;
  s = beginChange(c, h, "mgmt.channel.rotate", rec.slot, MgmtSnap::SLOT_KEY, fp, p, &answered);
  if (s != ST_OK || answered) { wipe(key, sizeof(key)); return s; }
  bool ok = h.channels->set(h.channels->user, rec.slot, name, key);
  wipe(key, sizeof(key));
  if (ok) {
    MgmtRecord next = rec;
    memcpy(next.fp, fp, MGMT_FP_BYTES);
    next.generation = rec.generation + 1;
    ok = saveRecord(h, next);   // a key written with its record not saved reads stale until reverted or rebound
  }
  if (ok) writeBinding(*c.out, h, mgmtState(h, &rec), rec);
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "write-failed"; return ST_FAILED; }
  return ST_OK;
}

Status opMgmtUnbind(OpContext& c) {
  MgmtHost& h = *hostOf(c);
  bool remove = false;
  const int rt = argOf(c, "remove");
  if (rt >= 0 && !jsonGetBool(*c.doc, rt, &remove)) return ST_INVALID;
  MgmtRecord rec;
  const MgmtState st = mgmtState(h, &rec);
  JsonWriter& w = *c.out;
  if (st == MGMT_NONE) {
    if (confirmAsked(c)) { c.error = "confirm-not-needed"; return ST_INVALID; }
    w.key("state"); w.str("none");
    w.key("removed"); w.boolean(false);
    return ST_OK;
  }
  // Removing erases the slot, so only while it still holds the bound key: a stale slot may hold the app's own channel.
  if (remove && st != MGMT_BOUND) { c.error = "stale"; return ST_FAILED; }
  Pending p;
  bool answered;
  const Status s = beginChange(c, h, "mgmt.channel.unbind", rec.slot,
                               remove ? MgmtSnap::SLOT_CLEARED : MgmtSnap::SLOT_NONE, nullptr, p, &answered);
  if (s != ST_OK || answered) return s;
  h.record->discard();
  bool ok = true;
  if (remove) {
    static const uint8_t ZERO[CHANNEL_SECRET_BYTES] = {0};
    ok = h.channels->set(h.channels->user, rec.slot, "", ZERO);
  }
  if (ok) {
    w.key("state"); w.str("none");
    w.key("removed"); w.boolean(remove);
  }
  finishChange(c, h, p, ok);
  if (!ok && !p.job) { c.error = "write-failed"; return ST_FAILED; }
  return ST_OK;
}

Status mgmtGuardChannel(OpContext& c, MgmtHost& h, Status (*op)(OpContext&), const char* op_name) {
  int64_t v;
  MgmtRecord rec;
  const MgmtState st = mgmtState(h, &rec);
  const bool guarded = st != MGMT_NONE && rec.slot != MGMT_NO_SLOT &&
                       jsonGetInt(*c.doc, argOf(c, "slot"), 0, (int64_t)h.channels->capacity - 1, &v) && (int)v == rec.slot;
  if (!guarded) {
    if (confirmAsked(c)) { c.error = "confirm-not-needed"; return ST_INVALID; }   // not the bound slot: nothing to protect
    OpContext cc = c;
    cc.user = h.channels;
    const Status s = op(cc);
    c.error = cc.error;
    return s;
  }
  const bool removing = strcmp(op_name, "channel.remove") == 0;
  uint8_t fp[MGMT_FP_BYTES] = {0};
  if (!removing) {   // a put is checked whole before any job opens
    const Status a = putArgs(c, h, rec.slot, fp);
    if (a != ST_OK) return a;
  }
  Pending p;
  bool answered;
  Status s = beginChange(c, h, op_name, rec.slot, removing ? MgmtSnap::SLOT_CLEARED : MgmtSnap::SLOT_KEY,
                         removing ? nullptr : fp, p, &answered);
  if (s != ST_OK || answered) return s;
  OpContext cc = c;
  cc.user = h.channels;
  s = op(cc);
  c.error = cc.error;
  bool ok = s == ST_OK;
  if (ok) {
    if (removing) {
      h.record->discard();   // the bound slot is gone: so is the binding
    } else {
      MgmtRecord next = rec;
      next.bound = true;
      memcpy(next.fp, fp, MGMT_FP_BYTES);
      next.generation = rec.generation + 1;
      ok = saveRecord(h, next);
    }
  }
  if (p.job) {
    finishChange(c, h, p, ok);
    return s == ST_OK || ok ? ST_OK : s;   // a failed write inside a job is the job's result
  }
  if (s == ST_OK && !ok) { c.error = "write-failed"; return ST_FAILED; }
  return s;
}

// ---- the job hooks

bool mgmtOwnsJob(const Job& j) {
  static const char* const OPS[] = {"mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind", "channel.put",
                                    "channel.remove"};
  for (const char* o : OPS)
    if (strcmp(j.op, o) == 0) return true;
  return false;
}

// Puts the slot and the record back. The slot is written only while it shows what the change left there; if it still
// holds the old contents, the change never reached it (a job cut before it was applied) and nothing is written; if it
// holds anything else, the app changed it meanwhile, and the revert is refused (the job ends failed and the binding
// reads stale) rather than overwrite that. The snapshot goes on every outcome: a terminal job never retries.
bool mgmtRevert(const MgmtHost& h, const Job& j) {
  uint8_t img[MgmtSnap::IMAGE_MAX];
  MgmtSnap* s = new (std::nothrow) MgmtSnap();
  bool ok = s != nullptr;
  if (ok) {
    const size_t n = h.snap->load(img, sizeof(img));
    ok = n && s->parse(img, n) && strcmp(s->job, j.id) == 0;
  }
  if (ok && s->slot_change != MgmtSnap::SLOT_NONE) {
    uint8_t cur[MGMT_FP_BYTES], old_fp[MGMT_FP_BYTES];
    const bool keyed = h.slot_fp(h.user, s->slot, cur);
    uint8_t any = 0;
    for (unsigned i = 0; i < CHANNEL_SECRET_BYTES; i++) any |= s->key[i];
    if (any) h.key_fp(h.user, s->key, old_fp);
    const bool applied = s->slot_change == MgmtSnap::SLOT_CLEARED ? !keyed : keyed && sameFp(cur, s->applied_fp);
    const bool untouched = any ? keyed && sameFp(cur, old_fp) : !keyed;
    if (applied && !untouched) ok = h.channels->set(h.channels->user, s->slot, s->name, s->key);
    else if (!untouched) ok = false;   // someone else's change since: never overwritten
  }
  if (ok) {
    if (s->rec.bound) ok = saveRecord(h, s->rec);
    else h.record->discard();
  }
  wipe(img, sizeof(img));
  if (s) { s->clear(); delete s; }
  h.snap->discard();
  return ok;
}

void mgmtConfirmed(const MgmtHost& h) { h.snap->discard(); }

void mgmtSnapBoot(const MgmtHost& h) {
  JobTable* t = h.jobs ? h.jobs->table() : nullptr;
  const Job* open = t ? t->openJob() : nullptr;
  bool keep = false;
  if (open && open->state == JOB_AWAITING && mgmtOwnsJob(*open)) {
    uint8_t img[MgmtSnap::IMAGE_MAX];
    MgmtSnap* s = new (std::nothrow) MgmtSnap();
    const size_t n = h.snap->load(img, sizeof(img));
    keep = s && n && s->parse(img, n) && strcmp(s->job, open->id) == 0;
    wipe(img, sizeof(img));
    if (s) { s->clear(); delete s; }
  }
  if (!keep) h.snap->discard();   // a snapshot no awaiting job owns holds an old key
}

}  // namespace Iface
