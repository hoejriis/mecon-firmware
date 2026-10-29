#include "ConsoleIface.h"
#include "DevIdHello.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helpers/MeconVersion.h"
#include "helpers/MemStats.h"
#include "helpers/Posture.h"
#include "helpers/cli/CliPolicy.h"
#include "helpers/cli/CliRegistry.h"
#include "helpers/cli/CliCaller.h"
#include "Observe.h"
#include "Wipe.h"

namespace Iface {

namespace {

ConsoleIface* self(OpContext& c) { return static_cast<ConsoleIface*>(c.user); }

void hexOf(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply",
                                    "sub.start", "sub.stop", "deployment.get", "deployment.provision",
                                    "deployment.authority_update", "deployment.reset", "deployment.set_unlock",
                                    "deployment.unlock_challenge", "deployment.unlock", "job.get", "job.list",
                                    "job.ack", "job.cancel", "job.confirm"};

Status opHello(OpContext& c) {
  ConsoleIface* m = self(c);
  JsonWriter& w = *c.out;
  char key[65];
  hexOf(key, m->host().node_key, 32);
  w.key("interface"); w.str("1.0");
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("node_key"); w.str(key);
  if (m->host().identity) {
    const Status ds = writeDeviceIdSig(c, *m->host().identity, m->deviceIdHex());
    if (ds != ST_OK) return ds;
  }
  w.key("role"); w.str(m->host().role);
  w.key("hardware"); w.str(m->host().hardware);
  w.key("firmware"); w.str(mecon::versionFull());
  w.key("upstream"); w.str(MECON_UPSTREAM_BASE);
  w.key("epoch"); w.unum(m->epoch());
  // The channel nonce of the console link, only once a Deployment record exists (AUTHORITY.md section 7.1).
  const char* nonce = m->helloNonce();
  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }
  w.key("capabilities"); w.beginArray();
  for (const char* cap : CAPABILITIES) w.str(cap);
  writeStreamCapabilities(w, m->produced());
  w.endArray();
  return ST_OK;
}

// Shared by status.get and the health stream. A server has no contact list, so "contacts" is absent.
void writeHealth(JsonWriter& w, ConsoleIface* m) {
  w.key("uptime_s"); w.unum(millis() / 1000);
  const MemStats ms = memStats();
  w.key("heap");
  if (!ms.known) w.null();
  else {
    w.beginObject();
    w.key("free"); w.unum(ms.free);
    w.key("largest"); w.unum(ms.largest);
    w.key("min_free"); if (ms.min_free) w.unum(ms.min_free); else w.null();
    w.key("loop_stack_free"); if (ms.loop_stack_free) w.unum(ms.loop_stack_free); else w.null();
    if (ms.psram) { w.key("psram_free"); w.unum(ms.psram); }
    w.endObject();
  }
  const uint16_t mv = m->host().battery_mv(m->host().user);
  w.key("battery_mv"); if (mv) w.unum(mv); else w.null();
}

Status opStatus(OpContext& c) {
  ConsoleIface* m = self(c);
  JsonWriter& w = *c.out;
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("epoch"); w.unum(m->epoch());
  writeHealth(w, m);
  const bool hardened = Posture::isHardened(m->host().posture(m->host().user));
  w.key("posture"); w.str(hardened ? "hardened" : "normal");
  m->auth().writeStatus(w, hardened);
  m->jobs().writeStatus(w);
  return ST_OK;
}

Status opSubStart(OpContext& c) {
  int started;
  const uint32_t now = millis();
  Status st = subStart(c, &self(c)->subs(), self(c)->produced(), now, &started, ConsoleIface::streamAllow,
                      self(c));
  self(c)->scheduleStream(started, now);
  return st;
}

Status opSubStop(OpContext& c) { return subStop(c, &self(c)->subs(), self(c)->produced()); }

Status withConfig(OpContext& c, Status (*op)(OpContext&)) {
  ConfigHost host = self(c)->configHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opSchema(OpContext& c) { return withConfig(c, opConfigSchema); }
Status opGet(OpContext& c) { return withConfig(c, opConfigGet); }
Status opApply(OpContext& c) { return withConfig(c, opConfigApply); }
Status opDeployGet(OpContext& c) { return self(c)->auth().opGet(c); }
Status opDeployProvision(OpContext& c) { return self(c)->auth().opProvision(c); }
Status opDeployUpdate(OpContext& c) { return self(c)->auth().opUpdate(c); }
Status opDeployReset(OpContext& c) { return self(c)->auth().opReset(c); }
Status opDeploySetUnlock(OpContext& c) { return self(c)->auth().opSetUnlock(c); }
Status opDeployUnlockChallenge(OpContext& c) { return self(c)->auth().opUnlockChallenge(c); }
Status opDeployUnlock(OpContext& c) { return self(c)->auth().opUnlock(c); }
Status opJobGet(OpContext& c) { return self(c)->jobs().opGet(c); }
Status opJobList(OpContext& c) { return self(c)->jobs().opList(c); }
Status opJobAck(OpContext& c) { return self(c)->jobs().opAck(c); }
Status opJobCancel(OpContext& c) { return self(c)->jobs().opCancel(c); }
Status opJobConfirm(OpContext& c) { return self(c)->jobs().opConfirm(c); }

const OpEntry OPS[] = {
  {"hello", opHello, PRIV_READ, false},
  {"status.get", opStatus, PRIV_READ, false},
  {"config.schema", opSchema, PRIV_READ, false},
  {"config.get", opGet, PRIV_READ, false},
  {"config.apply", opApply, PRIV_WRITE, true},
  {"sub.start", opSubStart, PRIV_READ, false},
  {"sub.stop", opSubStop, PRIV_READ, false},
  {"deployment.get", opDeployGet, PRIV_READ, false},
  {"deployment.provision", opDeployProvision, PRIV_WRITE, true},
  {"deployment.authority_update", opDeployUpdate, PRIV_WRITE, true},
  {"deployment.reset", opDeployReset, PRIV_WRITE, true},
  {"deployment.set_unlock", opDeploySetUnlock, PRIV_WRITE, true},
  // The handshake is never cached: a stored "unlocked" must not answer a request after the unlock has lapsed.
  {"deployment.unlock_challenge", opDeployUnlockChallenge, PRIV_WRITE, false},
  {"deployment.unlock", opDeployUnlock, PRIV_WRITE, false},
  // As on the Companion: ack is of the read class, cancel and confirm write; the three keep their result.
  {"job.get", opJobGet, PRIV_READ, false},
  {"job.list", opJobList, PRIV_READ, false},
  {"job.ack", opJobAck, PRIV_READ, true},
  {"job.cancel", opJobCancel, PRIV_WRITE, true},
  {"job.confirm", opJobConfirm, PRIV_WRITE, true},
};

}  // namespace

void ConsoleIface::begin(const Host& host, const uint8_t device_id[16], uint32_t epoch, bool epoch_saved) {
  _host = host;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
  _ctx = Cli::Context(Cli::T_USB, Cli::A_CABLE, _host.posture(_host.user));
  if (_host.snap_store)
    _confirm = ConfirmHost{&_jobs, _host.snap_store, confirmCable, confirmLineClass, nullptr, confirmRecordCaller,
                           confirmRunAs, confirmJobId};
  _jobs.setClassCheck(jobClasses, this);
  _jobs.setOnChange(jobChanged, this);
  _jobs.begin(_host.job_store, _host.board && abnormalReset(*_host.board) == 1, jobRevert, jobConfirm, this);
  configSnapBoot(configHost());   // a snapshot no awaiting job owns goes
  if (_host.dep_store) {
    DeployAuth::Env env;
    env.store = _host.dep_store;
    env.node_key = _host.node_key;
    env.publish_caller = true;   // the front door, CommonCLI and the Ethernet console read it (Cli::serverCaller)
    env.now_s = _host.clock_s ? _host.clock_s : nullptr;
    env.user = _host.user;
    _auth.begin(env);
    _prov.begin(_host.identity, &_auth, epoch, epoch_saved);
    Cli::serverCaller().unlocked = consoleUnlocked;   // the console lines of the stock CLI see the unlock too
    Cli::serverCaller().unlocked_user = this;
  }
  observeSink().fn = onObserve;
  observeSink().user = this;
}

// Whether the console, as a caller, may use a stream of this class: the CLI privilege table, as for any console line.
bool ConsoleIface::streamAllow(void* user, uint8_t cls) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  Cli::Context c(Cli::T_USB, Cli::A_CABLE, s->_host.posture(s->_host.user));
  s->_auth.stampContext(c, &s->_unlock);   // enrolled and unlocked, as for every console decision
  return Cli::decide(c, cls).allow;
}

// One received packet, as a console line, within the stream's rate. Not while a request is being typed: a line never
// lands inside another, so that packet counts as dropped.
void ConsoleIface::onObserve(void* user, float snr, float rssi, const uint8_t* raw, int len) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (!s->_subs.s[SID_OBSERVATIONS].active || !s->_out) return;
  if (s->_collecting) { s->_obs.dropped++; return; }
  if (!s->_obs.take(millis())) return;
  static const size_t CAP = 1024;
  char* ev = static_cast<char*>(malloc(CAP));
  if (!ev) { s->_obs.dropped++; return; }
  int n = writeObservation(ev, CAP, s->_clock, s->_epoch, false, 0, snr, rssi, raw, len, s->_obs.dropped);
  if (n > 0) n = s->_prov.sign("observations", ev, (size_t)n, CAP);   // -1: not signable, so not sent
  if (n <= 0) s->_obs.dropped++;   // and counted, so the next event tells the manager how many went missing
  if (n > 0) {
    s->_obs.dropped = 0;
    s->printLine(*s->_out, ev, (size_t)n);
  }
  free(ev);
}

bool ConsoleIface::consoleUnlocked(void* self) {
  ConsoleIface* s = static_cast<ConsoleIface*>(self);
  return s->_auth.unlocked(&s->_unlock);
}

ConfigHost ConsoleIface::configHost() {
  return ConfigHost{cliRun, cliAllow, this, SERVER_KEYS, N_SERVER_KEYS, &_confirm};
}

// ---- commit-or-revert (OPERATIONS.md 3.4). The console is the cable: a signed request on it is a signed caller.

bool ConsoleIface::confirmCable(void* user) {
  return Cli::principalOf(static_cast<ConsoleIface*>(user)->_ctx) == Cli::P_CABLE;
}

uint8_t ConsoleIface::confirmLineClass(void*, const char* line) { return Cli::resolve(line, Cli::ROLE_REPEATER).cls; }

void ConsoleIface::confirmRecordCaller(void* user, Job& j) {
  const ConsoleIface* s = static_cast<ConsoleIface*>(user);
  j.who_transport = s->_ctx.transport;
  j.who_auth = s->_ctx.auth;
  j.who_posture = s->_ctx.posture;
  j.who_mgmt = 0;
  j.who_audit = 0;
}

// A revert line runs as the console did for the caller who applied it: the signed flag is set again for a signed
// caller, so the CLI decides and audits it as that caller.
bool ConsoleIface::confirmRunAs(void* user, const Job& j, const char* line, char* reply, size_t cap) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const bool was = Cli::serverCaller().signed_line;
  Cli::serverCaller().signed_line = j.who_auth == Cli::A_SIGNED;
  const bool known = s->_host.run(s->_host.user, line, reply, cap);
  Cli::serverCaller().signed_line = was;
  return known;
}

bool ConsoleIface::confirmJobId(void* user, const Request& req, char* out, size_t cap) {
  return JobService::identity(&static_cast<ConsoleIface*>(user)->_auth.answering(), req, out, cap);
}

bool ConsoleIface::jobRevert(void* user, Job& j) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (strcmp(j.op, "config.apply") != 0) return false;
  bool reboot = false;
  if (!configRevert(s->configHost(), j, &reboot)) return false;
  s->_jobs.table()->setState(&j, JOB_REVERTED);
  // The servers apply the radio only at boot, so a restored radio needs one more reboot, at boot too.
  if (reboot) s->_reboot_at = millis() + 1500;
  return true;
}

bool ConsoleIface::jobConfirm(void* user, Job& j) {
  if (strcmp(j.op, "config.apply") != 0) return false;
  configConfirmed(static_cast<ConsoleIface*>(user)->configHost());
  return true;
}

// The console is one link, and its caller may always read: each change of a job's state is a line on it, unless a
// request is being typed (then it is still in job.get and job.list).
void ConsoleIface::jobChanged(void* user, const Job& j) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (!s->_out || s->_collecting) return;
  char ev[512 + Job::RESULT_CAP];
  const uint32_t ts = s->_host.clock_s ? s->_host.clock_s(s->_host.user) : 0;
  int n = s->_jobs.writeEvent(ev, sizeof(ev), s->_epoch, ts >= 1704067200, ts, j);
  if (n > 0) n = s->_prov.sign("job", ev, (size_t)n, sizeof(ev));
  if (n > 0) s->printLine(*s->_out, ev, (size_t)n);
}

bool ConsoleIface::jobClasses(void* user, uint8_t cls) {
  const ConsoleIface* s = static_cast<ConsoleIface*>(user);
  for (uint8_t c = 0; c < Cli::C_COUNT; c++)
    if ((cls & (1u << c)) && !Cli::decide(s->_ctx, c).allow) return false;
  return true;
}

bool ConsoleIface::cliRun(void* user, const char* line, char* reply, size_t cap) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  return s->_host.run(s->_host.user, line, reply, cap);
}

bool ConsoleIface::cliAllow(void* user, const char* line) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const Cli::Resolved r = Cli::resolve(line, Cli::ROLE_REPEATER);
  return Cli::decide(s->_ctx, r.cls, r.flags).allow;
}

bool ConsoleIface::authorize(void* user, const OpEntry& op, const Request&) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  s->_deny = nullptr;
  switch (s->_auth.preAuthorize(op, &s->_deny)) {
    case DeployAuth::PRE_ALLOW: return true;
    case DeployAuth::PRE_DENY: return false;
    case DeployAuth::PRE_TABLE: break;
  }
  const uint8_t cls = op.priv == PRIV_READ ? Cli::C_READ : op.priv == PRIV_WRITE ? Cli::C_WRITE : Cli::C_POSTURE;
  return Cli::decide(s->_ctx, cls).allow;
}

const char* ConsoleIface::denyReason(void* user) { return static_cast<ConsoleIface*>(user)->_deny; }

// One request through the dispatcher as the caller ctx: the cable, or the signed principal on it. The posture is read
// again each time, so a posture change takes effect on the next line.
int ConsoleIface::run(const Cli::Context& ctx, const char* msg, size_t len, char* out, size_t cap, ResultCache* cache) {
  _ctx = ctx;
  _ctx.posture = _host.posture(_host.user);
  _auth.setLink(&_unlock);
  _auth.stampContext(_ctx, &_unlock);   // the cable's unlock reaches the table as a flag
  _auth.setContext(_ctx);
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
  d.setDenyReason(denyReason);
  d.setAnswering(&_auth.answering());
  return d.handle(msg, len, out, cap, cache);
}

int ConsoleIface::runSigned(void* user, const Cli::Context& ctx, const char* body, size_t len, char* out, size_t cap) {
  return static_cast<ConsoleIface*>(user)->run(ctx, body, len, out, cap, nullptr);   // job ids guard replay; no id cache
}

void ConsoleIface::startLine() {
  _collecting = true;
  _overflow = false;
  _len = 0;
  if (!_line) _line = static_cast<char*>(malloc(MAX_LINE + 1));
  if (!_line) _overflow = true;   // nothing to collect into: answered with busy at the end of the line
}

void ConsoleIface::feed(char c, Print& out) {
  if (!_collecting) return;
  if (c == '\r' || c == '\n') {
    answer(out);
    return;
  }
  if (_overflow) return;
  if (_len >= MAX_LINE) { _overflow = true; return; }
  _line[_len++] = c;
}

void ConsoleIface::printLine(Print& out, const char* msg, size_t len) {
  out.print(PREFIX);
  out.write(reinterpret_cast<const uint8_t*>(msg), len);
  out.print("\r\n");
}

// Whether the environment stream has anything to report: rechecked once a minute, and before the first request is
// answered (a request can arrive before the first loop pass).
void ConsoleIface::refreshEnvironment(uint32_t now) {
  if (!_host.sensors || (_env_checked && now - _env_at < 60000)) return;
  _env_ok = countEnvironment(*_host.sensors, *_host.board) > 0;
  _env_checked = true;
  _env_at = now;
}

void ConsoleIface::answer(Print& out) {
  _collecting = false;
  if (!_env_checked) refreshEnvironment(millis());
  out.print("\r\n");   // ends the console's echo of the prefix, as the stock console ends an echoed line
  static const size_t OUT_MAX = 1600;
  char* res = static_cast<char*>(malloc(OUT_MAX));
  bool was_enrolled = false;   // whether a record existed when the request began
  int n;
  if (!res) {
    char small[320];
    n = writeResult(small, sizeof(small), "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) n = _prov.signResult(small, (size_t)n, sizeof(small));
    if (n > 0) printLine(out, small, (size_t)n);
  } else {
    if (_overflow) {
      n = writeResult(res, OUT_MAX, "", ST_BUSY, nullptr, _line ? "line-too-long" : "no-memory");
    } else {
      if (!_cache) _cache = new (std::nothrow) ResultCache();
      _auth.answering().clear();
      if (!_cache) {
        n = writeResult(res, OUT_MAX, "", ST_BUSY, nullptr, "no-memory");
      } else {
        was_enrolled = _auth.enrolled();
        if (!_nonce[0]) DeployAuth::issueNonce(_nonce);   // issued when the link's first request arrives
        const Cli::Context cable(Cli::T_USB, Cli::A_CABLE, _host.posture(_host.user));
        if (DeployAuth::isWrapper(_line, _len))
          n = _auth.dispatchSigned(cable, _nonce, _line, _len, _cache, runSigned, this, res, OUT_MAX);
        else
          n = run(cable, _line, _len, res, OUT_MAX, _cache);
      }
    }
    if (n > 0) n = _prov.signResult(res, (size_t)n, OUT_MAX, nullptr, &_auth.answering(), was_enrolled);
    _auth.answering().clear();
    if (n > 0) printLine(out, res, (size_t)n);
    free(res);
  }
  if (_line) {
    wipe(_line, MAX_LINE + 1);   // a write may have carried a secret; a plain memset before free can be dropped
    free(_line);
    _line = nullptr;
  }
  _len = 0;
}

bool ConsoleIface::writeStream(int s, JsonWriter& w) {
  w.beginObject();
  switch (s) {
    case SID_HEALTH: writeHealth(w, this); break;
    case SID_RADIO: {
      RadioCounts counts;
      if (_host.radio_counts) _host.radio_counts(_host.user, &counts);
      writeRadio(w, *_host.dispatcher, *_host.radio, counts);
      break;
    }
    case SID_POWER: writePower(w, *_host.board); break;
    case SID_RUNTIME: writeRuntime(w, *_host.board, _gap); break;
    case SID_ENVIRONMENT:
      if (!_host.sensors || writeEnvironment(w, *_host.sensors, *_host.board) == 0) return false;
      break;
    case SID_INVENTORY:
      if (!_host.inventory) return false;
      _host.inventory(_host.user, w);
      break;
    default: return false;
  }
  w.endObject();
  return w.ok();
}

void ConsoleIface::loop(uint32_t now, Print& out) {
  _out = &out;
  _gap.tick(now);
  _jobs.tick(now);
  if (_reboot_at && (int32_t)(now - _reboot_at) >= 0 && _host.board) _host.board->reboot();
  refreshEnvironment(now);
  if (_collecting) return;   // an event never lands in the middle of a request being typed
  SubSet* sets[1] = { &_subs };
  const uint32_t due = _clock.due(sets, 1, now);
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[800];   // an event of up to 640 bytes, and its signature
    int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, false, 0, data);
    if (n > 0) n = _prov.sign(STREAMS[s].name, ev, (size_t)n, sizeof(ev));   // -1: not signable, so not sent
    if (n > 0) printLine(out, ev, (size_t)n);
  }
  if (_boot_pending) {   // once per epoch, right after the first runtime snapshot
    _boot_pending = false;
    if (_subs.s[SID_RUNTIME].active) {
      _boot_sent = true;
      char data[384];
      JsonWriter w(data, sizeof(data));
      w.beginObject();
      writeBoot(w, *_host.board, _host.role, _host.hardware);
      w.endObject();
      char ev[512];
      const int n = w.ok() ? writeEvent(ev, sizeof(ev), STREAMS[SID_RUNTIME].name, ++_clock.seq[SID_RUNTIME], _epoch, false, 0, data) : -1;
      if (n > 0) printLine(out, ev, (size_t)n);
    }
  }
  if (!_subs.s[SID_LOGS].active) {
    _logs.skip(Posture::audit());
  } else {
    Posture::AuditEntry a[Posture::AuditRing::CAPACITY];
    uint32_t dropped = 0;
    const int k = _logs.poll(now, Posture::audit(), a, &dropped);
    for (int j = 0; j < k; j++) {
      char ev[320];
      const int n = writeLog(ev, sizeof(ev), _clock, _epoch, false, 0, a[j], j == 0 ? dropped : 0);
      if (n > 0) printLine(out, ev, (size_t)n);
    }
  }
}

}  // namespace Iface
