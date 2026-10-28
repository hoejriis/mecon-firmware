#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <helpers/MeconVersion.h>
#include <helpers/MemStats.h>
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
                                    "deployment.unlock_challenge", "deployment.unlock"};

Status opHello(OpContext& c);
Status opStatus(OpContext& c);
Status opSchema(OpContext& c);
Status opGet(OpContext& c);
Status opApply(OpContext& c);
Status opSubStart(OpContext& c);
Status opSubStop(OpContext& c);

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

}  // namespace

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
  // DAK-signed authority update (it carries its own proof; AUTHORITY.md sections 6.2 and 6.3).
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
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
  d.setDenyReason(denyReason);
  return d.handle(msg, len, out, cap, cache);
}

int MeconIface::dispatch(const Cli::Context& ctx, uint8_t caller, Link* link, const char* msg, size_t len, char* out,
                         size_t cap) {
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

void MeconIface::begin(MyMesh* mesh, const uint8_t device_id[16], uint32_t epoch) {
  _mesh = mesh;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
  deployLoad();
  observeSink().fn = onObserve;
  observeSink().user = this;
}

// Whether the caller of the request being handled may use a stream of this class: the CLI privilege table with its
// real context, as for the CLI line the data would come from.
bool MeconIface::streamAllow(void* user, uint8_t cls) { return Cli::decide(static_cast<MeconIface*>(user)->_ctx, cls).allow; }

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
  const int n = writeObservation(ev, RESULT_MAX, m->_clock, m->_epoch, ts >= 1704067200, ts, snr, rssi, raw, len,
                                 m->_obs.dropped);
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
  char busy[96];   // fits what events must leave free (RESULT_RESERVE)
  int n = writeResult(busy, sizeof(busy), "", ST_BUSY, nullptr, "queue-full");
  return n > 0 && l.q->push(OutQueue::RESULT, reinterpret_cast<const uint8_t*>(busy), (size_t)n) == OutQueue::PUSH_OK;
}

void MeconIface::onFrame(const BaseSerialInterface* src, const uint8_t* frame, size_t len, uint32_t now) {
  Link* l = linkFor(src, true);
  if (!l) return;
  RxResult r = l->rx.feed(frame, len, now);
  uint8_t xid;
  if (l->rx.takeDiscard(&xid)) {
    char msg[96];
    int n = writeResult(msg, sizeof(msg), "", ST_INVALID, nullptr, "incomplete");
    if (n > 0) queueResult(*l, msg, (size_t)n);
  }
  if (r == RX_NOMEM) {
    char msg[96];
    int n = writeResult(msg, sizeof(msg), "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) queueResult(*l, msg, (size_t)n);
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
    char msg[96];
    int n = writeResult(msg, sizeof(msg), "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) queueResult(*l, msg, (size_t)n);
  }
  free(out);
  l->rx.release();
}

// Whether the environment stream has anything to report: rechecked once a minute (sensors start after the mesh), and
// before the first request is answered (a request can arrive before the first loop pass).
void MeconIface::refreshEnvironment(uint32_t now) {
  if (_env_checked && now - _env_at < 60000) return;
  _env_ok = countEnvironment(sensors, board) > 0;
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
    case SID_INVENTORY: {
      writeCount(w, "contacts", _mesh->getNumContacts(), _mesh->getContactCapacity());
      int channels = 0, capacity = 0;
#ifdef MAX_GROUP_CHANNELS
      capacity = MAX_GROUP_CHANNELS;
      ChannelDetails ch;
      for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
        if (_mesh->getChannel(i, ch) && ch.name[0]) channels++;
#endif
      writeCount(w, "channels", channels, capacity);
      break;
    }
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
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[640];
    const int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, ts_known, ts, data);
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

void MeconIface::loop(uint32_t now) {
  _gap.tick(now);
  streamTick(now);
  for (Link& l : _links) {
    if (!l.src) continue;
    if (!l.src->isConnected()) {   // the link dropped: its nonce, its challenge and its unlock go with it
      l.nonce[0] = 0;
      l.unlock.clear();
    }
    if (l.rx.tick(now)) {
      uint8_t xid;
      l.rx.takeDiscard(&xid);
      char msg[96];
      int n = writeResult(msg, sizeof(msg), "", ST_INVALID, nullptr, "incomplete");
      if (n > 0) queueResult(l, msg, (size_t)n);
    }
    if (!l.q || !l.q->count()) continue;
    uint8_t f[FRAME_MAX];
    if (!_mesh->ifaceWritable(l.src)) continue;
    size_t n = l.q->nextFrame(f);
    if (n) _mesh->ifaceWrite(l.src, f, n);
  }
}

#endif  // ENABLE_MECON_IFACE
