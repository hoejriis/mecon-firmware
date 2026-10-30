#include "MeconIface.h"
#include <helpers/updates/UpdFloor.h>
#include <helpers/Uptime.h>
#include <helpers/iface/DevIdHello.h>
#include <helpers/iface/Wipe.h>
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <helpers/MeconVersion.h>
#include <helpers/MemStats.h>
#include <helpers/NrfWorker.h>
#include <helpers/HeapFloor.h>
#include <helpers/Posture.h>
#include <helpers/cli/CliPolicy.h>
#include <helpers/cli/CliRegistry.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/CryptoRNG.h>
#include <helpers/iface/IfaceConfig.h>
#include <helpers/iface/Streams.h>
#include <helpers/iface/Observe.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace Iface;

namespace {

MeconIface* self(OpContext& c) { return static_cast<MeconIface*>(c.user); }

void hexOf(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
  out[2 * n] = 0;
}

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply",
                                    "sub.start", "sub.stop", "deployment.get", "deployment.provision",
                                    "deployment.authority_update", "deployment.reset", "deployment.set_unlock",
                                    "deployment.unlock_challenge", "deployment.unlock", "identity.rotate", "identity.rotation",
                                    "job.get", "job.list", "job.ack", "job.cancel", "job.confirm",
                                    "update.manifest", "update.status"
#ifdef MAX_GROUP_CHANNELS
                                    , "channel.list", "channel.get", "channel.put", "channel.remove",
                                    "mgmt.channel.get", "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind"
#endif
                                    };

Status opHello(OpContext& c);
Status opStatus(OpContext& c);
Status opSchema(OpContext& c);
Status opGet(OpContext& c);
Status opApply(OpContext& c);
Status opSubStart(OpContext& c);
Status opSubStop(OpContext& c);
Status opJobGet(OpContext& c) { return self(c)->jobs().opGet(c); }
Status opJobList(OpContext& c) { return self(c)->jobs().opList(c); }
Status opJobAck(OpContext& c) { return self(c)->jobs().opAck(c); }
Status opJobCancel(OpContext& c) { return self(c)->jobs().opCancel(c); }
Status opJobConfirm(OpContext& c) { return self(c)->jobs().opConfirm(c); }
Status opUpdManifest(OpContext& c) { return self(c)->updates().opManifest(c, millis()); }
Status opUpdStatus(OpContext& c) { return self(c)->updates().opStatus(c); }
Status opChanList(OpContext& c);
Status opChanGet(OpContext& c);
Status opChanPut(OpContext& c);
Status opChanRemove(OpContext& c);
Status opMgmtGetW(OpContext& c);
Status opMgmtBindW(OpContext& c);
Status opMgmtRotateW(OpContext& c);
Status opMgmtUnbindW(OpContext& c);

// The operation's class is the least it needs; each config key is decided again by its own CLI line (IfaceConfig).
const OpEntry OPS[] = {
  {"hello", opHello, PRIV_READ, false},
  {"status.get", opStatus, PRIV_READ, false},
  {"config.schema", opSchema, PRIV_READ, false},
  {"config.get", opGet, PRIV_READ, false},
  {"config.apply", opApply, PRIV_WRITE, true},
  {"sub.start", opSubStart, PRIV_READ, false},
  {"sub.stop", opSubStop, PRIV_READ, false},
  {"deployment.get", MeconIface::opDeployGet, PRIV_READ, false},
  {"deployment.provision", MeconIface::opDeployProvision, PRIV_WRITE, true},
  {"deployment.authority_update", MeconIface::opDeployUpdate, PRIV_WRITE, true},
  {"deployment.reset", MeconIface::opDeployReset, PRIV_WRITE, true},
  {"deployment.set_unlock", MeconIface::opDeploySetUnlock, PRIV_WRITE, true},
  // The handshake is never cached: a stored "unlocked" must not answer a request after the unlock has lapsed.
  {"deployment.unlock_challenge", MeconIface::opDeployUnlockChallenge, PRIV_WRITE, false},
  {"deployment.unlock", MeconIface::opDeployUnlock, PRIV_WRITE, false},
  {"identity.rotate", MeconIface::opIdentityRotate, PRIV_WRITE, false},   // never answered from the cache: it must run once
  {"identity.rotation", MeconIface::opIdentityRotation, PRIV_READ, false},
  // Reading a job and acknowledging its result are of the read class (the result belongs to whoever may read it);
  // cancelling and confirming change the device. The three that change something keep their result (OPERATIONS.md 5.1).
  {"job.get", opJobGet, PRIV_READ, false},
  {"job.list", opJobList, PRIV_READ, false},
  {"job.ack", opJobAck, PRIV_READ, true},
  {"job.cancel", opJobCancel, PRIV_WRITE, true},
  {"job.confirm", opJobConfirm, PRIV_WRITE, true},
  // Staging a manifest changes nothing persistent; it is for any caller with read (UPDATES.md section 8).
  {"update.manifest", opUpdManifest, PRIV_READ, false},
  {"update.status", opUpdStatus, PRIV_READ, false},
#ifdef MAX_GROUP_CHANNELS
  // The native channel table; the secret is written, never read (OPERATIONS.md section 5.1).
  {"channel.list", opChanList, PRIV_READ, false},
  {"channel.get", opChanGet, PRIV_READ, false},
  {"channel.put", opChanPut, PRIV_WRITE, true},
  {"channel.remove", opChanRemove, PRIV_WRITE, true},
  // The management channel binding (MANAGEMENT_CHANNEL.md); bind, rotate and unbind confirm-or-revert for a remote caller.
  {"mgmt.channel.get", opMgmtGetW, PRIV_READ, false},
  {"mgmt.channel.bind", opMgmtBindW, PRIV_WRITE, true},
  {"mgmt.channel.rotate", opMgmtRotateW, PRIV_WRITE, true},
  {"mgmt.channel.unbind", opMgmtUnbindW, PRIV_WRITE, true},
#endif
};

}  // namespace

// ---- operations

namespace {

Status opHello(OpContext& c) {
  MeconIface* m = self(c);
  MyMesh* mesh = m->mesh();
  JsonWriter& w = *c.out;
  char key[2 * PUB_KEY_SIZE + 1];
  hexOf(key, mesh->self_id.pub_key, PUB_KEY_SIZE);
  w.key("interface"); w.str("1.0");
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("node_key"); w.str(key);
  const Status ds = writeDeviceIdSig(c, mesh->self_id, m->deviceIdHex());
  if (ds != ST_OK) return ds;
  w.key("role"); w.str("companion");
  w.key("hardware"); w.str(mesh->hardwareName());
  w.key("firmware"); w.str(mecon::versionFull());
  w.key("upstream"); w.str(MECON_UPSTREAM_BASE);
  w.key("epoch"); w.unum(m->epoch());
  // The channel nonce of the link this reply goes to, only once a Deployment record exists (AUTHORITY.md section 7.1).
  const char* nonce = m->helloNonce();
  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }
  w.key("capabilities"); w.beginArray();
  for (const char* cap : CAPABILITIES) w.str(cap);
  writeStreamCapabilities(w, m->streamsProduced());
  w.endArray();
  return ST_OK;
}

// The health fields, shared by status.get and the health stream so the two never disagree.
void writeHealth(JsonWriter& w, MyMesh* mesh) {
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
    if (ms.psram) { w.key("psram_free"); w.unum(ms.psram); }
    w.endObject();
  }
  const uint16_t mv = mesh->batteryMilliVolts();
  w.key("battery_mv"); if (mv) w.unum(mv); else w.null();
  w.key("contacts"); w.beginObject();
  w.key("count"); w.unum(mesh->getNumContacts());
  w.key("capacity"); w.unum(mesh->getContactCapacity());
  w.endObject();
}

Status opStatus(OpContext& c) {
  MeconIface* m = self(c);
  MyMesh* mesh = m->mesh();
  JsonWriter& w = *c.out;
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("epoch"); w.unum(m->epoch());
  writeHealth(w, mesh);
#ifdef ENABLE_CONNMODE
  w.key("connectivity"); w.str(mesh->getConnectivityMode() == ConnMode::WIFI ? "wifi" : "ble");
#endif
  w.key("posture"); w.str(Posture::isHardened(mesh->postureValue()) ? "hardened" : "normal");
  m->writeDeploymentStatus(w, Posture::isHardened(mesh->postureValue()));
  m->jobs().writeStatus(w);
  return ST_OK;
}

// sub.start / sub.stop on the calling transport (Streams.h): the stream table decides names, bounds and the reply.
Status opSubStart(OpContext& c) {
  MeconIface* m = self(c);
  int started;
  const uint32_t now = millis();
  Status st = subStart(c, m->currentSubs(), m->streamsProduced(), now, &started, MeconIface::streamAllow, m);
  m->scheduleStream(started, now);
  return st;
}

Status opSubStop(OpContext& c) { return subStop(c, self(c)->currentSubs(), self(c)->streamsProduced()); }


// Settings run as the caller's own CLI lines (design section 5): one store, and the CLI engine authorizes and audits
// each line exactly as it would a CMD_RUN_CLI_COMMAND frame from the same transport.
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

Status withChannels(OpContext& c, Status (*op)(OpContext&)) {
  ChannelHost host = self(c)->channelHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opChanList(OpContext& c) { return withChannels(c, opChannelList); }
Status opChanGet(OpContext& c) { return withChannels(c, opChannelGet); }
// A write to the management channel's own slot goes through the binding: confirm-or-revert for a remote caller.
Status opChanPut(OpContext& c) {
  MgmtHost h = self(c)->mgmtHost();
  return mgmtGuardChannel(c, h, opChannelPut, "channel.put");
}
Status opChanRemove(OpContext& c) {
  MgmtHost h = self(c)->mgmtHost();
  return mgmtGuardChannel(c, h, opChannelRemove, "channel.remove");
}

Status withMgmt(OpContext& c, Status (*op)(OpContext&)) {
  MgmtHost host = self(c)->mgmtHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opMgmtGetW(OpContext& c) { return withMgmt(c, opMgmtGet); }
Status opMgmtBindW(OpContext& c) { return withMgmt(c, opMgmtBind); }
Status opMgmtRotateW(OpContext& c) { return withMgmt(c, opMgmtRotate); }
Status opMgmtUnbindW(OpContext& c) { return withMgmt(c, opMgmtUnbind); }

}  // namespace

// ---- channels: the native table

ChannelHost MeconIface::channelHost() {
#ifdef MAX_GROUP_CHANNELS
  return ChannelHost{MAX_GROUP_CHANNELS, chanGet, chanSet, chanFind, this};
#else
  return ChannelHost{0, chanGet, chanSet, chanFind, this};
#endif
}

bool MeconIface::chanGet(void* user, int slot, ChannelInfo& out) {
  ChannelDetails ch;
  if (!static_cast<MeconIface*>(user)->_mesh->getChannel(slot, ch)) return false;
  memcpy(out.name, ch.name, CHANNEL_NAME_MAX);
  out.name[CHANNEL_NAME_MAX] = 0;
  out.hash = ch.channel.hash[0];
  static const uint8_t ZERO[16] = {0};
  out.key_bytes = memcmp(&ch.channel.secret[16], ZERO, 16) == 0 ? 16 : 32;   // as setChannel() hashes it
  out.keyed = memcmp(ch.channel.secret, ZERO, 16) != 0 || out.key_bytes == 32;
  wipe(&ch, sizeof(ch));
  return true;
}

// Writes the slot and saves the table, as the native CMD_SET_CHANNEL frame does. When the save fails, the slot's
// previous contents go back into RAM, so RAM and flash agree and a removed key cannot return after a reboot. An
// empty name with an all-zero key is how the app clears a slot.
bool MeconIface::chanSet(void* user, int slot, const char* name, const uint8_t* secret) {
  MyMesh* mesh = static_cast<MeconIface*>(user)->_mesh;
  ChannelDetails ch, before;
  if (!mesh->getChannel(slot, before)) return false;
  memset(&ch, 0, sizeof(ch));
  StrHelper::strncpy(ch.name, name, sizeof(ch.name));
  memcpy(ch.channel.secret, secret, CHANNEL_SECRET_BYTES);
  bool ok = mesh->setChannel(slot, ch);
  if (ok && !mesh->saveChannels()) {
    mesh->setChannel(slot, before);
    ok = false;
  }
  wipe(&ch, sizeof(ch));
  wipe(&before, sizeof(before));
  return ok;
}

int MeconIface::chanFind(void* user, const uint8_t* secret) {
  mesh::GroupChannel g;
  memset(&g, 0, sizeof(g));
  memcpy(g.secret, secret, CHANNEL_SECRET_BYTES);
  const int slot = static_cast<MeconIface*>(user)->_mesh->findChannelIdx(g);
  wipe(&g, sizeof(g));
  return slot;
}

// ---- settings: the caller's CLI

bool MeconIface::cliRun(void* user, const char* line, char* reply, size_t cap) {
  MeconIface* self = static_cast<MeconIface*>(user);
  MyMesh* mesh = self->_mesh;
  mesh->setCliCaller(self->_ctx, self->_caller);
  char buf[sizeof(mesh->reply_buf)];
  buf[0] = 0;
  // The CLI gate takes "remote" from the timestamp (a remote caller always has a non-zero one), as runMqttCli does.
  const bool known = mesh->handleCommand(line, self->_ctx.remote ? 1 : 0, buf);
  mesh->setCliCaller(Cli::Context(), Posture::CALLER_NET);   // no context outlives the line
  snprintf(reply, cap, "%s", buf);
  memset(buf, 0, sizeof(buf));
  return known;
}

bool MeconIface::cliAllow(void* user, const char* line) {
  const Cli::Resolved r = Cli::resolve(line, Cli::ROLE_COMPANION);
  return Cli::decide(static_cast<MeconIface*>(user)->_ctx, r.cls, r.flags).allow;
}

// ---- transport glue

bool MeconIface::authorize(void* user, const OpEntry& op, const Request&) {
  MeconIface* self = static_cast<MeconIface*>(user);
  self->_deny = nullptr;
  // Open to any caller that can reach the interface: the read of the stored set (it reveals nothing secret), and the
  // DAK-signed authority update (it carries its own proof; AUTHORITY.md sections 6.2 and 6.3). identity.rotation is not:
  // the rotation statement names device_id, which an untrusted link must not learn (section 3.5).
  if (strcmp(op.name, "deployment.get") == 0 || strcmp(op.name, "deployment.authority_update") == 0) return true;
  // Once a device holds a record, a transport is not authority: an unsigned request may read, and nothing more. A first
  // or repeated provisioning is decided by its handler (it knows the transport and the stored record).
  // Likewise the reset, whose handler knows the record's state, the transport and the posture (section 11).
  switch (self->_auth.preAuthorize(op, &self->_deny)) {
    case Iface::DeployAuth::PRE_ALLOW: return true;
    case Iface::DeployAuth::PRE_DENY: return false;
    case Iface::DeployAuth::PRE_TABLE: break;
  }
  const uint8_t cls = op.priv == PRIV_READ ? Cli::C_READ : op.priv == PRIV_WRITE ? Cli::C_WRITE : Cli::C_POSTURE;
  return Cli::decide(self->_ctx, cls).allow;
}

const char* MeconIface::denyReason(void* user) { return static_cast<MeconIface*>(user)->_deny; }

int MeconIface::runDispatch(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out,
                            size_t cap, ResultCache* cache) {
  _ctx = ctx;
  _auth.setLink(link ? &link->unlock : &_mqtt_unlock);
  _auth.stampContext(_ctx, link ? &link->unlock : nullptr);   // the cable link's unlock reaches the table as a flag
  _auth.setContext(_ctx);
  _caller = caller;
  _cur_link = link;
  (link ? link->may_read : _mqtt_may_read) = Cli::decide(ctx, Cli::C_READ).allow;
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
  d.setDenyReason(denyReason);
  d.setAnswering(&_auth.answering());
  return d.handle(msg, len, out, cap, cache);
}

// Every result of the interface is signed while a record exists (AUTHORITY.md section 3.4), whichever path produced it.
namespace {
struct DispatchJob {
  MeconIface* m;
  const Cli::Context* ctx;
  uint8_t caller;
  void* link;
  const char* msg;
  size_t len;
  char* out;
  size_t cap;
  int n;
};
}  // namespace

// The request's whole work runs on the nRF52 worker's stack (helpers/NrfWorker.h, #158); elsewhere it runs inline.
__attribute__((noinline)) void MeconIface::dispatchJob(void* p) {
  DispatchJob* j = static_cast<DispatchJob*>(p);
  j->n = j->m->dispatchNow(*j->ctx, j->caller, static_cast<Link*>(j->link), j->msg, j->len, j->out, j->cap);
}

int MeconIface::dispatch(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out,
                         size_t cap) {
  DispatchJob j = {this, &ctx, caller, link, msg, len, out, cap, -1};
  if (NrfWorker::run(dispatchJob, &j, 15000)) return j.n;
  // The worker could not be created (no memory): nothing runs on the small loop stack, not even a signature.
  static const char W[] = "{\"v\":1,\"id\":\"\",\"status\":\"busy\",\"error\":\"worker\",\"unsigned\":\"worker\"}";
  if (cap <= sizeof(W)) return -1;
  memcpy(out, W, sizeof(W));
  return (int)sizeof(W) - 1;
}

__attribute__((noinline)) int MeconIface::dispatchNow(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg,
                                                      size_t len, char* out, size_t cap) {
  _auth.answering().clear();
  const bool was_enrolled = _auth.enrolled();
  int n = dispatchInner(ctx, caller, link, msg, len, out, cap);
  if (n > 0) n = _prov.signResult(out, (size_t)n, cap, _old_id, &_auth.answering(), was_enrolled);
  _auth.answering().clear();
  if (_old_id) { Iface::wipe(_old_id, sizeof(*_old_id)); delete _old_id; _old_id = nullptr; }   // the retired key lives for one result
  return n;
}

int MeconIface::dispatchInner(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out,
                              size_t cap) {
  _link_ctx = ctx;   // the transport's own context, before any signature replaces it
  _ctx = ctx;
  _auth.setContext(ctx);
  _caller = caller;
  _cur_link = link;
  if (!_env_checked) refreshEnvironment(millis());
  if (!_cache) _cache = new (std::nothrow) ResultCache();   // 3.1 KB, only once the interface is used
  if (!_cache) return writeResult(out, cap, "", ST_BUSY, nullptr, "no-memory");
  char* nonce = nonceOf(link);
  if (!nonce[0]) Iface::DeployAuth::issueNonce(nonce);   // issued when the link's first request arrives
  if (Iface::DeployAuth::isWrapper(msg, len)) return dispatchSigned(ctx, caller, link, msg, len, out, cap);
  return runDispatch(ctx, caller, link, msg, len, out, cap, _cache);
}

size_t MeconIface::onMqtt(const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap) {
  const int n = dispatch(ctx, Posture::CALLER_MQTT, nullptr, reinterpret_cast<const char*>(msg), len, out, cap);
  return n > 0 ? (size_t)n : 0;
}

void MeconIface::begin(MyMesh* mesh, const uint8_t device_id[16], uint32_t epoch, bool epoch_saved) {
  _mesh = mesh;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
  deployLoad();
  _prov.begin(&_mesh->self_id, &_auth, epoch, epoch_saved);
  _updates.begin(_mesh->hardwareName(), "companion");
  Upd::floorAtBoot();   // the floor rises to the security version compiled into this image (UPDATES.md section 4)
  observeSink().fn = onObserve;
  observeSink().user = this;
}

// Whether the caller of the request being handled may use a stream of this class: the CLI privilege table with its
// real context, as for the CLI line the data would come from.
// A stream beyond read (logs, the private streams) also needs the link that will carry it to be allowed it by itself: a
// signature authorises the request, not the wire, so a signed sub.start on the open TCP port or a default-PIN BLE link
// does not send positions or contacts over that link.
bool MeconIface::streamAllow(void* user, uint8_t cls) {
  MeconIface* m = static_cast<MeconIface*>(user);
  if (!Cli::decide(m->_ctx, cls).allow) return false;
  return cls == Cli::C_READ || Cli::decide(m->_link_ctx, cls).allow;
}

// One received packet: an observations event to every transport subscribed, within the stream's rate.
void MeconIface::onObserve(void* user, float snr, float rssi, const uint8_t* raw, int len) {
  MeconIface* m = static_cast<MeconIface*>(user);
  bool any = m->_mqtt_subs.s[SID_OBSERVATIONS].active;
  for (Link& l : m->_links) any = any || l.subs.s[SID_OBSERVATIONS].active;
  if (!any) return;
  const uint32_t now = millis();
  if (!m->_obs.take(now)) return;
  char* ev = static_cast<char*>(malloc(RESULT_MAX));
  if (!ev) { m->_obs.dropped++; return; }
  const uint32_t ts = m->_mesh->getRTCClock()->getCurrentTime();
  int n = writeObservation(ev, RESULT_MAX, m->_clock, m->_epoch, ts >= 1704067200, ts, snr, rssi, raw, len,
                                 m->_obs.dropped);
  if (n > 0) n = m->_prov.sign("observations", ev, (size_t)n, RESULT_MAX);   // -1: not signable, so not sent
  if (n <= 0) m->_obs.dropped++;   // and counted, so the next event tells the manager how many went missing
  if (n > 0) {
    m->_obs.dropped = 0;
    for (Link& l : m->_links) {
      if (!l.subs.s[SID_OBSERVATIONS].active || !m->ensureQueue(l)) continue;
      l.q->push(OutQueue::EVENT, reinterpret_cast<const uint8_t*>(ev), (size_t)n);
    }
    if (m->_mqtt_subs.s[SID_OBSERVATIONS].active && m->_ev_publish) m->_ev_publish("observations", ev, (size_t)n);
  }
  free(ev);
}

MeconIface::Link* MeconIface::linkFor(const BaseSerialInterface* src, bool create) {
  for (Link& l : _links) if (l.src == src && src) return &l;
  if (!create) return nullptr;
  for (Link& l : _links) if (!l.src) { l.src = src; return &l; }
  return nullptr;
}

bool MeconIface::ensureQueue(Link& l) {
  if (l.q) return true;
  uint8_t* store = static_cast<uint8_t*>(malloc(QUEUE_BYTES));
  if (!store) return false;
  l.q = new (std::nothrow) OutQueue(store, QUEUE_BYTES);
  if (!l.q) free(store);
  return l.q != nullptr;
}

bool MeconIface::queueResult(Link& l, const char* msg, size_t len) {
  if (!ensureQueue(l)) return false;
  if (l.q->push(OutQueue::RESULT, reinterpret_cast<const uint8_t*>(msg), len) == OutQueue::PUSH_OK) return true;
  char busy[320];   // signed, still well inside what events must leave free (RESULT_RESERVE)
  int n = writeResult(busy, sizeof(busy), "", ST_BUSY, nullptr, "queue-full");
  if (n > 0) n = _prov.signResult(busy, (size_t)n, sizeof(busy));
  return n > 0 && l.q->push(OutQueue::RESULT, reinterpret_cast<const uint8_t*>(busy), (size_t)n) == OutQueue::PUSH_OK;
}

// A short result the interface itself raises (no request ran): signed like any other.
void MeconIface::sendSmall(Link& l, Status st, const char* error) {
  char msg[320];
  int n = writeResult(msg, sizeof(msg), "", st, nullptr, error);
  if (n > 0) n = _prov.signResult(msg, (size_t)n, sizeof(msg));
  if (n > 0) queueResult(l, msg, (size_t)n);
}

void MeconIface::onFrame(const BaseSerialInterface* src, const uint8_t* frame, size_t len, uint32_t now) {
  Link* l = linkFor(src, true);
  if (!l) return;
  RxResult r = l->rx.feed(frame, len, now);
  uint8_t xid;
  if (l->rx.takeDiscard(&xid)) {
    sendSmall(*l, ST_INVALID, "incomplete");
  }
  if (r == RX_NOMEM) {
    sendSmall(*l, ST_BUSY, "no-memory");
    return;
  }
  if (r != RX_COMPLETE) return;
  char* out = static_cast<char*>(malloc(RESULT_MAX + 1));
  if (out) {
    const Cli::Context ctx = _mesh->serialContext();
    const uint8_t caller = ctx.transport == Cli::T_USB ? Posture::CALLER_CABLE : Posture::CALLER_BLE;
    int n = dispatch(ctx, caller, l, reinterpret_cast<const char*>(l->rx.data()), l->rx.size(), out, RESULT_MAX + 1);
    if (n > 0) queueResult(*l, out, (size_t)n);
  } else {
    sendSmall(*l, ST_BUSY, "no-memory");
  }
  free(out);
  l->rx.release();
}

// Whether the environment stream has anything to report: rechecked once a minute (sensors start after the mesh), and
// before the first request is answered (a request can arrive before the first loop pass).
void MeconIface::refreshEnvironment(uint32_t now) {
  if (_env_checked && now - _env_at < 60000) return;
  _env_ok = countEnvironment(sensors, board) > 0;
  _loc_ok = sensors.getLocationProvider() != nullptr;   // the location stream: a build with a GNSS provider
  _env_checked = true;
  _env_at = now;
}

SubSet* MeconIface::currentSubs() { return _cur_link ? &_cur_link->subs : &_mqtt_subs; }

// The data of one stream's event (TELEMETRY.md section 3); false when it could not be built.
bool MeconIface::writeStream(int s, JsonWriter& w) {
  w.beginObject();
  switch (s) {
    case SID_HEALTH: writeHealth(w, _mesh); break;
    case SID_RADIO: {
      RadioCounts counts;
      counts.known = true;
      counts.rx_total = radio_driver.getPacketsRecv();
      counts.tx_total = radio_driver.getPacketsSent();
      counts.rx_errors = radio_driver.getPacketsRecvErrors();
      writeRadio(w, *_mesh, radio_driver, counts);
      break;
    }
    case SID_POWER: writePower(w, board); break;
    case SID_RUNTIME:
      writeRuntime(w, board, _gap, heapFloor().enabled() ? (heapFloor().low() ? 1 : 0) : -1);   // absent where the build does not check
      break;
    case SID_ENVIRONMENT:
      if (writeEnvironment(w, sensors, board) == 0) return false;   // nothing to report this time
      break;
    case SID_LOCATION: {
      LocationFix fix;
      if (!readLocation(sensors.getLocationProvider(), fix)) return false;
      writeLocation(w, fix);
      break;
    }
    case SID_INVENTORY: return false;   // paged: streamTick hands it to startInventory, and inventoryTick builds the pages
    case SID_CONNECTIVITY:
      if (!_conn_write) return false;
      _conn_write(w);
      break;
    default: return false;
  }
  w.endObject();
  return w.ok();
}

// Lapses expired subscriptions, then builds each stream that is due once and sends the same bytes to every transport
// subscribed. A full queue drops the event on that transport only; the receiver sees the gap in seq.
void MeconIface::streamTick(uint32_t now) {
  if (!_ev_up || !_ev_up()) {
    _mqtt_subs.clear();   // a subscription made over MQTT lapses with the session
    _mqtt_nonce[0] = 0;   // and so does the session's channel nonce
  }
  refreshEnvironment(now);
  if (_conn_sig) {   // a link changed state: the connectivity stream sends an event now
    const uint64_t sig = _conn_sig();
    if (sig != _conn_last) {
      _conn_last = sig;
      _clock.scheduleNow(SID_CONNECTIVITY, now);
    }
  }
  SubSet* sets[LINKS + 1];
  for (int i = 0; i < LINKS; i++) sets[i] = &_links[i].subs;
  sets[LINKS] = &_mqtt_subs;
  const uint32_t due = _clock.due(sets, LINKS + 1, now);
  drainLogs();
  if (!due && !_boot_pending) return;
  const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
  const bool ts_known = ts >= 1704067200;   // 2024-01-01: an RTC that has never been set reads far earlier
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    if (s == SID_INVENTORY) { startInventory(now); continue; }   // paged: inventoryTick sends it
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[800];   // an event of up to 640 bytes, and its signature
    int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, ts_known, ts, data);
    if (n > 0) n = _prov.sign(STREAMS[s].name, ev, (size_t)n, sizeof(ev));   // -1: not signable, so not sent
    if (n <= 0) continue;
    sendEvent(s, ev, (size_t)n);
  }
  if (_boot_pending) {   // once per epoch, right after the first runtime snapshot
    _boot_pending = false;
    bool any = _mqtt_subs.s[SID_RUNTIME].active;
    for (Link& l : _links) any = any || l.subs.s[SID_RUNTIME].active;
    if (any) {
      _boot_sent = true;
      char data[384];
      JsonWriter w(data, sizeof(data));
      w.beginObject();
      writeBoot(w, board, "companion", _mesh->hardwareName());
      w.endObject();
      char ev[512];
      const int n = w.ok() ? writeEvent(ev, sizeof(ev), STREAMS[SID_RUNTIME].name, ++_clock.seq[SID_RUNTIME], _epoch, ts_known, ts, data) : -1;
      if (n > 0) sendEvent(SID_RUNTIME, ev, (size_t)n);
    }
  }
}

// One built event to every transport subscribed to its stream. A full queue drops it on that transport only.
void MeconIface::sendEvent(int s, const char* ev, size_t n) {
  for (Link& l : _links) {
    if (!l.subs.s[s].active || !ensureQueue(l)) continue;
    l.q->push(OutQueue::EVENT, reinterpret_cast<const uint8_t*>(ev), n);
  }
  if (_mqtt_subs.s[s].active && _ev_publish) _ev_publish(STREAMS[s].name, ev, n);
}

// The audit entries queued since the last pass, as logs events (their own early return in streamTick: there is no
// periodic clock for them).
void MeconIface::drainLogs() {
  bool any = _mqtt_subs.s[SID_LOGS].active;
  for (Link& l : _links) any = any || l.subs.s[SID_LOGS].active;
  if (!any) {
    _logs.skip(Posture::audit());   // what is recorded while nobody listens is history
    return;
  }
  Posture::AuditEntry a[Posture::AuditRing::CAPACITY];
  uint32_t dropped = 0;
  const int k = _logs.poll(millis(), Posture::audit(), a, &dropped);
  if (!k) return;
  const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
  for (int j = 0; j < k; j++) {
    char ev[320];
    const int n = writeLog(ev, sizeof(ev), _clock, _epoch, ts >= 1704067200, ts, a[j], j == 0 ? dropped : 0);
    if (n > 0) sendEvent(SID_LOGS, ev, (size_t)n);
  }
}

void MeconIface::startInventory(uint32_t now) {
  if (_inv_active) { _inv_again = true; return; }   // finish this one, then send another: never restarted, so no caller can
                                                    // keep a Backend from ever completing a snapshot
  _inv_active = true;
  _inv_snapshot++;
  _inv_part = 0;
  _inv_channel = 0;
  _inv_cursor = 0;
  _inv_next = now;
}

void MeconIface::inventoryTick(uint32_t now) {
  if (!_inv_active || (int32_t)(now - _inv_next) < 0) return;
  bool any = _mqtt_subs.s[SID_INVENTORY].active;
  for (Link& l : _links) {
    if (!l.subs.s[SID_INVENTORY].active) continue;
    any = true;
    if (l.q && !l.q->eventFits(INV_EVENT)) { _inv_next = now + INV_PAGE_MS; return; }   // wait until the page fits
  }
  if (!any) { _inv_active = false; _inv_again = false; return; }
  char* data = static_cast<char*>(malloc(INV_DATA));
  char* ev = static_cast<char*>(malloc(INV_EVENT));
  if (!data || !ev) { free(data); free(ev); _inv_next = now + 1000; return; }   // no heap now: try again in a second
  int channels = 0, ch_cap = 0;
#ifdef MAX_GROUP_CHANNELS
  ch_cap = MAX_GROUP_CHANNELS;
  {
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (_mesh->getChannel(i, ch) && ch.name[0]) channels++;
    Iface::wipe(&ch, sizeof(ch));   // the copy holds a channel's secret
  }
#endif
  InventoryPage page(data, INV_DATA);
  bool ok = page.begin(_inv_snapshot, ++_inv_part, _mesh->getNumContacts(), _mesh->getContactCapacity(), channels, ch_cap);
  char entry[400];
  bool full = false, done = false;
#ifdef MAX_GROUP_CHANNELS
  for (; ok && _inv_channel < MAX_GROUP_CHANNELS; _inv_channel++) {
    ChannelDetails ch;
    const bool got = _mesh->getChannel(_inv_channel, ch);
    char name[sizeof(ch.name) + 1];
    memcpy(name, ch.name, sizeof(ch.name)); name[sizeof(ch.name)] = 0;
    Iface::wipe(&ch, sizeof(ch));   // the copy holds the channel's secret; only the name is used below
    if (!got || !name[0]) continue;
    JsonWriter w(entry, sizeof(entry));
    writeChannelEntry(w, _inv_channel, name);   // the name only: never the channel's secret
    if (!w.ok()) continue;
    if (!page.add(entry, w.size())) { full = true; break; }   // this channel opens the next page
  }
#endif
  while (ok && !full) {
    const int before = _inv_cursor;
    ContactInfo c;
    if (!_mesh->contactStore().next(_inv_cursor, c)) { done = true; break; }
    char name[sizeof(c.name) + 1];
    memcpy(name, c.name, sizeof(c.name)); name[sizeof(c.name)] = 0;
    ContactEntry e{};
    memcpy(e.key, c.id.pub_key, 32);
    e.name = name;
    e.type = c.type;
    e.fav = c.isFav();
    e.last_advert = c.last_advert_timestamp;
    e.hops = c.out_path_len == OUT_PATH_UNKNOWN ? -1 : (int16_t)(c.out_path_len & 63);
    e.has_pos = c.gps_lat != 0 || c.gps_lon != 0;
    e.lat_e6 = c.gps_lat;
    e.lon_e6 = c.gps_lon;
    Iface::wipe(&c, sizeof(c));   // the copy may hold a cached shared secret
    JsonWriter w(entry, sizeof(entry));
    writeContactEntry(w, e);   // no shared secret, no path bytes
    if (!w.ok()) continue;
    if (!page.add(entry, w.size())) {
      if (page.count() == 0) continue;   // cannot fit even an empty page: skipped rather than stall the snapshot
      _inv_cursor = before;              // this contact opens the next page
      full = true;
    }
  }
  const size_t dn = ok ? page.finish(done) : 0;
  if (dn) {
    const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
    int n = writeEvent(ev, INV_EVENT, STREAMS[SID_INVENTORY].name, ++_clock.seq[SID_INVENTORY], _epoch, ts >= 1704067200,
                       ts, data);
    if (n > 0) n = _prov.sign(STREAMS[SID_INVENTORY].name, ev, (size_t)n, INV_EVENT);
    if (n > 0) sendEvent(SID_INVENTORY, ev, (size_t)n);   // a page that is not sent is not retried: the next snapshot is
  }
  if (done || !ok || !dn) {
    _inv_active = false;
    if (_inv_again) { _inv_again = false; startInventory(now + INV_PAGE_MS); }
  }
  _inv_next = now + INV_PAGE_MS;
  free(data);
  free(ev);
}

void MeconIface::loop(uint32_t now) {
  _gap.tick(now);
  _jobs.tick(now);
  if (_reboot_at && (int32_t)(now - _reboot_at) >= 0) board.reboot();
  streamTick(now);
  inventoryTick(now);
  for (Link& l : _links) {
    if (!l.src) continue;
    if (!l.src->isConnected()) {   // the link dropped: its nonce, its challenge and its unlock go with it
      l.nonce[0] = 0;
      l.unlock.clear();
      l.subs.clear();              // and its subscriptions and queued messages: the next client on it inherits nothing
      if (l.q) l.q->clear();
      l.may_read = false;
    }
    if (l.rx.tick(now)) {
      uint8_t xid;
      l.rx.takeDiscard(&xid);
      sendSmall(l, ST_INVALID, "incomplete");
    }
    if (!l.q || !l.q->count()) continue;
    uint8_t f[FRAME_MAX];
    if (!_mesh->ifaceWritable(l.src)) continue;
    size_t n = l.q->nextFrame(f);
    if (n) _mesh->ifaceWrite(l.src, f, n);
  }
}

#endif  // ENABLE_MECON_IFACE
