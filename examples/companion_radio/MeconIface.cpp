#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <helpers/MeconVersion.h>
#include <helpers/MemStats.h>
#include <helpers/Posture.h>
#include <helpers/cli/CliPolicy.h>
#include <helpers/cli/CliRegistry.h>
#include <helpers/iface/IfaceConfig.h>
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

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply"};

Status opHello(OpContext& c);
Status opStatus(OpContext& c);
Status opSchema(OpContext& c);
Status opGet(OpContext& c);
Status opApply(OpContext& c);

// The operation's class is the least it needs; each config key is decided again by its own CLI line (IfaceConfig).
const OpEntry OPS[] = {
  {"hello", opHello, PRIV_READ, false},
  {"status.get", opStatus, PRIV_READ, false},
  {"config.schema", opSchema, PRIV_READ, false},
  {"config.get", opGet, PRIV_READ, false},
  {"config.apply", opApply, PRIV_WRITE, true},
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
  w.key("capabilities"); w.beginArray();
  for (const char* cap : CAPABILITIES) w.str(cap);
  w.endArray();
  return ST_OK;
}

Status opStatus(OpContext& c) {
  MeconIface* m = self(c);
  MyMesh* mesh = m->mesh();
  JsonWriter& w = *c.out;
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("epoch"); w.unum(m->epoch());
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
#ifdef ENABLE_CONNMODE
  w.key("connectivity"); w.str(mesh->getConnectivityMode() == ConnMode::WIFI ? "wifi" : "ble");
#endif
  w.key("posture"); w.str(Posture::isHardened(mesh->postureValue()) ? "hardened" : "normal");
  return ST_OK;
}

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
  const uint8_t cls = op.priv == PRIV_READ ? Cli::C_READ : op.priv == PRIV_WRITE ? Cli::C_WRITE : Cli::C_POSTURE;
  return Cli::decide(self->_ctx, cls).allow;
}

int MeconIface::dispatch(const Cli::Context& ctx, uint8_t caller, const char* msg, size_t len, char* out, size_t cap) {
  _ctx = ctx;
  _caller = caller;
  if (!_cache) _cache = new (std::nothrow) ResultCache();   // 3.1 KB, only once the interface is used
  if (!_cache) return writeResult(out, cap, "", ST_BUSY, nullptr, "no-memory");
  Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
  return d.handle(msg, len, out, cap, _cache);
}

size_t MeconIface::onMqtt(const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap) {
  const int n = dispatch(ctx, Posture::CALLER_MQTT, reinterpret_cast<const char*>(msg), len, out, cap);
  return n > 0 ? (size_t)n : 0;
}

void MeconIface::begin(MyMesh* mesh, const uint8_t device_id[16], uint32_t epoch) {
  _mesh = mesh;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
}

MeconIface::Link* MeconIface::linkFor(const BaseSerialInterface* src, bool create) {
  for (Link& l : _links) if (l.src == src && src) return &l;
  if (!create) return nullptr;
  for (Link& l : _links) if (!l.src) { l.src = src; return &l; }
  return nullptr;
}

bool MeconIface::queueResult(Link& l, const char* msg, size_t len) {
  if (!l.q) {
    uint8_t* store = static_cast<uint8_t*>(malloc(QUEUE_BYTES));
    if (!store) return false;
    l.q = new (std::nothrow) OutQueue(store, QUEUE_BYTES);
    if (!l.q) { free(store); return false; }
  }
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
    int n = dispatch(ctx, caller, reinterpret_cast<const char*>(l->rx.data()), l->rx.size(), out, RESULT_MAX + 1);
    if (n > 0) queueResult(*l, out, (size_t)n);
  } else {
    char msg[96];
    int n = writeResult(msg, sizeof(msg), "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) queueResult(*l, msg, (size_t)n);
  }
  free(out);
  l->rx.release();
}

void MeconIface::loop(uint32_t now) {
  for (Link& l : _links) {
    if (!l.src) continue;
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
