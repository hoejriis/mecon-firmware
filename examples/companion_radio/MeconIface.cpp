#include "MeconIface.h"
#include <helpers/BleStart.h>
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
#include <helpers/cli/MeconCli.h>
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

// Where hello stops adding capabilities, as the size of the result so far: the result limit (1,536 bytes), less the
// signature (99), the op and counters it adds (about 60), the closing of the array and result and a `next` member.
const size_t HELLO_SIGNED_BUDGET = 1350;
const size_t HELLO_PLAIN_BUDGET = 1500;

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply",
                                    "sub.start", "sub.stop", "posture.set", "rf.request", "report.now", "standing.put", "standing.list", "standing.remove", "deployment.get", "deployment.provision",
                                    "deployment.authority_update", "deployment.reset", "deployment.set_unlock",
                                    "deployment.unlock_challenge", "deployment.unlock", "identity.rotate", "identity.rotation",
                                    "job.get", "job.list", "job.ack", "job.cancel", "job.confirm",
                                    "update.manifest", "update.status", "update.offer", "update.chunk",
                                    "contact.list", "contact.get", "contact.put", "contact.remove", "contact.protect",
                                    "message.send", "message.get", "message.list", "message.ack",
                                    "inbox.list", "inbox.ack", "message.deliver"
#ifdef MAX_GROUP_CHANNELS
                                    , "channel.list", "channel.get", "channel.put", "channel.remove",
                                    "mgmt.channel.get", "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind"
#endif
                                    };

Status opHello(OpContext& c);
Status opStatus(OpContext& c);
Status opMsgSend(OpContext& c);
Status opMsgGet(OpContext& c);
Status opMsgList(OpContext& c);
Status opMsgAck(OpContext& c);
Status opInbList(OpContext& c);
Status opInbAck(OpContext& c);
Status opMsgDeliver(OpContext& c);
Status opSchema(OpContext& c);
Status opGet(OpContext& c);
Status opApply(OpContext& c);
Status opRfRequest(OpContext& c);
Status opReportNow(OpContext& c);
Status opStandingPut(OpContext& c);
Status opStandingList(OpContext& c);
Status opStandingRemove(OpContext& c);
Status opPostureW(OpContext& c);
Status opSubStart(OpContext& c);
Status opSubStop(OpContext& c);
Status opJobGet(OpContext& c) { return self(c)->jobs().opGet(c); }
Status opJobList(OpContext& c) { return self(c)->jobs().opList(c); }
Status opJobAck(OpContext& c) { return self(c)->jobs().opAck(c); }
Status opJobCancel(OpContext& c) { return self(c)->jobs().opCancel(c); }
Status opJobConfirm(OpContext& c) { return self(c)->jobs().opConfirm(c); }
Status opUpdManifest(OpContext& c) { return self(c)->updates().opManifest(c, millis()); }
Status opUpdStatus(OpContext& c) { return self(c)->updates().opStatus(c); }
Status opUpdOffer(OpContext& c) { return self(c)->updates().opOffer(c, millis()); }
Status opUpdChunk(OpContext& c) { return self(c)->updates().opChunk(c, millis()); }
Status opContList(OpContext& c);
Status opContGet(OpContext& c);
Status opContPut(OpContext& c);
Status opContRemove(OpContext& c);
Status opContProtect(OpContext& c);
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
  {"rf.request", opRfRequest, PRIV_WRITE, false},
  {"report.now", opReportNow, PRIV_WRITE, false},
  {"standing.put", opStandingPut, PRIV_WRITE, true},
  {"standing.list", opStandingList, PRIV_READ, false},
  {"standing.remove", opStandingRemove, PRIV_WRITE, true},
  {"posture.set", opPostureW, PRIV_WRITE, true},
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
  // The offer is of the update class and keeps its result: a retry gets the same job and transfer token back. A chunk
  // is authorised by that token (DeployAuth::preAuthorize lets it through to the handler) and is not kept: its answer
  // is the offset expected next, which a retry asks again.
  {"update.offer", opUpdOffer, PRIV_UPDATE, true},
  {"update.chunk", opUpdChunk, PRIV_READ, false},
#ifdef MAX_GROUP_CHANNELS
  // The native contact table (OPERATIONS.md section 5.1): no shared secret in a read, no grant in a write.
  {"contact.list", opContList, PRIV_READ, false},
  {"contact.get", opContGet, PRIV_READ, false},
  {"contact.put", opContPut, PRIV_WRITE, true},
  {"contact.remove", opContRemove, PRIV_WRITE, true},
  {"contact.protect", opContProtect, PRIV_WRITE, true},
  // Native messages (OPERATIONS.md section 6): a read of the outcomes shows who a message went to, so it is private.
  {"message.send", opMsgSend, PRIV_WRITE, true},
  {"message.get", opMsgGet, PRIV_READ, false},
  {"message.list", opMsgList, PRIV_READ, false},
  {"message.ack", opMsgAck, PRIV_READ, false},
  // Received messages (section 6.4): their text and sender are private, and neither read touches the native queue.
  {"inbox.list", opInbList, PRIV_READ, false},
  {"inbox.ack", opInbAck, PRIV_READ, false},
  // A write, and kept by id: a retry whose reply was lost is the same delivery (section 6.5).
  {"message.deliver", opMsgDeliver, PRIV_WRITE, true},
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
  m->updates().writeAttestation(w);   // the image of an update job, while it awaits confirmation (UPDATES.md section 7)
  // The channel nonce of the link this reply goes to, only once a Deployment record exists (AUTHORITY.md section 7.1).
  const char* nonce = m->helloNonce();
  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }
  // The list is paged (`cursor` in, `next` out, as DEVICE_INTERFACE.md says) once it would not leave room for the
  // signature: an enrolled device signs the result, and a nonce adds a second device-id signature, which together
  // outgrew the 1,536-byte result on a build with every stream. Most hellos fit in one page and carry no `next`.
  int64_t from = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, 255, &from)) return ST_INVALID;
  const size_t room = m->enrolled() ? HELLO_SIGNED_BUDGET : HELLO_PLAIN_BUDGET;
  unsigned idx = 0;
  int next = -1;
  const bool lora = m->onLora();
  auto add = [&](const char* cap) {
    if (lora && !loraOpAllowed(cap)) return;   // an operation or stream this link does not carry is not offered on it
    if (idx++ < (unsigned)from || next >= 0) return;
    if (w.size() + strlen(cap) + 3 > room) { next = (int)idx - 1; return; }
    w.str(cap);
  };
  w.key("capabilities"); w.beginArray();
  // hello and the streams first, so a manager reading only the first page still has what it subscribes to.
  add(CAPABILITIES[0]);
  const size_t hello_first = 1;
  const uint32_t streams = m->streamsProduced();
  for (int sid = 0; sid < STREAM_COUNT; sid++) {
    if (!(streams & (1u << sid))) continue;
    char cap[24];
    snprintf(cap, sizeof(cap), "stream.%s", STREAMS[sid].name);
    add(cap);
  }
  for (size_t i = hello_first; i < sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]); i++) add(CAPABILITIES[i]);
  w.endArray();
  if (next >= 0) { w.key("next"); w.num(next); }
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
  if (BleStart::state() != BleStart::NOT_TRIED) { w.key("ble_started"); w.boolean(BleStart::state() == BleStart::STARTED); }
  w.key("posture"); w.str(Posture::isHardened(mesh->postureValue()) ? "hardened" : "normal");
  m->writeDeploymentStatus(w, Posture::isHardened(mesh->postureValue()));
  m->jobs().writeStatus(w);
  m->lora().writeCounts(w);
  w.key("messages"); w.beginObject();   // a manager that reconnects learns there are outcomes to collect
  w.key("open"); w.num(m->messages() ? m->messages()->open() : 0);
  w.key("owed"); w.num(m->messages() ? m->messages()->owed() : 0);
  w.endObject();
  w.key("inbox"); w.beginObject();      // the same for what was received, counted even while no slots exist
  w.key("received"); w.unum(m->inbox().received());
  w.key("unread"); w.num(m->inbox().unread());
  w.key("lost"); w.unum(m->inbox().lost());
  w.endObject();
  m->updates().writeAttestation(w);
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
Status opRfRequest(OpContext& c) { return self(c)->rfRequest(c); }
Status opReportNow(OpContext& c) { return self(c)->standing().opNow(c, millis()); }
Status opStandingPut(OpContext& c) { return self(c)->standing().opPut(c, millis()); }
Status opStandingList(OpContext& c) { return self(c)->standing().opList(c); }
Status opStandingRemove(OpContext& c) { return self(c)->standing().opRemove(c); }
Status opPostureW(OpContext& c) {
  MeconIface* m = self(c);
  const uint8_t before = m->postureNow();
  const Status st = withConfig(c, opPostureSet);
  if (st == ST_OK && m->postureNow() != before) m->rebootSoon();   // what starts in each posture is decided at boot
  return st;
}

Status withChannels(OpContext& c, Status (*op)(OpContext&)) {
  ChannelHost host = self(c)->channelHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
// A list of contacts or channels is private data, as the inventory stream that carries the same entries (TELEMETRY.md):
// the op runs at read, as sub.start does, so an enrolled device's plain-read rule still applies, and the private class
// is then decided here, for the caller and for the link the answer goes out on.
__attribute__((noinline)) bool mayReadPrivate(OpContext& c) {
  if (MeconIface::streamAllow(self(c), Cli::C_PRIVATE)) return true;
  c.error = "private";
  return false;
}

Status withContacts(OpContext& c, Status (*op)(OpContext&)) {
  ContactHost host = self(c)->contactHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opContList(OpContext& c) { return mayReadPrivate(c) ? withContacts(c, opContactList) : ST_REFUSED; }
Status opContGet(OpContext& c) { return mayReadPrivate(c) ? withContacts(c, opContactGet) : ST_REFUSED; }
Status opContPut(OpContext& c) { return withContacts(c, opContactPut); }
Status opContRemove(OpContext& c) { return withContacts(c, opContactRemove); }
Status opContProtect(OpContext& c) { return withContacts(c, opContactProtect); }

__attribute__((noinline)) Status withMessages(OpContext& c, Status (*op)(OpContext&), bool create) {
  MessageHost host = self(c)->messageHost(create);
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opMsgSend(OpContext& c) { return withMessages(c, opMessageSend, true); }
Status opMsgGet(OpContext& c) { return mayReadPrivate(c) ? withMessages(c, opMessageGet, false) : ST_REFUSED; }
Status opMsgList(OpContext& c) { return mayReadPrivate(c) ? withMessages(c, opMessageList, false) : ST_REFUSED; }
Status opMsgAck(OpContext& c) { return mayReadPrivate(c) ? withMessages(c, opMessageAck, false) : ST_REFUSED; }

__attribute__((noinline)) Status withInbox(OpContext& c, Status (*op)(OpContext&)) {
  if (!mayReadPrivate(c)) return ST_REFUSED;
  InboxHost host = self(c)->inboxHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opInbList(OpContext& c) { return withInbox(c, opInboxList); }
Status opInbAck(OpContext& c) { return withInbox(c, opInboxAck); }
Status opMsgDeliver(OpContext& c) {
  InboxHost host = self(c)->inboxHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = opMessageDeliver(cc);
  c.error = cc.error;
  return st;
}

Status opChanList(OpContext& c) { return mayReadPrivate(c) ? withChannels(c, opChannelList) : ST_REFUSED; }
Status opChanGet(OpContext& c) { return mayReadPrivate(c) ? withChannels(c, opChannelGet) : ST_REFUSED; }
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

// ---- contacts: the native table

static MessageSend msgDirect(void* user, const uint8_t key[32], uint32_t ts, uint8_t attempt, const char* text, MessageSent& out) {
  return static_cast<MeconIface*>(user)->mesh()->messageSendDirect(key, ts, attempt, text, out);
}
static MessageSend msgChannel(void* user, int slot, uint32_t ts, const char* text, size_t len, MessageSent& out) {
  return static_cast<MeconIface*>(user)->mesh()->messageSendChannel(slot, ts, text, len, out);
}
static uint32_t msgClock(void* user) { return static_cast<MeconIface*>(user)->mesh()->getRTCClock()->getCurrentTimeUnique(); }

static DeliverResult msgDeliver(void* user, const uint8_t* from, int channel, uint32_t ts, const char* text, size_t len) {
  return static_cast<MeconIface*>(user)->mesh()->messageDeliver(from, channel, ts, text, len);
}

InboxHost MeconIface::inboxHost() {
#ifdef MAX_GROUP_CHANNELS
  const bool channels = true;
#else
  const bool channels = false;
#endif
  return InboxHost{&_inbox, _epoch, (uint32_t)millis(), msgDeliver, channels, this};
}

MessageHost MeconIface::messageHost(bool create) {
  if (create && !_messages) _messages = new (std::nothrow) MessageTable();
#ifdef MAX_GROUP_CHANNELS
  const bool channels = true;
#else
  const bool channels = false;
#endif
  return MessageHost{_messages, MAX_TEXT_LEN, channels, msgDirect, msgChannel, msgClock, confirmJobId, _epoch, millis(), this};
}

// A message event goes where a job event goes: its data names no recipient and no text. One a pass, on the loop's own
// stack, since a signed event is not small; a change while this waits is told once, in its latest state.
void MeconIface::tellMessages() {
  if (!_messages) return;
  const uint32_t now = millis();
  _messages->tick(now);
  const MessageEntry* e = _messages->nextToTell();
  if (!e) return;
  char data[96];
  if (!writeMessageEventData(data, sizeof(data), *e)) return;
  tellReaders("message", ++_msg_ev_seq, data);   // if this cannot be told, the state is still in message.get and message.list
}

// An inbox event says only that something arrived, so it goes where a job event goes, and many receptions in one pass are
// told once. A device with no Deployment gets its slots on the first inbox.list, and is told nothing until then.
void MeconIface::tellInbox() {
  if (!_inbox.ready() && !_inbox_tried && _auth.enrolled()) {
    _inbox_tried = true;
    _inbox.alloc();
  }
  if (!_inbox.ready() || _inbox_told == _inbox.received()) return;
  _inbox_told = _inbox.received();
  char data[64];
  if (writeInboxEventData(data, sizeof(data), _inbox)) tellReaders("inbox", ++_inbox_ev_seq, data);
}

// One event to every link that may read and to the broker, on the loop's own stack, since a signed event is not small.
void MeconIface::tellReaders(const char* ev_name, uint32_t seq, const char* data) {
  char* ev = static_cast<char*>(malloc(RESULT_MAX));
  if (!ev) return;
  const uint32_t ts = _mesh->getRTCClock()->knownTime();
  int n = writeEvent(ev, RESULT_MAX, ev_name, seq, _epoch, ts != 0, ts, data);
  if (n > 0) n = _prov.sign(ev_name, ev, (size_t)n, RESULT_MAX);
  if (n > 0) {
    for (Link& l : _links) {
      if (!l.src || !l.may_read || !l.src->isConnected() || !ensureQueue(l)) continue;
      l.q->push(OutQueue::EVENT, reinterpret_cast<const uint8_t*>(ev), (size_t)n);
    }
    if (_mqtt_may_read && _ev_publish && _ev_up && _ev_up()) _ev_publish(ev_name, ev, (size_t)n);
  }
  free(ev);
}

ContactHost MeconIface::contactHost() {
  return ContactHost{_mesh->getContactCapacity(), _mesh->getNumContacts(), contGet, contNext, contPut, contRemove,
                     contProtect, this};
}

static void toRec(const ContactInfo& c, ContactRec& out) {
  memcpy(out.key, c.id.pub_key, 32);
  memcpy(out.name, c.name, CONTACT_NAME_MAX);
  out.name[CONTACT_NAME_MAX] = 0;
  out.type = c.type;
  out.fav = c.isFav();
  out.last_advert = c.last_advert_timestamp;
  out.hops = c.out_path_len == OUT_PATH_UNKNOWN ? -1 : (int16_t)(c.out_path_len & 63);
  out.has_pos = c.gps_lat != 0 || c.gps_lon != 0;
  out.lat_e6 = c.gps_lat;
  out.lon_e6 = c.gps_lon;
}

bool MeconIface::contGet(void* user, const uint8_t* key, ContactRec& out) {
  const ContactInfo* c = static_cast<MeconIface*>(user)->_mesh->contactStore().find(key, PUB_KEY_SIZE);   // stored contacts only
  if (!c) return false;
  toRec(*c, out);
  return true;
}

bool MeconIface::contNext(void* user, int& cursor, ContactRec& out) {
  ContactInfo c;
  if (!static_cast<MeconIface*>(user)->_mesh->contactStore().next(cursor, c)) return false;
  toRec(c, out);
  wipe(&c, sizeof(c));   // the copy may hold a cached shared secret
  return true;
}

// As CMD_ADD_UPDATE_CONTACT: an update rewrites the named fields in place, a new contact goes through addContact (so a
// full table evicts or refuses exactly as for the app), and the lazy save is scheduled. Flags and path are the contact's
// own: a put never grants telemetry or the remote CLI.
ContactWrite MeconIface::contPut(void* user, const ContactRec& in) {
  MyMesh* mesh = static_cast<MeconIface*>(user)->_mesh;
  ContactInfo* have = mesh->contactStore().find(in.key, PUB_KEY_SIZE);
  const uint32_t now = mesh->getRTCClock()->getCurrentTime();
  if (have) {
    memset(have->name, 0, sizeof(have->name));
    memcpy(have->name, in.name, strlen(in.name));
    have->type = in.type;
    have->gps_lat = in.has_pos ? in.lat_e6 : 0;
    have->gps_lon = in.has_pos ? in.lon_e6 : 0;
    have->lastmod = now;
    mesh->markContactsDirty();
    return CW_OK;
  }
  ContactInfo fresh;
  memset(&fresh, 0, sizeof(fresh));
  memcpy(fresh.id.pub_key, in.key, PUB_KEY_SIZE);
  memcpy(fresh.name, in.name, strlen(in.name));
  fresh.type = in.type;
  fresh.out_path_len = OUT_PATH_UNKNOWN;
  fresh.gps_lat = in.has_pos ? in.lat_e6 : 0;
  fresh.gps_lon = in.has_pos ? in.lon_e6 : 0;
  fresh.lastmod = now;
  const bool added = mesh->addContact(fresh);
  wipe(&fresh, sizeof(fresh));
  if (!added) return CW_FULL;
  mesh->markContactsDirty();
  return CW_OK;
}

int MeconIface::contRemove(void* user, const uint8_t* key) {
  MyMesh* mesh = static_cast<MeconIface*>(user)->_mesh;
  ContactInfo* have = mesh->contactStore().find(key, PUB_KEY_SIZE);
  if (!have || !mesh->removeContact(*have)) return 0;
  mesh->deleteContactBlob(key);
  mesh->markContactsDirty();
  return 1;
}

int MeconIface::contProtect(void* user, const uint8_t* key, bool on) {
  MyMesh* mesh = static_cast<MeconIface*>(user)->_mesh;
  ContactInfo* have = mesh->contactStore().find(key, PUB_KEY_SIZE);
  if (!have) return -1;
  if (have->isFav() == on) return 0;
  have->flags = on ? (uint8_t)(have->flags | 0x01) : (uint8_t)(have->flags & ~0x01);
  mesh->markContactsDirty();
  return 1;
}

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
  const uint8_t cls = op.priv == PRIV_READ    ? Cli::C_READ
                      : op.priv == PRIV_WRITE  ? Cli::C_WRITE
                      : op.priv == PRIV_UPDATE ? Cli::C_UPDATE
                                               : Cli::C_POSTURE;
  // A refusal says why, in the CLI's words (broker-not-verified, untrusted-transport, enrolled-unsigned, ...): a bare
  // "refused" cannot be told from any other.
  const Cli::Verdict v = Cli::decide(self->_ctx, cls);
  if (!v.allow) self->_deny = Cli::reasonName(v.reason);
  return v.allow;
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
  if (_on_lora) d.setOpFilter(_lora_channel ? loraChannelOpAllowed : loraOpAllowed);
  _lora_bound_dirty = true;   // cheaper than asking which operations touch the binding: requests are rare beside packets
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
  if (NrfWorker::run(dispatchJob, &j, 15000)) {
#ifdef ENABLE_CONNMODE
    // The reply is written and goes out on this pass: a restart the request asked for (connectivity.mode) counts its
    // 1.5 s from here, not from the line inside the request. A nine-key config.apply took 5.6 s on a Heltec V3.
    _mesh->restartConnectivityClock();
#endif
    _updates.replyWritten(millis());   // the reply goes out on this pass: a DFU reset the request asked for waits for it
    return j.n;
  }
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
  _updates.begin(_mesh->hardwareName(), "companion", _mesh->hardwareNames());
  observeSink().fn = onObserve;
  observeSink().user = this;
  loraSink().fn = onLoraFrame;
  loraSink().user = this;
}

// Whether the caller of the request being handled may use a stream of this class: the CLI privilege table with its
// real context, as for the CLI line the data would come from.
// A stream beyond read (logs, the private streams) also needs the link that will carry it to be allowed it by itself: a
// signature authorises the request, not the wire, so a signed sub.start on the open TCP port or a default-PIN BLE link
// does not send positions or contacts over that link.
bool MeconIface::streamAllow(void* user, uint8_t cls) {
  MeconIface* m = static_cast<MeconIface*>(user);
  if (!Cli::decide(m->_ctx, cls).allow) return false;
  if (cls == Cli::C_READ || Cli::decide(m->_link_ctx, cls).allow) return true;
  // The logs (class posture) to a subscriber a Deployment member signed for, over a link that may carry private data:
  // the owner's decision of 2026-11-01. A plain broker login never gets this far, it fails the first check.
  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED && Cli::decide(m->_link_ctx, Cli::C_PRIVATE).allow;
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
  const uint32_t ts = m->_mesh->getRTCClock()->knownTime();
  int n = writeObservation(ev, RESULT_MAX, m->_clock, m->_epoch, ts != 0, ts, snr, rssi, raw, len,
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

// One lost byte throws the frame reader out of step, and each frame it then misreads looks like a new message, dropping the
// one before. Answered once a second: otherwise one lost request reads to the client as many.
void MeconIface::sendIncomplete(Link& l, uint32_t now) {
  if (l.incomplete_sent && (uint32_t)(now - l.incomplete_ms) < INCOMPLETE_QUIET_MS) return;
  l.incomplete_sent = true;
  l.incomplete_ms = now;
  sendSmall(l, ST_INVALID, "incomplete");
}

void MeconIface::onFrame(const BaseSerialInterface* src, const uint8_t* frame, size_t len, uint32_t now) {
  Link* l = linkFor(src, true);
  if (!l) return;
  RxResult r = l->rx.feed(frame, len, now);
  uint8_t xid;
  if (l->rx.takeDiscard(&xid)) sendIncomplete(*l, now);
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
    case SID_SCREEN: return false;      // in parts: streamTick hands it to the feed, and screenTick sends them
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
  if (!due && !_boot_pending) return;
  const uint32_t ts = _mesh->getRTCClock()->knownTime();
  const bool ts_known = ts != 0;
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    if (s == SID_INVENTORY) { startInventory(now); continue; }   // paged: inventoryTick sends it
    if (s == SID_SCREEN) { _screen.start(now); continue; }       // in parts: screenTick sends it
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
      // static: the loop task's stack has no room for these next to the signer (the stack gate, #234); loop only
      static char data[384];
      JsonWriter w(data, sizeof(data));
      w.beginObject();
      writeBoot(w, board, "companion", _mesh->hardwareName());
      w.endObject();
      static char ev[640];   // the event and its signature
      int n = w.ok() ? writeEvent(ev, sizeof(ev), STREAMS[SID_RUNTIME].name, ++_clock.seq[SID_RUNTIME], _epoch, ts_known, ts, data) : -1;
      if (n > 0) n = _prov.sign(STREAMS[SID_RUNTIME].name, ev, (size_t)n, sizeof(ev));   // signed like every event (#234)
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
  const uint32_t ts = _mesh->getRTCClock()->knownTime();
  for (int j = 0; j < k; j++) {
    static char ev[480];   // the event and its signature; static for the loop task's stack (#234), loop only
    int n = writeLog(ev, sizeof(ev), _clock, _epoch, ts != 0, ts, a[j], j == 0 ? dropped : 0);
    if (n > 0) n = _prov.sign(STREAMS[SID_LOGS].name, ev, (size_t)n, sizeof(ev));   // signed like every event (#234)
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
    const uint32_t ts = _mesh->getRTCClock()->knownTime();
    int n = writeEvent(ev, INV_EVENT, STREAMS[SID_INVENTORY].name, ++_clock.seq[SID_INVENTORY], _epoch, ts != 0,
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

void MeconIface::onLoraFrame(void* user, const uint8_t* key, const uint8_t* secret, const uint8_t* path, uint8_t path_len,
                             const uint8_t* frame, size_t n) {
  static_cast<MeconIface*>(user)->_lora.feed(key, secret, path, path_len, frame, n, LORA_REQ_PAYLOAD, millis());
}

bool MeconIface::loraSend(void* user, const LoraLink::Peer& p, const uint8_t* frame, size_t n, bool flood_ok) {
  MeconIface* m = static_cast<MeconIface*>(user);
  if (p.channel) return m->_lora_slot >= 0 && m->_mesh->loraChannelSend(m->_lora_slot, p.key, frame, n);
  return m->_mesh->loraReply(p.key, p.secret, p.path, p.path_len, frame, n, flood_ok);
}

// One complete message on this pass, then its reply a frame at a time. Over LoRa only a signed request runs (section 2).
void MeconIface::loraTick(uint32_t now) {
  if (!_lora_bound_read || _lora_bound_dirty || now - _lora_bound_at >= 60000) {   // as ConsoleIface::loraKeyRefresh
    _lora_bound_read = true;
    _lora_bound_dirty = false;
    _lora_bound_at = now;
    MgmtHost h = mgmtHost();
    MgmtRecord rec;
    _lora_bound_slot = mgmtState(h, &rec) == MGMT_BOUND ? rec.slot : -1;
  }
  if (LoraLink::Peer* p = _lora.nextComplete()) {
    char* res = static_cast<char*>(malloc(LORA_MSG_MAX + 1));
    if (res) {
      const char* msg = LoraLink::message(*p);
      const size_t len = LoraLink::messageLen(*p);
      int n;
      bool verified = false;
      if (DeployAuth::isWrapper(msg, len)) {
        _on_lora = true;
        _lora_channel = p->channel;
        const Cli::Context ctx(Cli::T_LORA, Cli::A_NONE, _mesh->postureValue());
        n = dispatch(ctx, Posture::CALLER_REMOTE_ADMIN, &_lora_link, msg, len, res, LORA_MSG_MAX + 1);
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
  _lora.tick(now, loraSend, this);
}

// A frame heard on a channel. Taken only on the bound channel while its binding is bound (MANAGEMENT_CHANNEL.md section 2), and
// only when addressed to this node; a reply to this Companion's own rf.request is taken on whichever slot it sent on.
bool MeconIface::onLoraChannel(int slot, const uint8_t* data, size_t n) {
  static const uint8_t ALL[LORA_CH_PREFIX] = {0};   // a report may name no one: whoever holds the channel may read it
  if (n > LORA_CH_ADDR && data[LORA_CH_ADDR] == REPORT_MARKER && memcmp(data, ALL, LORA_CH_PREFIX) == 0) {
    onRfReport(data + LORA_CH_PREFIX, LORA_CH_PREFIX, data + LORA_CH_ADDR, n - LORA_CH_ADDR);
    return false;
  }
  if (n <= LORA_CH_ADDR + LORA_HDR || memcmp(data, _mesh->self_id.pub_key, LORA_CH_PREFIX) != 0) return false;
  const uint8_t* sender = data + LORA_CH_PREFIX;
  const uint8_t* f = data + LORA_CH_ADDR;
  const size_t len = n - LORA_CH_ADDR;
  if (f[0] == REPORT_MARKER) { onRfReport(sender, LORA_CH_PREFIX, f, len); return true; }
  if (memcmp(f, LORA_REPLY_MAGIC, 4) == 0) {
    uint8_t key[32] = {0};
    memcpy(key, sender, LORA_CH_PREFIX);
    onRfReply(key, f, len);
    return true;
  }
  if (_lora_bound_slot != slot) return false;   // read on the main pass: a file read here would hold the radio's next packet
  _lora_slot = slot;
  _lora.feedChannel(sender, f, len, millis());
  return true;
}

// rf.request {"to":"<node key hex>","message":"<the signed request, as text>"[,"slot":n]}: this Companion is the manager's radio.
// With a slot it goes on that channel, else point to point. It adds no authority: the message is carried as it came, and the
// target verifies it. Replies come back on the rf stream.
Status MeconIface::rfRequest(OpContext& c) {
  if (_rf_out) { c.error = "rf-busy"; return ST_BUSY; }
  const JsonDoc& d = *c.doc;
  char hex[65];
  if (c.req->args < 0 || !jsonGetString(d, jsonFind(d, c.req->args, "to"), hex, sizeof(hex), CS_NAME, 64)) return ST_INVALID;
  uint8_t to[32];
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1) return ST_INVALID;
    to[i] = (uint8_t)v;
  }
  char* msg = static_cast<char*>(malloc(LORA_MSG_MAX + 1));
  if (!msg) { c.error = "no-memory"; return ST_BUSY; }
  if (!jsonGetString(d, jsonFind(d, c.req->args, "message"), msg, LORA_MSG_MAX + 1, CS_ANY, 1)) {
    free(msg);
    c.error = "too-large";
    return ST_INVALID;
  }
  memcpy(_rf_to, to, 32);
  int64_t slot = -1;
  const int st = jsonFind(d, c.req->args, "slot");
  if (st >= 0 && !jsonGetInt(d, st, 0, 255, &slot)) { free(msg); return ST_INVALID; }
  _rf_slot = (int)slot;
  _rf_out = reinterpret_cast<uint8_t*>(msg);
  _rf_len = strlen(msg);
  _rf_off = 0;
  _rf_idx = 0;
  // A retry of a message whose frames did not all arrive names its xid: the target keeps what it has, and this fills the gap.
  const int xt = jsonFind(d, c.req->args, "xid");
  int64_t xv;
  if (xt >= 0 && jsonGetInt(d, xt, 0, 255, &xv)) _rf_xid = (uint8_t)xv;
  else _rf_xid++;
  _rf_count = (uint8_t)((_rf_len + LORA_REQ_PAYLOAD - 1) / LORA_REQ_PAYLOAD);
  _rf_next = millis();
  JsonWriter& w = *c.out;
  w.key("xid"); w.num(_rf_xid);
  w.key("frames"); w.num(_rf_count);
  return ST_OK;
}

void MeconIface::rfTick(uint32_t now) {
  if (!_rf_out || (int32_t)(now - _rf_next) < 0) return;
  uint8_t f[LORA_HDR + LORA_REQ_PAYLOAD];
  const size_t take = _rf_len - _rf_off < LORA_REQ_PAYLOAD ? _rf_len - _rf_off : LORA_REQ_PAYLOAD;
  f[0] = LORA_MARKER; f[1] = _rf_xid; f[2] = _rf_idx; f[3] = _rf_count;
  memcpy(f + LORA_HDR, _rf_out + _rf_off, take);
  const bool sent = _rf_slot >= 0 ? _mesh->loraChannelSend(_rf_slot, _rf_to, f, LORA_HDR + take) : _mesh->rfSend(_rf_to, f, LORA_HDR + take);
  if (!sent) { _rf_next = now + 200; return; }   // no packet free: the next pass tries
  _rf_off += take;
  _rf_idx++;
  _rf_next = now + LORA_FRAME_GAP_MS;
  if (_rf_off >= _rf_len) {
    Iface::wipe(_rf_out, _rf_len);
    free(_rf_out);
    _rf_out = nullptr;
  }
}

// A reply frame for this Companion's rf.request: when it completes it goes out on the rf stream as it came.
void MeconIface::onRfReply(const uint8_t* key, const uint8_t* frame, size_t n) {
  if (n <= LORA_REPLY_HDR) return;
  uint8_t* f = static_cast<uint8_t*>(malloc(n));
  if (!f) return;
  memcpy(f, frame + 3, n - 3);   // the three bytes before the xid become the header's marker
  f[0] = LORA_MARKER;
  static const uint8_t NO_SECRET[32] = {0};
  const LoraLink::Feed r = _rf.feed(key, NO_SECRET, nullptr, LoraLink::PATH_UNKNOWN, f, n - 3, _rf_slot >= 0 ? LORA_CH_REPLY_PAYLOAD : LORA_REPLY_PAYLOAD, millis());
  free(f);
  if (r != LoraLink::F_COMPLETE) return;
  LoraLink::Peer* p = _rf.nextComplete();
  if (!p) return;
  char* data = static_cast<char*>(malloc(RESULT_MAX));
  char* ev = static_cast<char*>(malloc(RESULT_MAX));
  if (data && ev) {
    char hex[65];
    hexOf(hex, key, 32);
    if (_rf_slot >= 0) hex[2 * LORA_CH_PREFIX] = 0;   // the channel names a prefix, no whole key
    JsonWriter w(data, RESULT_MAX);
    w.beginObject();
    w.key("from"); w.str(hex);
    w.key("xid"); w.num(p->xid);
    w.key("message"); w.str(LoraLink::message(*p));
    w.endObject();
    if (w.ok()) {
      const uint32_t ts = _mesh->getRTCClock()->knownTime();
      int m = writeEvent(ev, RESULT_MAX, STREAMS[SID_RF].name, ++_clock.seq[SID_RF], _epoch, ts != 0, ts, data);
      if (m > 0) m = _prov.sign(STREAMS[SID_RF].name, ev, (size_t)m, RESULT_MAX);
      if (m > 0) sendEvent(SID_RF, ev, (size_t)m);
    }
  }
  free(data);
  free(ev);
  _rf.release(*p);
}

uint8_t MeconIface::reportProduced(void*) {
  LocationFix f;
  return RF_HEALTH | RF_RADIO | (readLocation(sensors.getLocationProvider(), f) ? RF_LOCATION : 0);
}
bool MeconIface::reportMayRead(void* user) { return streamAllow(user, Cli::C_PRIVATE); }
const char* MeconIface::reportIssuer(void* user) { return static_cast<MeconIface*>(user)->_auth.signingIssuer(); }
bool MeconIface::reportIssuerValid(void* user, const char* issuer) { return static_cast<MeconIface*>(user)->_auth.issuerValid(issuer); }
uint32_t MeconIface::reportRandom(void*) {
  CryptoRNG rng;
  uint32_t v = 0;
  rng.random(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  return v;
}

bool MeconIface::reportSend(void* user, uint8_t fields, const uint8_t* to, int slot, uint32_t* seq) {
  MeconIface* m = static_cast<MeconIface*>(user);
  ReportData d;
  fillReportCommon(d, board, Uptime::secs(millis()));
  RadioCounts counts;
  counts.known = true;
  counts.rx_total = radio_driver.getPacketsRecv();
  counts.tx_total = radio_driver.getPacketsSent();
  counts.rx_errors = radio_driver.getPacketsRecvErrors();
  fillReportRadio(d, *m->_mesh, radio_driver, counts);
  if (fields & RF_LOCATION) {
    LocationFix f;
    if (!readLocation(sensors.getLocationProvider(), f)) return false;
    fillReportLocation(d, f);
  }
  uint8_t b[REPORT_MAX];
  const size_t n = m->_prov.makeReport(b, sizeof(b), fields, d, m->_mesh->getRTCClock()->knownTime(), seq);
  if (!n) return false;
  static const uint8_t ALL[32] = {0};
  if (slot >= 0) return m->_mesh->loraChannelSend(slot, to ? to : ALL, b, n);
  return to && m->_mesh->rfSend(to, b, n);
}

// A compact report heard: out on the rf stream as it came, for the manager to decode and verify.
void MeconIface::onRfReport(const uint8_t* key, size_t key_len, const uint8_t* bytes, size_t n) {
  if (n < REPORT_HEADER + REPORT_SIG || bytes[1] != REPORT_VERSION) return;
  const uint8_t fields = bytes[2];
  const size_t len = REPORT_HEADER + ((fields & RF_HEALTH) ? 8 : 0) + ((fields & RF_RADIO) ? 12 : 0) + ((fields & RF_LOCATION) ? 14 : 0) + REPORT_SIG;
  if (len > n) return;   // the datagram pads to whole blocks: the fields say where it ends
  char* data = static_cast<char*>(malloc(400));
  char* ev = static_cast<char*>(malloc(700));
  if (data && ev) {
    char hex[65];
    hexOf(hex, key, key_len);
    char b64[4 * (REPORT_MAX + 2) / 3 + 4];
    base64Encode(b64, bytes, len);
    JsonWriter w(data, 400);
    w.beginObject();
    w.key("from"); w.str(hex);
    w.key("report"); w.str(b64);
    w.endObject();
    if (w.ok()) {
      const uint32_t ts = _mesh->getRTCClock()->knownTime();
      int m = writeEvent(ev, 700, STREAMS[SID_RF].name, ++_clock.seq[SID_RF], _epoch, ts != 0, ts, data);
      if (m > 0) m = _prov.sign(STREAMS[SID_RF].name, ev, (size_t)m, 700);
      if (m > 0) sendEvent(SID_RF, ev, (size_t)m);
    }
  }
  free(data);
  free(ev);
}

// One part of the screen frame in hand. Beside streamTick for the same reason as drainLogs: a signature on top of its
// frame would overrun the loop stack.
void MeconIface::screenTick(uint32_t now) {
  if (!_screen.due(now)) return;
  bool any = _mqtt_subs.s[SID_SCREEN].active;
  for (Link& l : _links) any = any || l.subs.s[SID_SCREEN].active;
  if (!any) { _screen.cancel(); return; }
  char* data = static_cast<char*>(malloc(ScreenFeed::DATA_CAP));
  char* ev = static_cast<char*>(malloc(ScreenFeed::EVENT_CAP));
  if (data && ev && _screen.next(now, data, ScreenFeed::DATA_CAP)) {
    const uint32_t ts = _mesh->getRTCClock()->knownTime();
    int n = writeEvent(ev, ScreenFeed::EVENT_CAP, STREAMS[SID_SCREEN].name, ++_clock.seq[SID_SCREEN], _epoch, ts != 0, ts, data);
    if (n > 0) n = _prov.sign(STREAMS[SID_SCREEN].name, ev, (size_t)n, ScreenFeed::EVENT_CAP);
    if (n > 0) sendEvent(SID_SCREEN, ev, (size_t)n);
  } else if (!data || !ev) {
    _screen.cancel();   // no memory for this part: the frame is lost, and the seq gap or missing part shows it
  }
  free(data);
  free(ev);
}

uint8_t MeconIface::postureNow() const { return _mesh->postureValue(); }

void MeconIface::loop(uint32_t now) {
  if (!_standing_begun) {
    _standing_begun = true;
    _standing.begin(ReportHost{reportSend, reportProduced, reportMayRead, reportIssuer, reportIssuerValid, reportRandom, standingStore(), this}, now);
  }
  _gap.tick(now);
  _jobs.tick(now);
  _updates.loop(now);
  if (_reboot_at && (int32_t)(now - _reboot_at) >= 0) board.reboot();
  streamTick(now);
  tellMessages();
  tellInbox();
  drainLogs();   // beside streamTick, not inside it: a signed log event on top of its frame overran the loop stack (#234)
  inventoryTick(now);
  loraTick(now);
  rfTick(now);
  _standing.tick(now);
  screenTick(now);
  const bool cable_up = cableLineUp();
  for (Link& l : _links) {
    if (!l.src) continue;
    if (!cable_up) l.unlock.clear();   // the cable's host went: only the cable holds an unlock (AUTHORITY.md section 10)
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
      sendIncomplete(l, now);
    }
    if (!l.q || !l.q->count()) continue;
    uint8_t f[FRAME_MAX];
    if (!_mesh->ifaceWritable(l.src)) continue;
    size_t n = l.q->nextFrame(f);
    if (n) _mesh->ifaceWrite(l.src, f, n);
  }
}

#endif  // ENABLE_MECON_IFACE
