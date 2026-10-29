#include "JobService.h"
#include <new>
#include <stdlib.h>
#include <string.h>

namespace Iface {

static const char REVERT_FAILED[] = "{\"error\":\"revert-failed\"}";   // a revert the role could not do

void JobService::begin(JobStore* store, bool abnormal_boot, Hook revert, Hook confirm, void* user) {
  _store = store;
  _revert = revert;
  _confirm = confirm;
  _user = user;
  if (!_store) return;
  const size_t cap = JobTable::imageSize();
  uint8_t* buf = static_cast<uint8_t*>(malloc(cap));
  if (!buf) return;
  const size_t n = _store->load(buf, cap);
  if (n && table()) {
    if (!_t->deserialize(buf, n)) persist();   // a damaged table is forgotten (and replaced), never half trusted
    bool changed = false;
    _t->booted(abnormal_boot, [&](Job* j) {
      changed = true;
      if (!_revert || !_revert(_user, *j)) _t->setState(j, JOB_FAILED, REVERT_FAILED, sizeof(REVERT_FAILED) - 1);
    });
    if (changed) persist();
  }
  free(buf);
}

JobTable* JobService::table() {
  if (!_t) _t = new (std::nothrow) JobTable();
  return _t;
}

bool JobService::persist() {
  if (!_t || !_store) return _t != nullptr;   // RAM only when the role gave no store
  const size_t cap = JobTable::imageSize();
  uint8_t* buf = static_cast<uint8_t*>(malloc(cap));
  if (!buf) return false;
  const size_t n = _t->serialize(buf, cap);
  const bool ok = n && _store->save(buf, n);
  free(buf);
  return ok;
}

bool JobService::jobArg(OpContext& c, char* id, size_t cap) {
  const int t = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "job") : -1;
  return t >= 0 && jsonGetString(*c.doc, t, id, cap, CS_ID, 1);
}

void JobService::writeSummary(JsonWriter& w, const Job& j) {
  w.key("job"); w.str(j.id);
  w.key("op"); w.str(j.op);
  w.key("state"); w.str(jobStateName(j.state));
  w.key("owed"); w.boolean(jobTerminal(j.state) && !j.acked);
}

Status JobService::opGet(OpContext& c) {
  char id[Job::ID_CAP + 1];
  if (!jobArg(c, id, sizeof(id))) { c.error = "bad-args"; return ST_INVALID; }
  Job* j = nullptr;
  const JobTable::Lookup l = _t ? _t->find(id, &j) : JobTable::NONE;
  if (l == JobTable::EXPIRED) { c.error = "result-expired"; return ST_FAILED; }
  if (l != JobTable::FOUND) { c.error = "unknown-job"; return ST_INVALID; }
  JsonWriter& w = *c.out;
  writeSummary(w, *j);
  if (j->state == JOB_AWAITING) { w.key("confirm_left_s"); w.unum(j->confirm_left_s); }
  w.key("result");
  if (j->result_len) w.raw(j->result, j->result_len); else w.null();
  return ST_OK;
}

Status JobService::opList(OpContext& c) {
  JsonWriter& w = *c.out;
  w.key("jobs"); w.beginArray();
  if (_t)
    for (int i = 0; i < JobTable::SLOTS; i++) {
      const Job* j = _t->slot(i);
      if (!j) continue;
      w.beginObject();
      writeSummary(w, *j);
      w.endObject();
    }
  w.endArray();
  w.key("open"); w.unum(_t ? _t->openCount() : 0);
  w.key("owed"); w.unum(_t ? _t->owedCount() : 0);
  return ST_OK;
}

Status JobService::opAck(OpContext& c) {
  char id[Job::ID_CAP + 1];
  if (!jobArg(c, id, sizeof(id))) { c.error = "bad-args"; return ST_INVALID; }
  Job* j = nullptr;
  if (!_t || _t->find(id, &j) != JobTable::FOUND) { c.error = "unknown-job"; return ST_INVALID; }
  if (!jobTerminal(j->state)) { c.error = "not-terminal"; return ST_INVALID; }
  if (!j->acked) {
    _t->ack(id);
    if (!persist()) { c.error = "storage"; return ST_BUSY; }
  }
  c.out->key("acked"); c.out->boolean(true);
  return ST_OK;
}

// job.cancel: before anything is applied the job is cancelled; once applied and awaiting confirmation it is the revert
// rule (3.3). A terminal job cannot be cancelled.
Status JobService::opCancel(OpContext& c) {
  char id[Job::ID_CAP + 1];
  if (!jobArg(c, id, sizeof(id))) { c.error = "bad-args"; return ST_INVALID; }
  Job* j = nullptr;
  if (!_t || _t->find(id, &j) != JobTable::FOUND) { c.error = "unknown-job"; return ST_INVALID; }
  if (jobTerminal(j->state)) { c.error = "not-cancellable"; return ST_INVALID; }
  if (j->state == JOB_AWAITING) {
    if (!_revert || !_revert(_user, *j)) { c.error = "revert-failed"; return ST_FAILED; }
  } else {
    _t->setState(j, JOB_CANCELLED);
  }
  if (!persist()) { c.error = "storage"; return ST_BUSY; }
  writeSummary(*c.out, *j);
  return ST_OK;
}

// job.confirm: only an awaiting-confirm job, and only after its change took effect (the role's hook decides that).
// Reachability alone never confirms; this request is the confirmation.
Status JobService::opConfirm(OpContext& c) {
  char id[Job::ID_CAP + 1];
  if (!jobArg(c, id, sizeof(id))) { c.error = "bad-args"; return ST_INVALID; }
  Job* j = nullptr;
  if (!_t || _t->find(id, &j) != JobTable::FOUND) { c.error = "unknown-job"; return ST_INVALID; }
  if (j->state != JOB_AWAITING) { c.error = "not-awaiting"; return ST_INVALID; }
  if (!_confirm || !_confirm(_user, *j)) { c.error = "confirm-failed"; return ST_FAILED; }
  _t->setState(j, JOB_CONFIRMED);
  if (!persist()) { c.error = "storage"; return ST_BUSY; }
  writeSummary(*c.out, *j);
  return ST_OK;
}

void JobService::writeStatus(JsonWriter& w) {
  w.key("jobs"); w.beginObject();
  w.key("open"); w.unum(_t ? _t->openCount() : 0);
  w.key("owed"); w.unum(_t ? _t->owedCount() : 0);
  w.endObject();
}

void JobService::tick(uint32_t now_ms) {
  if (!_tick_init) { _tick_init = true; _tick_ms = now_ms; return; }
  const uint32_t secs = (now_ms - _tick_ms) / 1000;
  if (!secs) return;
  _tick_ms += secs * 1000;
  if (!_t) return;
  bool changed = false;
  _t->tick(secs, [&](Job* j) {
    changed = true;
    if (!_revert || !_revert(_user, *j)) _t->setState(j, JOB_FAILED, REVERT_FAILED, sizeof(REVERT_FAILED) - 1);
  });
  if (changed) persist();
}

}  // namespace Iface
