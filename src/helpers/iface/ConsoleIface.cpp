#include "ConsoleIface.h"
#include <helpers/Uptime.h>
#include "DevIdHello.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helpers/MeconVersion.h"
#include "helpers/MemStats.h"
#include "helpers/Posture.h"
#include "helpers/SensorManager.h"
#include "helpers/cli/CliPolicy.h"
#include "helpers/cli/MeconCli.h"
#include "helpers/cli/CliRegistry.h"
#include "helpers/cli/CliCaller.h"
#include "helpers/NrfWorker.h"
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
                                    "deployment.unlock_challenge", "deployment.unlock", "identity.rotate", "identity.rotation",
                                    "channel.list", "channel.get", "channel.put", "channel.remove", "mgmt.channel.get",
                                    "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind", "job.get", "job.list",
                                    "job.ack", "job.cancel", "job.confirm", "update.manifest", "update.status",
                                    "update.offer", "update.chunk"};

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
  m->updates().writeAttestation(w);   // the image of an update job, while it awaits confirmation (UPDATES.md section 7)
  // The channel nonce of the console link, only once a Deployment record exists (AUTHORITY.md section 7.1).
  const char* nonce = m->helloNonce();
  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }
  w.key("capabilities"); w.beginArray();
  for (const char* cap : CAPABILITIES) if (!m->onLora() || loraOpAllowed(cap)) w.str(cap);
  if (!m->onLora()) writeStreamCapabilities(w, m->produced());   // no stream goes over LoRa
  w.endArray();
  return ST_OK;
}

// Shared by status.get and the health stream. A server has no contact list, so "contacts" is absent.
void writeHealth(JsonWriter& w, ConsoleIface* m) {
  w.key("uptime_s"); w.unum(Uptime::secs(millis()));
  const MemStats ms = memStats();
  w.key("heap");
  if (!ms.known) w.null();
  else {
    w.beginObject();
    w.key("free"); w.unum(ms.free);
    w.key("largest"); w.unum(ms.largest);
    w.key("min_free"); if (ms.min_free) w.unum(ms.min_free); else w.null();
    w.key("loop_stack_free"); if (ms.loop_stack_free) w.unum(ms.loop_stack_free); else w.null();
    if (const uint32_t ws = NrfWorker::workerStackFree()) { w.key("worker_stack_free"); w.unum(ws); }   // nRF52 (#158)
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
  m->updates().writeAttestation(w);
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
// The stored set is an open read; the rotation statement in it names device_id, so only a caller the table lets read gets it.
Status opDeployGet(OpContext& c) {
  const Status st = self(c)->auth().opGet(c);
  if (st == ST_OK && self(c)->mayRead()) self(c)->writeRotation(*c.out);
  return st;
}
Status opIdentityRotate(OpContext& c) { return self(c)->rotateIdentity(c); }
Status withChannels(OpContext& c, Status (*op)(OpContext&)) {
  ChannelHost host = self(c)->channelHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status withMgmt(OpContext& c, Status (*op)(OpContext&)) {
  MgmtHost host = self(c)->mgmtHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opChanList(OpContext& c) { return withChannels(c, opChannelList); }
Status opChanGet(OpContext& c) { return withChannels(c, opChannelGet); }
Status opChanPut(OpContext& c) {
  MgmtHost h = self(c)->mgmtHost();
  return mgmtGuardChannel(c, h, opChannelPut, "channel.put");
}
Status opChanRemove(OpContext& c) {
  MgmtHost h = self(c)->mgmtHost();
  return mgmtGuardChannel(c, h, opChannelRemove, "channel.remove");
}
Status opMgmtGetW(OpContext& c) { return withMgmt(c, opMgmtGet); }
Status opMgmtBindW(OpContext& c) { return withMgmt(c, opMgmtBind); }
Status opMgmtRotateW(OpContext& c) { return withMgmt(c, opMgmtRotate); }
Status opMgmtUnbindW(OpContext& c) { return withMgmt(c, opMgmtUnbind); }
Status opIdentityRotation(OpContext& c) { self(c)->writeRotation(*c.out); return ST_OK; }
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
Status opUpdManifest(OpContext& c) { return self(c)->updates().opManifest(c, millis()); }
Status opUpdStatus(OpContext& c) { return self(c)->updates().opStatus(c); }
Status opUpdOffer(OpContext& c) { return self(c)->updates().opOffer(c, millis()); }
Status opUpdChunk(OpContext& c) { return self(c)->updates().opChunk(c, millis()); }

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
  {"identity.rotate", opIdentityRotate, PRIV_WRITE, false},   // never answered from the cache: it must run once
  {"identity.rotation", opIdentityRotation, PRIV_READ, false},
  // One channel slot, for the management channel (MANAGEMENT_CHANNEL.md section 6); bind, rotate and unbind confirm-or-revert
  // for anyone but the cable.
  {"channel.list", opChanList, PRIV_READ, false},
  {"channel.get", opChanGet, PRIV_READ, false},
  {"channel.put", opChanPut, PRIV_WRITE, true},
  {"channel.remove", opChanRemove, PRIV_WRITE, true},
  {"mgmt.channel.get", opMgmtGetW, PRIV_READ, false},
  {"mgmt.channel.bind", opMgmtBindW, PRIV_WRITE, true},
  {"mgmt.channel.rotate", opMgmtRotateW, PRIV_WRITE, true},
  {"mgmt.channel.unbind", opMgmtUnbindW, PRIV_WRITE, true},
  // As on the Companion: ack is of the read class, cancel and confirm write; the three keep their result.
  {"job.get", opJobGet, PRIV_READ, false},
  {"job.list", opJobList, PRIV_READ, false},
  {"job.ack", opJobAck, PRIV_READ, true},
  {"job.cancel", opJobCancel, PRIV_WRITE, true},
  {"job.confirm", opJobConfirm, PRIV_WRITE, true},
  // Staging a manifest changes nothing persistent; it is for any caller with read (UPDATES.md section 8).
  {"update.manifest", opUpdManifest, PRIV_READ, false},
  {"update.status", opUpdStatus, PRIV_READ, false},
  // The offer is of the update class and keeps its result: a retry gets the same job and transfer token back. A chunk
  // is authorised by that token (DeployAuth::preAuthorize lets it through to the handler) and is not kept: its answer
  // is the offset expected next, which a retry asks again.
  {"update.offer", opUpdOffer, PRIV_UPDATE, true},
  {"update.chunk", opUpdChunk, PRIV_READ, false},
};

}  // namespace

void ConsoleIface::begin(const Host& host, const uint8_t device_id[16], uint32_t epoch, bool epoch_saved) {
  _host = host;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
  _ctx = Cli::Context(Cli::T_USB, Cli::A_CABLE, _host.posture(_host.user));
  _updates.begin(_host.hardware, _host.role, _host.board ? _host.board->getManufacturerNames() : nullptr);
  if (_host.snap_store)
    _confirm = ConfirmHost{&_jobs, _host.snap_store, confirmCable, confirmLineClass,
                           _host.read_secret ? confirmReadSecret : nullptr, confirmRecordCaller,
                           confirmRunAs, confirmJobId};
  _mgmt_slot.setStore(_host.mgmt_slot);
  mesh::Utils::sha256(_public_fp, MGMT_FP_BYTES, MGMT_PUBLIC_KEY, sizeof(MGMT_PUBLIC_KEY));
  _jobs.setClassCheck(jobClasses, this);
  _jobs.setOnChange(jobChanged, this);
  _updates.setHost(Upd::UpdateService::Host{&_jobs, this, confirmJobId, confirmRecordCaller, updSigned, updReboot});
  _jobs.begin(_host.job_store, _host.board && abnormalReset(*_host.board) == 1, jobRevert, jobConfirm, this);
  _updates.bootReconcile();   // an installed image attests, or the bootloader rolled it back
  configSnapBoot(configHost());   // a snapshot no awaiting job owns goes
  mgmtSnapBoot(mgmtHost());       // likewise the management channel's: it holds an old key
  if (_host.dep_store) {
    DeployAuth::Env env;
    env.store = _host.dep_store;
    env.node_key = _host.node_key;
    env.publish_caller = true;   // the front door, CommonCLI and the Ethernet console read it (Cli::serverCaller)
    env.now_s = _host.clock_s ? authClock : nullptr;
    env.profile_exists = _host.profile_exists ? authProfileExists : nullptr;
    env.user = this;
    _auth.begin(env);
    _prov.begin(_host.identity, &_auth, epoch, epoch_saved);
    Cli::serverCaller().unlocked = consoleUnlocked;   // the console lines of the stock CLI see the unlock too
    Cli::serverCaller().unlocked_user = this;
  }
  observeSink().fn = onObserve;
  observeSink().user = this;
}

uint32_t ConsoleIface::authClock(void* user) {
  const ConsoleIface* s = static_cast<ConsoleIface*>(user);
  return s->_host.clock_s(s->_host.user);
}

bool ConsoleIface::authProfileExists(void* user, uint8_t index) {
  const ConsoleIface* s = static_cast<ConsoleIface*>(user);
  return s->_host.profile_exists(s->_host.user, index);
}

bool ConsoleIface::confirmReadSecret(void* user, const KeyDef& k, unsigned idx, char* out, size_t cap) {
  const ConsoleIface* s = static_cast<ConsoleIface*>(user);
  return s->_host.read_secret(s->_host.user, k, idx, out, cap);
}

MgmtHost ConsoleIface::mgmtHost() {
  _chan_host = channelHost();
  return MgmtHost{&_chan_host, _host.mgmt_record, _host.mgmt_snap, &_jobs, mgmtSlotFp, mgmtKeyFp, _public_fp, mgmtReadSlot,
                  confirmCable, confirmRecordCaller, confirmJobId, (uint8_t)(1u << Cli::C_WRITE), this};
}

bool ConsoleIface::mgmtSlotFp(void* user, int slot, uint8_t out[MGMT_FP_BYTES]) {
  char name[CHANNEL_NAME_MAX + 1];
  uint8_t key[CHANNEL_SECRET_BYTES];
  const bool ok = slot == 0 && static_cast<ConsoleIface*>(user)->_mgmt_slot.read(name, key);
  if (ok) mesh::Utils::sha256(out, MGMT_FP_BYTES, key, CHANNEL_SECRET_BYTES);
  wipe(key, sizeof(key));
  return ok;
}

void ConsoleIface::mgmtKeyFp(void*, const uint8_t key[CHANNEL_SECRET_BYTES], uint8_t out[MGMT_FP_BYTES]) {
  mesh::Utils::sha256(out, MGMT_FP_BYTES, key, CHANNEL_SECRET_BYTES);
}

bool ConsoleIface::mgmtReadSlot(void* user, int slot, char name[CHANNEL_NAME_MAX + 1], uint8_t key[CHANNEL_SECRET_BYTES]) {
  return slot == 0 && static_cast<ConsoleIface*>(user)->_mgmt_slot.read(name, key);
}

bool ConsoleIface::mayRead() { return Cli::decide(_ctx, Cli::C_READ).allow; }

// The statement the old key signed before the swap (AUTHORITY.md section 11), as a member of the current object.
bool ConsoleIface::writeRotation(JsonWriter& w) {
  char text[256], sig[89];
  if (!_host.read_rotation || !_host.read_rotation(_host.user, text, sizeof(text), sig, sizeof(sig))) return false;
  w.key("rotation");
  w.beginObject();
  w.key("statement"); w.str(text);
  w.key("sig"); w.str(sig);
  w.endObject();
  return true;
}

// identity.rotate. The result is signed with the key being retired (compute() does it from _old_id), so the manager that
// enrolled that key can verify the answer and learn the new one from it.
Status ConsoleIface::rotateIdentity(OpContext& c) {
  Status st = _auth.rotateGate(c);
  if (st != ST_OK) return st;
  if (!_host.rotate || !_host.identity) { c.error = "unsupported"; return ST_FAILED; }
  char oldhex[65], newhex[65];
  hexOf(oldhex, _host.node_key, 32);
  mesh::LocalIdentity* old = new (std::nothrow) mesh::LocalIdentity(*_host.identity);
  if (!old) { c.error = "no-memory"; return ST_BUSY; }
  const char* err = "failed";
  uint32_t job = 0;
  {
    int64_t j;
    if (c.doc && jsonGetInt(*c.doc, jsonFind(*c.doc, 0, "job_id"), 0, 0xFFFFFFFFLL, &j)) job = (uint32_t)j;
  }
  const int r = _host.rotate(_host.user, _id_hex, job, &err);
  if (r == 0) { wipe(old, sizeof(*old)); delete old; c.error = err; return ST_FAILED; }
  _old_id = old;   // signs this result; compute() wipes it
  if (r == 2) { c.error = "contacts_not_dropped"; return ST_FAILED; }
  hexOf(newhex, _host.node_key, 32);
  JsonWriter& w = *c.out;
  w.key("old_public_key"); w.str(oldhex);
  w.key("new_public_key"); w.str(newhex);
  w.key("removed"); w.beginArray();
  w.str("node_key");
  for (const char* const* p = _host.removed; p && *p; p++) w.str(*p);
  w.endArray();
  w.key("kept"); w.beginArray();
  w.str("configuration"); w.str("record"); w.str("posture"); w.str("device_id");
  w.endArray();
  return ST_OK;
}

// Whether the caller may use a stream of this class. The console: the CLI privilege table, as for any console line. The
// broker, as on the Companion: the caller's real context, and a stream beyond read also needs the session itself to be
// allowed it, since a signature authorises the request and not the wire. The logs go to a subscriber a Deployment member
// signed for, over a session that may carry private data; a plain broker login never gets that far.
bool ConsoleIface::streamAllow(void* user, uint8_t cls) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (s->_on_mqtt) {
    if (!Cli::decide(s->_ctx, cls).allow) return false;
    if (cls == Cli::C_READ || Cli::decide(s->_link_ctx, cls).allow) return true;
    return cls == Cli::C_POSTURE && s->_ctx.auth == Cli::A_SIGNED && Cli::decide(s->_link_ctx, Cli::C_PRIVATE).allow;
  }
  Cli::Context c(Cli::T_USB, Cli::A_CABLE, s->_host.posture(s->_host.user));
  s->_auth.stampContext(c, &s->_unlock);   // enrolled and unlocked, as for every console decision
  return Cli::decide(c, cls).allow;
}

// One received packet, as a console line and on the broker, within the stream's rate. Not on the console while a request
// is being typed: a line never lands inside another, so with no broker subscriber that packet counts as dropped.
void ConsoleIface::onObserve(void* user, float snr, float rssi, const uint8_t* raw, int len) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const bool con = s->_subs.s[SID_OBSERVATIONS].active && s->_out;
  const bool net = s->_mqtt_subs.s[SID_OBSERVATIONS].active && s->_ev_publish;
  if (!con && !net) return;
  if (s->_collecting && !net) { s->_obs.dropped++; return; }
  if (!s->_obs.take(millis())) return;
  static const size_t CAP = 1024;
  char* ev = static_cast<char*>(malloc(CAP));
  if (!ev) { s->_obs.dropped++; return; }
  bool known;
  const uint32_t ts = s->knownSeconds(&known);
  int n = writeObservation(ev, CAP, s->_clock, s->_epoch, known, ts, snr, rssi, raw, len, s->_obs.dropped);
  if (n > 0) n = s->_prov.sign("observations", ev, (size_t)n, CAP);   // -1: not signable, so not sent
  if (n <= 0) s->_obs.dropped++;   // and counted, so the next event tells the manager how many went missing
  if (n > 0) {
    s->_obs.dropped = 0;
    s->emit(SID_OBSERVATIONS, ev, (size_t)n);
  }
  free(ev);
}

bool ConsoleIface::consoleUnlocked(void* self) {
  ConsoleIface* s = static_cast<ConsoleIface*>(self);
  return s->_auth.unlocked(&s->_unlock);
}

uint32_t ConsoleIface::knownSeconds(bool* known) {
  const uint32_t ts = _host.clock_s ? _host.clock_s(_host.user) : 0;
  *known = ts >= 1704067200;   // 2024-01-01: a clock that has never been set reads earlier
  return *known ? ts : 0;
}

void ConsoleIface::emit(int stream, const char* ev, size_t n) {
  if (_out && !_collecting && _subs.s[stream].active) printLine(*_out, ev, n);
  if (_mqtt_subs.s[stream].active && _ev_publish) _ev_publish(STREAMS[stream].name, ev, n);
}

bool ConsoleIface::mqttSubscribed() const {
  for (int i = 0; i < STREAM_COUNT; i++) if (_mqtt_subs.s[i].active) return true;
  return false;
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
  j.who_mgmt = s->_ctx.mgmt ? 1 : 0;
  j.who_audit = 0;
}

// A revert line runs as the console did for the caller who applied it: the signed flag is set again for a signed
// caller, so the CLI decides and audits it as that caller.
bool ConsoleIface::confirmRunAs(void* user, const Job& j, const char* line, char* reply, size_t cap) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const bool was = Cli::serverCaller().signed_line;
  Cli::serverCaller().signed_line = j.who_auth == Cli::A_SIGNED;
  bool known;
  if (j.who_transport == Cli::T_MQTT) {   // and a broker's caller runs as that session, never as the cable
    const Cli::Context net(j.who_transport, j.who_auth, s->_host.posture(s->_host.user), j.who_mgmt != 0);
    const Cli::NetLineScope scope(net);
    known = s->_host.run(s->_host.user, line, reply, cap);
  } else {
    known = s->_host.run(s->_host.user, line, reply, cap);
  }
  Cli::serverCaller().signed_line = was;
  return known;
}

bool ConsoleIface::confirmJobId(void* user, const Request& req, char* out, size_t cap) {
  return JobService::identity(&static_cast<ConsoleIface*>(user)->_auth.answering(), req, out, cap);
}

bool ConsoleIface::jobRevert(void* user, Job& j) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (Upd::UpdateService::owns(j)) return s->_updates.jobRevert(j);
  if (mgmtOwnsJob(j)) {   // the management channel's ops, by exact name
    if (!mgmtRevert(s->mgmtHost(), j)) return false;
    s->_jobs.table()->setState(&j, JOB_REVERTED);
    return true;
  }
  if (strcmp(j.op, "config.apply") != 0) return false;
  bool reboot = false;
  if (!configRevert(s->configHost(), j, &reboot)) return false;
  s->_jobs.table()->setState(&j, JOB_REVERTED);
  // The servers apply the radio only at boot, so a restored radio needs one more reboot, at boot too.
  if (reboot) s->_reboot_at = millis() + 1500;
  return true;
}

bool ConsoleIface::jobConfirm(void* user, Job& j) {
  if (Upd::UpdateService::owns(j)) return static_cast<ConsoleIface*>(user)->_updates.jobConfirm(j);
  if (mgmtOwnsJob(j)) {
    mgmtConfirmed(static_cast<ConsoleIface*>(user)->mgmtHost());
    return true;
  }
  if (strcmp(j.op, "config.apply") != 0) return false;
  configConfirmed(static_cast<ConsoleIface*>(user)->configHost());
  return true;
}

bool ConsoleIface::updSigned(void* user) { return static_cast<ConsoleIface*>(user)->_auth.isSigned(); }

// After the answer has gone out: an activated image boots, or a reverted one goes back to the previous image.
void ConsoleIface::updReboot(void* user) { static_cast<ConsoleIface*>(user)->_reboot_at = millis() + 1500; }

// The console is one link, and its caller may always read: each change of a job's state is a line on it, unless a
// request is being typed (then it is still in job.get and job.list). The broker session gets it too, once it has been
// allowed to read.
void ConsoleIface::jobChanged(void* user, const Job& j) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const bool con = s->_out && !s->_collecting;
  const bool net = s->_mqtt_may_read && s->_ev_publish && s->_ev_up && s->_ev_up();
  if (!con && !net) return;
  // On the heap: a job opens from inside a request, already deep in the loop's stack.
  const size_t cap = 512 + Job::RESULT_CAP;
  char* ev = static_cast<char*>(malloc(cap));
  if (!ev) return;   // job.get and job.list still have it
  const uint32_t ts = s->_host.clock_s ? s->_host.clock_s(s->_host.user) : 0;
  int n = s->_jobs.writeEvent(ev, cap, s->_epoch, ts != 0, ts, j);
  if (n > 0) n = s->_prov.sign("job", ev, (size_t)n, cap);
  if (n > 0 && con) s->printLine(*s->_out, ev, (size_t)n);
  if (n > 0 && net) s->_ev_publish("job", ev, (size_t)n);
  free(ev);
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
  const uint8_t cls = op.priv == PRIV_READ    ? Cli::C_READ
                      : op.priv == PRIV_WRITE  ? Cli::C_WRITE
                      : op.priv == PRIV_UPDATE ? Cli::C_UPDATE
                                               : Cli::C_POSTURE;
  const Cli::Verdict v = Cli::decide(s->_ctx, cls);   // a refusal says why, as on the Companion
  if (!v.allow) s->_deny = Cli::reasonName(v.reason);
  return v.allow;
}

const char* ConsoleIface::denyReason(void* user) { return static_cast<ConsoleIface*>(user)->_deny; }

// One request through the dispatcher as the caller ctx: the cable or the broker session, or the signed principal on
// either. The posture is read again each time, so a posture change takes effect on the next line.
int ConsoleIface::run(const Cli::Context& ctx, const char* msg, size_t len, char* out, size_t cap, ResultCache* cache) {
  _ctx = ctx;
  _ctx.posture = _host.posture(_host.user);
  _auth.setLink(_on_mqtt ? &_mqtt_unlock : &_unlock);
  _auth.stampContext(_ctx, _on_mqtt ? nullptr : &_unlock);   // the cable's unlock reaches the table as a flag
  _auth.setContext(_ctx);
  if (_on_mqtt) _mqtt_may_read = Cli::decide(ctx, Cli::C_READ).allow;
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
  d.setDenyReason(denyReason);
  d.setAnswering(&_auth.answering());
  if (_on_lora) d.setOpFilter(loraOpAllowed);
  return d.handle(msg, len, out, cap, cache);
}

int ConsoleIface::runSigned(void* user, const Cli::Context& ctx, const char* body, size_t len, char* out, size_t cap) {
  return static_cast<ConsoleIface*>(user)->run(ctx, body, len, out, cap, nullptr);   // job ids guard replay; no id cache
}

// One request over the broker, on the same worker as a console line. While it runs, the role's command handlers see the
// session (Cli::NetLineScope), so a line it causes is decided and audited as that caller.
__attribute__((noinline)) int ConsoleIface::computeMqtt(const Cli::Context& ctx, const char* msg, size_t len, char* out, size_t cap) {
  _on_mqtt = true;
  _link_ctx = ctx;
  const Cli::NetLineScope scope(_link_ctx);
  if (!_env_checked) refreshEnvironment(millis());
  if (!_cache) _cache = new (std::nothrow) ResultCache();
  _auth.answering().clear();
  const bool was_enrolled = _auth.enrolled();
  int n;
  if (!_cache) {
    n = writeResult(out, cap, "", ST_BUSY, nullptr, "no-memory");
  } else {
    if (!_mqtt_nonce[0]) DeployAuth::issueNonce(_mqtt_nonce);   // issued when the session's first request arrives
    if (DeployAuth::isWrapper(msg, len))
      n = _auth.dispatchSigned(ctx, _mqtt_nonce, msg, len, _cache, runSigned, this, out, cap);
    else
      n = run(ctx, msg, len, out, cap, _cache);
  }
  if (n > 0) n = _prov.signResult(out, (size_t)n, cap, _old_id, &_auth.answering(), was_enrolled);
  if (_old_id) { wipe(_old_id, sizeof(*_old_id)); delete _old_id; _old_id = nullptr; }
  _auth.answering().clear();
  _on_mqtt = false;
  return n;
}

namespace {
struct MqttJob {
  ConsoleIface* s;
  const Cli::Context* ctx;
  const char* msg;
  size_t len;
  char* out;
  size_t cap;
  int n;
};
__attribute__((noinline)) void mqttJob(void* p) {
  MqttJob* j = static_cast<MqttJob*>(p);
  j->n = j->s->computeMqtt(*j->ctx, j->msg, j->len, j->out, j->cap);
}
}  // namespace

size_t ConsoleIface::onMqtt(const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap) {
  MqttJob job = {this, &ctx, reinterpret_cast<const char*>(msg), len, out, cap, -1};
  if (NrfWorker::run(mqttJob, &job, 15000)) {
    _updates.replyWritten(millis());   // the reply goes out on this pass: a DFU reset the request asked for waits for it
    return job.n > 0 ? (size_t)job.n : 0;
  }
  static const char W[] = "{\"v\":1,\"id\":\"\",\"status\":\"busy\",\"error\":\"worker\",\"unsigned\":\"worker\"}";
  if (cap <= sizeof(W)) return 0;
  memcpy(out, W, sizeof(W));
  return sizeof(W) - 1;
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
  _loc_ok = _host.sensors->getLocationProvider() != nullptr;
  _env_checked = true;
  _env_at = now;
}

// The request's whole work, from the parse to the signed result, on the nRF52 worker's stack (NrfWorker.h, #158): the
// loop task only prints the line. res is OUT_MAX bytes, or null when it could not be allocated (then the small
// no-memory answer is written into small).
__attribute__((noinline)) int ConsoleIface::compute(char* res, char* small, size_t small_cap) {
  bool was_enrolled = false;   // whether a record existed when the request began
  int n;
  if (!res) {
    n = writeResult(small, small_cap, "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) n = _prov.signResult(small, (size_t)n, small_cap);
    return n;
  }
  {
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
    if (n > 0) n = _prov.signResult(res, (size_t)n, OUT_MAX, _old_id, &_auth.answering(), was_enrolled);
    if (_old_id) { wipe(_old_id, sizeof(*_old_id)); delete _old_id; _old_id = nullptr; }   // the retired key: gone after this one result
    _auth.answering().clear();
  }
  return n;
}

namespace {
struct AnswerJob {
  ConsoleIface* s;
  char* res;
  char* small;
  size_t small_cap;
  int n;
};
__attribute__((noinline)) void answerJob(void* p) {
  AnswerJob* j = static_cast<AnswerJob*>(p);
  j->n = j->s->compute(j->res, j->small, j->small_cap);
}
}  // namespace

void ConsoleIface::answer(Print& out) {
  _collecting = false;
  if (!_env_checked) refreshEnvironment(millis());
  out.print("\r\n");   // ends the console's echo of the prefix, as the stock console ends an echoed line
  char* res = static_cast<char*>(malloc(OUT_MAX));
  static char small[320];
  AnswerJob job = {this, res, small, sizeof(small), -1};
  if (NrfWorker::run(answerJob, &job, 15000)) {
    if (job.n > 0) printLine(out, res ? res : small, (size_t)job.n);
    _updates.replyWritten(millis());   // a DFU reset the request asked for waits for its reply
  } else {
    // The worker could not be created (no memory): nothing runs on the small loop stack, not even a signature, so the
    // refusal goes out unsigned and says why. (A job that overran would have reset the device instead.)
    static const char W[] = "{\"v\":1,\"id\":\"\",\"status\":\"busy\",\"error\":\"worker\",\"unsigned\":\"worker\"}";
    printLine(out, W, sizeof(W) - 1);
  }
  free(res);
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
    case SID_LOCATION: {
      LocationFix fix;
      if (!_host.sensors || !readLocation(_host.sensors->getLocationProvider(), fix)) return false;
      writeLocation(w, fix);
      break;
    }
    case SID_INVENTORY:
      if (!_host.inventory) return false;
      _host.inventory(_host.user, w);
      break;
    case SID_CONNECTIVITY:
      if (!_conn_write) return false;
      _conn_write(w);
      break;
    default: return false;
  }
  w.endObject();
  return w.ok();
}

void ConsoleIface::onLoraFrame(void* user, const uint8_t* key, const uint8_t* secret, const uint8_t* path, uint8_t path_len,
                               const uint8_t* frame, size_t n) {
  static_cast<ConsoleIface*>(user)->_lora.feed(key, secret, path, path_len, frame, n, LORA_REQ_PAYLOAD, millis());
}

// One complete message on this pass, then its reply a frame at a time. Over LoRa only a signed request runs (section 2).
void ConsoleIface::loraTick(uint32_t now) {
  if (!_lora_send) return;
  if (LoraLink::Peer* p = _lora.nextComplete()) {
    char* res = static_cast<char*>(malloc(LORA_MSG_MAX + 1));
    if (res) {
      const char* msg = LoraLink::message(*p);
      const size_t len = LoraLink::messageLen(*p);
      int n;
      bool verified = false;
      if (DeployAuth::isWrapper(msg, len)) {
        _on_lora = true;
        const Cli::Context ctx(Cli::T_LORA, Cli::A_NONE, _host.posture(_host.user));
        n = (int)onMqtt(ctx, reinterpret_cast<const uint8_t*>(msg), len, res, LORA_MSG_MAX + 1);
        _on_lora = false;
        verified = _auth.lastVerified();
      } else {
        n = writeResult(res, LORA_MSG_MAX + 1, "", ST_REFUSED, nullptr, "unauthenticated");
      }
      if (n > 0) _lora.queueReply(*p, res, (size_t)n, verified, now);
      free(res);
    }
    _lora.release(*p);
  }
  _lora.tick(now, _lora_send, _lora_send_user);
}

void ConsoleIface::loop(uint32_t now, Print& out) {
  _out = &out;
  loraTick(now);
  _gap.tick(now);
  _jobs.tick(now);
  _updates.loop(now);
  if (_reboot_at && (int32_t)(now - _reboot_at) >= 0 && _host.board) _host.board->reboot();
  refreshEnvironment(now);
  if (_ev_up && !_ev_up()) {
    _mqtt_subs.clear();   // a subscription made over the broker lapses with the session
    _mqtt_nonce[0] = 0;   // and so does the session's channel nonce
  }
  if (_conn_sig) {   // a link changed state: the connectivity stream sends an event now
    const uint32_t sig = _conn_sig();
    if (sig != _conn_last) {
      _conn_last = sig;
      _clock.scheduleNow(SID_CONNECTIVITY, now);
    }
  }
  if (_collecting && !mqttSubscribed()) return;   // an event never lands in the middle of a request being typed
  SubSet* sets[2] = { &_subs, &_mqtt_subs };
  const uint32_t due = _clock.due(sets, 2, now);
  bool known;
  const uint32_t ts = knownSeconds(&known);
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[800];   // an event of up to 640 bytes, and its signature
    int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, known, ts, data);
    if (n > 0) n = _prov.sign(STREAMS[s].name, ev, (size_t)n, sizeof(ev));   // -1: not signable, so not sent
    if (n > 0) emit(s, ev, (size_t)n);
  }
  if (_boot_pending) {   // once per epoch, right after the first runtime snapshot
    _boot_pending = false;
    if (_subs.s[SID_RUNTIME].active || _mqtt_subs.s[SID_RUNTIME].active) {
      _boot_sent = true;
      // static: the loop task's stack has no room for these next to the signer (the stack gate, #234); loop only
      static char data[384];
      JsonWriter w(data, sizeof(data));
      w.beginObject();
      writeBoot(w, *_host.board, _host.role, _host.hardware);
      w.endObject();
      static char ev[640];   // the event and its signature
      int n = w.ok() ? writeEvent(ev, sizeof(ev), STREAMS[SID_RUNTIME].name, ++_clock.seq[SID_RUNTIME], _epoch, known, ts, data) : -1;
      if (n > 0) n = _prov.sign(STREAMS[SID_RUNTIME].name, ev, (size_t)n, sizeof(ev));   // signed like every event (#234)
      if (n > 0) emit(SID_RUNTIME, ev, (size_t)n);
    }
  }
  if (!_subs.s[SID_LOGS].active && !_mqtt_subs.s[SID_LOGS].active) {
    _logs.skip(Posture::audit());
  } else {
    Posture::AuditEntry a[Posture::AuditRing::CAPACITY];
    uint32_t dropped = 0;
    const int k = _logs.poll(now, Posture::audit(), a, &dropped);
    for (int j = 0; j < k; j++) {
      static char ev[480];   // the event and its signature; static for the loop task's stack (#234), loop only
      int n = writeLog(ev, sizeof(ev), _clock, _epoch, known, ts, a[j], j == 0 ? dropped : 0);
      if (n > 0) n = _prov.sign(STREAMS[SID_LOGS].name, ev, (size_t)n, sizeof(ev));   // signed like every event (#234)
      if (n > 0) emit(SID_LOGS, ev, (size_t)n);
    }
  }
}

}  // namespace Iface
