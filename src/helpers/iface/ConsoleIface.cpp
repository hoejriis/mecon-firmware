#include "ConsoleIface.h"
#include "AuditOps.h"
#include "../HeapFloor.h"
#include <helpers/esp32/NativeUsbTx.h>
#include <helpers/CryptoRNG.h>
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

// What a hello's result body may fill before its capability list goes on a next page (DEVICE_INTERFACE.md: one page unless it
// would outgrow the 1,536-byte result). The body is counted without the envelope and the signature: measured on a simulated
// Repeater, signing added 189 bytes, and the longest nonce's second device-id signature 156 more. A signed hello with that nonce
// then needs the body to page at about 1,250 bytes; 1,150 leaves margin. An unsigned hello (1,458 bytes with every capability and
// the longest nonce) fits in one page.
const size_t HELLO_SIGNED_BUDGET = 1150;
const size_t HELLO_PLAIN_BUDGET = 1500;
// Over LoRa a whole reply is at most LORA_MSG_MAX (1,024 bytes): the signed envelope, about 230 bytes without a nonce,
// leaves this much for the hello's body before it pages.
const size_t HELLO_LORA_BUDGET = 700;

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply",
                                    "sub.start", "sub.stop", "posture.set", "report.now", "standing.put", "standing.list", "standing.remove", "deployment.get", "deployment.provision",
                                    "deployment.authority_update", "deployment.reset", "deployment.set_unlock",
                                    "deployment.unlock_challenge", "deployment.unlock", "identity.rotate", "identity.rotation", "identity.get", "clock.get", "clock.set", "advert.send", "device.reboot", "device.reset", "identity.key.export", "acl.list", "acl.put", "password.set", "audit.list", "board.get", "board.adc.set", "power.saving.set", "sensor.list", "sensor.get", "sensor.set", "clock.reset", "stats.clear", "log.start", "log.stop", "log.erase", "log.read", "radio.temp",
                                    "channel.list", "channel.get", "channel.put", "channel.remove", "mgmt.channel.get",
                                    "mgmt.channel.bind", "mgmt.channel.rotate", "mgmt.channel.unbind", "job.get", "job.list",
                                    "job.ack", "job.cancel", "job.confirm", "update.manifest", "update.status",
                                    "update.offer", "update.chunk"};
// Listed only on a role with a region tree.
// The network-profile operations, on a build with Wi-Fi or MQTT (NetProfileOps.h).
const char* const NET_CAPABILITIES[] = {"net.profile.clear"
#ifdef ENABLE_MQTT
                                        , "mqtt.profile.anchor"
#endif
                                        };

// Listed only on the role that has them: the Repeater's neighbour table, the Room server's posts.
const char* const NEIGHBOUR_CAPABILITIES[] = {"neighbour.list", "neighbour.remove", "neighbour.discover"};
// Listed only on a build with GPS (ENV_INCLUDE_GPS), as the CLI's `gps` lines are.
#if ENV_INCLUDE_GPS == 1
const char* const GPS_CAPABILITIES[] = {"gps.get", "gps.set", "gps.sync", "gps.setloc", "gps.advert.set"};
#endif

const char* const REGION_CAPABILITIES[] = {"region.list", "region.put", "region.remove", "region.flood", "region.home",
                                           "region.default"};

Status opAuditListW(OpContext& c) { return opAuditList(c, ConsoleIface::streamAllow, self(c)); }
Status opNetClear(OpContext& c) {
  NetProfileHost h = self(c)->netHost();
  return opNetProfileClear(c, h);
}
Status opNetAnchor(OpContext& c) {
  NetProfileHost h = self(c)->netHost();
  return opMqttAnchor(c, h);
}

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
  w.key("variant"); w.str(MECON_VARIANT);
  w.key("upstream"); w.str(MECON_UPSTREAM_BASE);
  w.key("epoch"); w.unum(m->epoch());
  m->updates().writeAttestation(w);   // the image of an update job, while it awaits confirmation (UPDATES.md section 7)
  // The channel nonce of the console link, only once a Deployment record exists (AUTHORITY.md section 7.1).
  const char* nonce = m->helloNonce();
  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }
  // The list is paged (`cursor` in, `next` out, as DEVICE_INTERFACE.md says) once it would not leave room for the signature,
  // as on the Companion: an enrolled server signs the result, and the device-id signature and the nonce come before the list.
  // The region operations took the server's signed hello past the 1,536-byte result. Most hellos fit in one page.
  int64_t from = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, 255, &from)) return ST_INVALID;
  const size_t room = m->onLora() ? HELLO_LORA_BUDGET : m->helloSigned() ? HELLO_SIGNED_BUDGET : HELLO_PLAIN_BUDGET;
  unsigned idx = 0;
  int next = -1;
  const bool lora = m->onLora();
  auto add = [&](const char* cap) {
    if (lora && !loraOpAllowed(cap)) return;   // an operation this link does not carry is not offered on it
    if (idx++ < (unsigned)from || next >= 0) return;
    if (w.size() + strlen(cap) + 3 > room) { next = (int)idx - 1; return; }
    w.str(cap);
  };
  w.key("capabilities"); w.beginArray();
  add(CAPABILITIES[0]);   // hello, then the streams, so a manager reading only the first page has what it subscribes to
  const uint32_t produced = m->produced();
  for (int sid = 0; sid < STREAM_COUNT; sid++) {   // add() leaves them out over LoRa, which carries no stream
    if (!(produced & (1u << sid))) continue;
    char cap[24];
    snprintf(cap, sizeof(cap), "stream.%s", STREAMS[sid].name);
    add(cap);
  }
  for (size_t i = 1; i < sizeof(CAPABILITIES) / sizeof(CAPABILITIES[0]); i++) add(CAPABILITIES[i]);
  if (m->hasRegions()) for (const char* rcap : REGION_CAPABILITIES) add(rcap);
  if (netProfileOpsBuilt()) for (const char* ncap : NET_CAPABILITIES) add(ncap);
  if (m->host().neighbour) for (const char* ncap : NEIGHBOUR_CAPABILITIES) add(ncap);
  if (strcmp(m->host().role, "room") == 0) add("room.post");
#if ENV_INCLUDE_GPS == 1
  for (const char* gcap : GPS_CAPABILITIES) add(gcap);
#endif
  w.endArray();
  if (next >= 0) { w.key("next"); w.num(next); }
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
  m->lora().writeCounts(w);
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
Status opReportNow(OpContext& c) { return self(c)->standing().opNow(c, millis()); }
Status opStandingPut(OpContext& c) { return self(c)->standing().opPut(c, millis()); }
Status opStandingList(OpContext& c) { return self(c)->standing().opList(c); }
Status opStandingRemove(OpContext& c) { return self(c)->standing().opRemove(c); }
Status opPostureW(OpContext& c) {
  ConsoleIface* s = self(c);
  const uint8_t before = s->postureNow();
  const Status st = withConfig(c, opPostureSet);
  if (st == ST_OK && s->postureNow() != before) s->rebootSoon();   // what starts in each posture is decided at boot
  return st;
}
// The stored set is an open read; the rotation statement in it names device_id, so only a caller the table lets read gets it.
Status opDeployGet(OpContext& c) {
  const bool rotated = self(c)->mayRead() && self(c)->rotationStored();
  const Status st = self(c)->auth().opGet(c, (rotated ? DeployAuth::ROTATION_MEMBER : 0) +
                                                 (self(c)->onLora() ? DeployAuth::LORA_RESERVE : 0));
  if (st == ST_OK && rotated) self(c)->writeRotation(*c.out);
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
Status withRegions(OpContext& c, Status (*op)(OpContext&, RegionHost&)) {
  RegionHost h = self(c)->regionHost();
  if (!h.map) return ST_UNKNOWN_OP;   // the role has no region tree
  return op(c, h);
}
Status opRegListW(OpContext& c) { return withRegions(c, opRegionList); }
Status opRegPutW(OpContext& c) { return withRegions(c, opRegionPut); }
Status opRegRemoveW(OpContext& c) { return withRegions(c, opRegionRemove); }
Status opRegFloodW(OpContext& c) { return withRegions(c, opRegionFlood); }
Status opRegHomeW(OpContext& c) { return withRegions(c, opRegionHome); }
Status opRegDefaultW(OpContext& c) { return withRegions(c, opRegionDefault); }
Status opMgmtGetW(OpContext& c) { return withMgmt(c, opMgmtGet); }
Status opMgmtBindW(OpContext& c) { return withMgmt(c, opMgmtBind); }
Status opMgmtRotateW(OpContext& c) { return withMgmt(c, opMgmtRotate); }
Status opMgmtUnbindW(OpContext& c) { return withMgmt(c, opMgmtUnbind); }
Status opIdentityRotation(OpContext& c) { self(c)->writeRotation(*c.out); return ST_OK; }
// identity.get: the node key and device id, with the device id's signature, as hello gives them (OPERATIONS.md 5.1).
Status opIdentityGet(OpContext& c) {
  ConsoleIface* m = self(c);
  JsonWriter& w = *c.out;
  char nk[65];
  hexOf(nk, m->host().node_key, 32);
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("node_key"); w.str(nk);
  if (m->host().identity) return writeDeviceIdSig(c, *m->host().identity, m->deviceIdHex());
  return ST_OK;
}
// clock.get: wall-clock seconds; null while the clock is not set.
Status opClockGet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  const uint32_t t = h.clock_s ? h.clock_s(h.user) : 0;
  c.out->key("epoch"); if (t) c.out->unum(t); else c.out->null();
  return ST_OK;
}
// clock.set: {"epoch": n}, forward only, through the native `time` line (which refuses a time not after the clock's).
// The remote-admin ACL's roles, as the native permission byte's low two bits (helpers/ClientACL.h).
const char* const ACL_ROLES[] = {"guest", "read-only", "read-write", "admin"};
const unsigned ACL_PAGE = 8;
// acl.list {cursor}: the ACL, 8 entries a page: each client's node key and role, never its shared secret. The entries say
// who may administer the device, so it is private data, decided as the inventory stream that counts them is.
Status opAclList(OpContext& c) {
  ConsoleIface* m = self(c);
  const ConsoleIface::Host& h = m->host();
  if (!h.acl_count || !h.acl_entry) { c.error = "unsupported"; return ST_FAILED; }
  if (!ConsoleIface::streamAllow(m, Cli::C_PRIVATE)) { c.error = "private"; return ST_REFUSED; }
  int64_t cursor = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, 255, &cursor)) return ST_INVALID;
  const int n = h.acl_count(h.user);
  JsonWriter& w = *c.out;
  w.key("clients"); w.beginArray();
  int i = (int)cursor;
  for (unsigned k = 0; i < n && k < ACL_PAGE; i++, k++) {
    uint8_t key[32], perms = 0;
    if (!h.acl_entry(h.user, i, key, &perms)) continue;
    char hex[65];
    hexOf(hex, key, 32);
    w.beginObject();
    w.key("key"); w.str(hex);
    w.key("role"); w.str(ACL_ROLES[perms & 3]);
    w.endObject();
  }
  w.endArray();
  w.key("next"); if (i < n) w.num(i); else w.null();
  return ST_OK;
}
// acl.put {key, role}: sets a client's role through the native `setperm` line, so the CLI's gate decides it as this
// caller; a key not in the ACL yet is added, as setperm adds it.
Status opAclPut(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  if (!h.acl_count) { c.error = "unsupported"; return ST_FAILED; }
  char hex[65], role[16];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "key"), hex, sizeof(hex), CS_NAME, 64) ||
      strlen(hex) != 64 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "role"), role, sizeof(role), CS_NAME, 1))
    return ST_INVALID;
  for (const char* p = hex; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return ST_INVALID;
  int perms = -1;
  for (int r = 0; r < 4; r++) if (strcmp(role, ACL_ROLES[r]) == 0) perms = r;
  if (perms < 0) return ST_INVALID;
  char line[96], reply[80] = {0};
  snprintf(line, sizeof(line), "setperm %s %d", hex, perms);
  if (!h.run(h.user, line, reply, sizeof(reply)) || strncmp(reply, "OK", 2) != 0) { c.error = "refused"; return ST_FAILED; }
  c.out->key("key"); c.out->str(hex);
  c.out->key("role"); c.out->str(ACL_ROLES[perms]);
  return ST_OK;
}
// password.set {"which": "admin" | "guest", "password"}: the native `password` and `set guest.password` lines. Write-only:
// the answer names which one changed, never the value, and the native reply, which echoes it, is wiped. The console only:
// a broker session does not set it, and the LoRa link carries no write-only setting (LORA_MANAGEMENT section 2). Over
// the air the native remote admin keeps its own `password` line.
Status opPasswordSet(OpContext& c) {
  ConsoleIface* m = self(c);
  if (m->onBroker()) { c.error = "not-over-this-link"; return ST_REFUSED; }
  const ConsoleIface::Host& h = m->host();
  char which[8], pw[24] = {0};
  const bool parsed = c.req->args >= 0 &&
                      jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "which"), which, sizeof(which), CS_NAME, 1) &&
                      jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "password"), pw, sizeof(pw), CS_ANY, 1);
  const bool admin = parsed && strcmp(which, "admin") == 0, guest = parsed && strcmp(which, "guest") == 0;
  if (!admin && !guest) { wipe(pw, sizeof(pw)); return ST_INVALID; }
  if (strlen(pw) > 15) { wipe(pw, sizeof(pw)); c.error = "too-long"; return ST_INVALID; }   // the native field holds 15
  char line[48], reply[160] = {0};
  snprintf(line, sizeof(line), "%s%s", admin ? "password " : "set guest.password ", pw);
  const bool ran = h.run(h.user, line, reply, sizeof(reply));
  const bool ok = ran && (admin ? strncmp(reply, "password now", 12) == 0 : strncmp(reply, "OK", 2) == 0);
  wipe(line, sizeof(line));
  wipe(reply, sizeof(reply));
  wipe(pw, sizeof(pw));
  if (!ok) { c.error = "refused"; return ST_FAILED; }
  c.out->key("which"); c.out->str(admin ? "admin" : "guest");
  return ST_OK;
}
Status opClockSet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  int64_t t;
  if (c.req->args < 0 || !jsonGetInt(*c.doc, jsonFind(*c.doc, c.req->args, "epoch"), 1, 0xFFFFFFFFLL, &t)) return ST_INVALID;
  char line[24], reply[80] = {0};
  snprintf(line, sizeof(line), "time %lu", (unsigned long)t);
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strncmp(reply, "OK", 2) != 0) { c.error = strstr(reply, "backwards") ? "backwards" : "refused"; return ST_FAILED; }
  const uint32_t now = h.clock_s ? h.clock_s(h.user) : 0;
  c.out->key("epoch"); if (now) c.out->unum(now); else c.out->null();
  return ST_OK;
}
// advert.send: {"flood": true} floods the self advert; without it, zero hop. The native CLI line sends it, so the CLI's
// gate decides it as this caller too.
Status opAdvertSend(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  bool flood = false;
  const int tf = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "flood") : -1;
  if (tf >= 0 && !jsonGetBool(*c.doc, tf, &flood)) return ST_INVALID;
  char reply[64] = {0};
  if (!h.run(h.user, flood ? "advert" : "advert.zerohop", reply, sizeof(reply)) || strncmp(reply, "OK", 2) != 0) {
    c.error = "refused";
    return ST_FAILED;
  }
  c.out->key("flood"); c.out->boolean(flood);
  return ST_OK;
}
// One action CLI line that answers with ok_prefix when done; `refused` for anything else (the CLI gate, or the line's own
// refusal).
Status actionLine(OpContext& c, const char* line, const char* ok_prefix) {
  const ConsoleIface::Host& h = self(c)->host();
  char reply[160] = {0};
  if (!h.run(h.user, line, reply, sizeof(reply)) || strncmp(reply, ok_prefix, strlen(ok_prefix)) != 0) {
    c.error = "refused";
    return ST_FAILED;
  }
  return ST_OK;
}
// clock.reset: `clkreboot`, the clock set to the CLI's guess (2024-05-15) at once, then the reboot, after the answer.
Status opClockReset(OpContext& c) {
  ConsoleIface* m = self(c);
  if (!m->host().clock_guess) { c.error = "unsupported"; return ST_FAILED; }
  m->host().clock_guess(m->host().user);
  m->rebootSoon();
  return ST_OK;
}
// stats.clear: `clear stats`, the packet and radio counters back to zero.
Status opStatsClear(OpContext& c) { return actionLine(c, "clear stats", "(OK - stats reset)"); }
// log.start, log.stop, log.erase: the packet log's `log start`, `log stop` and `log erase`.
Status opLogStart(OpContext& c) { return actionLine(c, "log start", "   logging on"); }
Status opLogStop(OpContext& c) { return actionLine(c, "log stop", "   logging off"); }
Status opLogErase(OpContext& c) { return actionLine(c, "log erase", "   log erased"); }
// log.read {"offset"}: the packet log's text from offset, at most LOG_PAGE bytes: {"size","offset","text","next"}, next null
// at the end. The CLI's `log` prints the file to the serial port, from the console only (a remote line never runs it), so
// this is of the write class, as that line is, and a broker session is refused.
const size_t LOG_PAGE = 512;
Status opLogRead(OpContext& c) {
  ConsoleIface* m = self(c);
  if (m->onBroker()) {   // as `log`, the console only
    c.error = "not-over-this-link";
    return ST_REFUSED;
  }
  const ConsoleIface::Host& h = m->host();
  if (!h.log_read) { c.error = "unsupported"; return ST_FAILED; }
  int64_t offset = 0;
  const int ot = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "offset") : -1;
  if (ot >= 0 && !jsonGetInt(*c.doc, ot, 0, 0x7FFFFFFF, &offset)) return ST_INVALID;
  uint8_t buf[LOG_PAGE];
  uint32_t size = 0;
  const int n = h.log_read(h.user, (uint32_t)offset, buf, sizeof(buf), &size);
  JsonWriter& w = *c.out;
  if (n < 0) size = 0;   // no log yet: an empty one
  if ((uint32_t)offset > size) { c.error = "bad-offset"; return ST_INVALID; }
  const size_t got = n > 0 ? (size_t)n : 0;
  w.key("size"); w.unum(size);
  w.key("offset"); w.unum((uint32_t)offset);
  w.key("text"); w.str((const char*)buf, got);
  w.key("next"); if ((uint32_t)offset + got < size) w.unum((uint32_t)offset + got); else w.null();
  return ST_OK;
}
// radio.temp {"radio":"freq,bw,sf,cr","minutes"}: `tempradio`, radio settings for that many minutes (1 to 1,440), then the
// saved ones again. Not over LoRa, so the change never cuts its caller's own path; it reverts itself.
Status opRadioTemp(OpContext& c) {
  char radio[41];
  int64_t mins;
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "radio"), radio, sizeof(radio), CS_ANY, 7) ||
      !checkFormat("freq,bw,sf,cr", radio) || !jsonGetInt(*c.doc, jsonFind(*c.doc, c.req->args, "minutes"), 1, 1440, &mins))
    return ST_INVALID;
  char line[64];
  snprintf(line, sizeof(line), "tempradio %s,%u", radio, (unsigned)mins);
  const Status st = actionLine(c, line, "OK - temp params");
  if (st == ST_OK) { c.out->key("radio"); c.out->str(radio); c.out->key("minutes"); c.out->unum((uint32_t)mins); }
  return st;
}
// neighbour.list {"cursor"}: the heard neighbours, newest first as `neighbors` orders them, NEIGHBOUR_PAGE a page, each
// {"key","heard_s_ago","snr"} with the whole node key (the CLI shows four bytes) and the SNR in dB.
const int NEIGHBOUR_PAGE = 8;
Status opNeighbourList(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  if (!h.neighbour) { c.error = "not-on-this-device"; return ST_FAILED; }   // the Room server keeps no neighbour table
  int64_t cursor = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, 255, &cursor)) return ST_INVALID;
  JsonWriter& w = *c.out;
  w.key("neighbours"); w.beginArray();
  int i = (int)cursor, shown = 0;
  uint8_t key[32];
  uint32_t ago;
  int8_t snr4;
  for (; shown < NEIGHBOUR_PAGE && h.neighbour(h.user, i, key, &ago, &snr4); i++, shown++) {
    char hex[65];
    for (int b = 0; b < 32; b++) snprintf(hex + 2 * b, 3, "%02x", key[b]);
    w.beginObject();
    w.key("key"); w.str(hex);
    w.key("heard_s_ago"); w.unum(ago);
    char snr[12];
    snprintf(snr, sizeof(snr), "%.2f", snr4 / 4.0);
    w.key("snr"); w.raw(snr);
    w.endObject();
  }
  w.endArray();
  w.key("next"); if (shown == NEIGHBOUR_PAGE && h.neighbour(h.user, i, key, &ago, &snr4)) w.unum((uint32_t)i); else w.null();
  return ST_OK;
}
// neighbour.remove {"key"}: `neighbor.remove`, one whole node key (the CLI also takes a prefix, which can match several).
Status opNeighbourRemove(OpContext& c) {
  if (!self(c)->host().neighbour) { c.error = "not-on-this-device"; return ST_FAILED; }
  char hex[65];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "key"), hex, sizeof(hex), CS_NAME, 64))
    return ST_INVALID;
  for (const char* p = hex; *p; p++)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return ST_INVALID;
  char line[90];
  snprintf(line, sizeof(line), "neighbor.remove %s", hex);
  const Status st = actionLine(c, line, "OK");
  if (st == ST_OK) { c.out->key("key"); c.out->str(hex); }
  return st;
}
// neighbour.discover: `discover.neighbors`, a zero-hop discovery request on the air, under the radio's own duty-cycle rules.
Status opNeighbourDiscover(OpContext& c) {
  if (!self(c)->host().neighbour) { c.error = "not-on-this-device"; return ST_FAILED; }
  return actionLine(c, "discover.neighbors", "OK - Discover sent");
}
// room.post {"text"}: `room.post`, a system post to the room's clients. The role's CLI line holds 159 characters, so the
// text holds 149 after "room.post ".
const size_t MAX_ROOM_POST = 149;
Status opRoomPost(OpContext& c) {
  if (strcmp(self(c)->host().role, "room") != 0) { c.error = "not-on-this-device"; return ST_FAILED; }
  char text[MAX_ROOM_POST + 1];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "text"), text, sizeof(text), CS_ANY, 1))
    return ST_INVALID;
  char line[MAX_ROOM_POST + 16];
  snprintf(line, sizeof(line), "room.post %s", text);
  return actionLine(c, line, "OK");
}
// The text after "> " of a CLI get line run as this caller, or nullptr when the line is refused or answers anything else
// (the board cannot tell, or has no such reading).
const char* cliValue(OpContext& c, const char* line, char* reply, size_t cap) {
  const ConsoleIface::Host& h = self(c)->host();
  reply[0] = 0;
  if (!h.run(h.user, line, reply, cap) || strncmp(reply, "> ", 2) != 0) return nullptr;
  return reply + 2;
}
void strOrNull(JsonWriter& w, const char* key, const char* v) {
  w.key(key);
  if (v) w.str(v); else w.null();
}
// board.get: the board reads of the CLI, `get bootloader.ver`, `get adc.multiplier`, `get pwrmgt.support`,
// `get pwrmgt.source`, `get pwrmgt.bootmv`, `get pwrmgt.bootreason` and `powersaving`, as fields; null where the board
// cannot tell. `power_source` is the board's isExternalPowered(), the power stream's `external`.
Status opBoardGet(OpContext& c) {
  JsonWriter& w = *c.out;
  char r[160];
  strOrNull(w, "bootloader", cliValue(c, "get bootloader.ver", r, sizeof(r)));
  const char* adc = cliValue(c, "get adc.multiplier", r, sizeof(r));
  w.key("adc_multiplier");
  if (adc && *adc >= '0' && *adc <= '9') w.raw(adc); else w.null();   // "%.3f", a JSON number
  const char* mgmt = cliValue(c, "get pwrmgt.support", r, sizeof(r));
  w.key("power_management"); w.boolean(mgmt && strcmp(mgmt, "supported") == 0);
  strOrNull(w, "power_source", cliValue(c, "get pwrmgt.source", r, sizeof(r)));
  const char* mv = cliValue(c, "get pwrmgt.bootmv", r, sizeof(r));
  w.key("boot_mv"); if (mv && *mv >= '0' && *mv <= '9') w.unum(strtoul(mv, nullptr, 10)); else w.null();
  strOrNull(w, "boot_reason", cliValue(c, "get pwrmgt.bootreason", r, sizeof(r)));
  const ConsoleIface::Host& h = self(c)->host();
  r[0] = 0;
  const bool ps_read = h.run(h.user, "powersaving", r, sizeof(r)) && (strcmp(r, "on") == 0 || strcmp(r, "off") == 0);
  w.key("powersaving"); if (ps_read) w.boolean(strcmp(r, "on") == 0); else w.null();
  return ST_OK;
}
// board.adc.set {"multiplier"}: `set adc.multiplier`, a decimal from 0 to 10 with at most three places; 0 is the board's
// default. `unsupported` on a board without an adjustable battery divider.
Status opBoardAdcSet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  char v[12];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "multiplier"), v, sizeof(v), CS_ANY, 1) ||
      !checkFormat("multiplier", v))
    return ST_INVALID;
  char line[32], reply[80] = {0};
  snprintf(line, sizeof(line), "set adc.multiplier %s", v);
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strncmp(reply, "OK", 2) != 0) { c.error = strstr(reply, "unsupported") ? "unsupported" : "refused"; return ST_FAILED; }
  c.out->key("multiplier"); c.out->raw(v);
  return ST_OK;
}
// power.saving.set {"on"}: `powersaving on|off`. `unsupported` where the CLI says the board or a bridge build has none.
Status opPowerSavingSet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  bool on;
  if (c.req->args < 0 || !jsonGetBool(*c.doc, jsonFind(*c.doc, c.req->args, "on"), &on)) return ST_INVALID;
  char reply[80] = {0};
  if (!h.run(h.user, on ? "powersaving on" : "powersaving off", reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strncmp(reply, on ? "on" : "off", on ? 2 : 3) != 0) {
    c.error = strstr(reply, "not supported") ? "unsupported" : "refused";
    return ST_FAILED;
  }
  c.out->key("on"); c.out->boolean(on);
  return ST_OK;
}
// sensor.list {"cursor"}: `sensor list <cursor>`, the board's sensor settings from that index, as many as the CLI's page
// holds: {"total","settings":[{"name","value"}],"next"}, `next` null on the last page.
Status opSensorList(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  int64_t cursor = 0;
  const int ct = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "cursor") : -1;
  if (ct >= 0 && !jsonGetInt(*c.doc, ct, 0, 255, &cursor)) return ST_INVALID;
  char line[24], reply[200] = {0};
  snprintf(line, sizeof(line), "sensor list %u", (unsigned)cursor);
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  if (strcmp(reply, "no custom var") == 0) {   // the cursor is at or past the end, or there are none
    if (cursor > 0) { c.error = "bad-cursor"; return ST_INVALID; }
    w.key("total"); w.unum(0);
    w.key("settings"); w.beginArray(); w.endArray();
    w.key("next"); w.null();
    return ST_OK;
  }
  char* p = reply;
  const unsigned long total = strtoul(p, &p, 10);
  if (strncmp(p, " vars\n", 6) != 0) { c.error = "unreadable"; return ST_FAILED; }
  p += 6;
  long next = -1;
  w.key("total"); w.unum(total);
  w.key("settings"); w.beginArray();
  while (*p) {
    char* end = strchr(p, '\n');
    if (end) *end = 0;
    if (strncmp(p, "... next:", 9) == 0) next = strtol(p + 9, nullptr, 10);
    else if (char* eq = strchr(p, '=')) {
      *eq = 0;
      w.beginObject(); w.key("name"); w.str(p); w.key("value"); w.str(eq + 1); w.endObject();
    }
    if (!end) break;
    p = end + 1;
  }
  w.endArray();
  w.key("next"); if (next >= 0) w.unum((uint32_t)next); else w.null();
  return ST_OK;
}
// sensor.get {"name"}: `sensor get <name>`; `unknown-setting` when the board has none of that name.
Status opSensorGet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  char name[24];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "name"), name, sizeof(name), CS_NAME, 1))
    return ST_INVALID;
  char line[40], reply[160] = {0};
  snprintf(line, sizeof(line), "sensor get %s", name);
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strncmp(reply, "> ", 2) != 0) { c.error = "unknown-setting"; return ST_FAILED; }
  c.out->key("name"); c.out->str(name);
  c.out->key("value"); c.out->str(reply + 2);
  return ST_OK;
}
// sensor.set {"name","value"}: `sensor set <name> <value>`. The CLI splits the line at spaces, so a value holds none.
Status opSensorSet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  char name[24], value[64];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "name"), name, sizeof(name), CS_NAME, 1) ||
      !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "value"), value, sizeof(value), CS_ANY, 1))
    return ST_INVALID;
  if (strchr(value, ' ')) { c.error = "bad-value"; return ST_INVALID; }
  char line[100], reply[80] = {0};
  snprintf(line, sizeof(line), "sensor set %s %s", name, value);
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strcmp(reply, "ok") != 0) { c.error = "unknown-setting"; return ST_FAILED; }
  c.out->key("name"); c.out->str(name);
  c.out->key("value"); c.out->str(value);
  return ST_OK;
}
#if ENV_INCLUDE_GPS == 1
// A CLI line that answers "ok" when done; `no-gps` for anything else (the board has no receiver or no GPS toggle).
Status gpsLine(OpContext& c, const char* line) {
  const ConsoleIface::Host& h = self(c)->host();
  char reply[80] = {0};
  if (!h.run(h.user, line, reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }
  if (strcmp(reply, "ok") != 0) { c.error = "no-gps"; return ST_FAILED; }
  return ST_OK;
}
// gps.get: `gps`, `gps advert` and `gps diag` as fields. `receiver` false, and the rest null, on a board with no GPS.
// The CLI classes `gps advert` and `gps diag` with the writes (neither is registered): each runs as this caller, so its
// gate refuses them to a caller limited to reads, and the field is null.
Status opGpsGet(OpContext& c) {
  const ConsoleIface::Host& h = self(c)->host();
  char r[160] = {0};
  if (!h.run(h.user, "gps", r, sizeof(r))) { c.error = "refused"; return ST_FAILED; }
  JsonWriter& w = *c.out;
  const bool receiver = strcmp(r, "Can't find GPS") != 0;
  const bool powered = receiver && strncmp(r, "on, ", 4) == 0;
  w.key("receiver"); w.boolean(receiver);
  w.key("powered"); if (receiver) w.boolean(powered); else w.null();
  w.key("active"); if (powered) w.boolean(strstr(r, ", active,") != nullptr); else w.null();
  w.key("fix"); if (powered) w.boolean(strstr(r, ", fix,") != nullptr); else w.null();
  const char* sats = powered ? strrchr(r, ',') : nullptr;
  w.key("sats"); if (sats) w.unum(strtoul(sats + 1, nullptr, 10)); else w.null();
  r[0] = 0;
  const bool policy = h.run(h.user, "gps advert", r, sizeof(r)) && strncmp(r, "> ", 2) == 0;
  w.key("advert"); if (policy) w.str(r + 2); else w.null();
  r[0] = 0;
  const bool diag = receiver && h.run(h.user, "gps diag", r, sizeof(r)) && strncmp(r, "req:", 4) == 0;
  w.key("diag"); if (diag) w.str(r); else w.null();
  return ST_OK;
}
// gps.set {"on"}: `gps on|off`, saved, as the CLI saves it.
Status opGpsSet(OpContext& c) {
  bool on;
  if (c.req->args < 0 || !jsonGetBool(*c.doc, jsonFind(*c.doc, c.req->args, "on"), &on)) return ST_INVALID;
  const Status st = gpsLine(c, on ? "gps on" : "gps off");
  if (st == ST_OK) { c.out->key("on"); c.out->boolean(on); }
  return st;
}
// gps.sync: `gps sync`, the clock set from the receiver's next fix.
Status opGpsSync(OpContext& c) { return gpsLine(c, "gps sync"); }
// gps.setloc: `gps setloc`, the receiver's position into the saved lat and lon; the answer is what was saved.
Status opGpsSetloc(OpContext& c) {
  const Status st = gpsLine(c, "gps setloc");
  if (st != ST_OK) return st;
  char r[40];
  strOrNull(*c.out, "lat", cliValue(c, "get lat", r, sizeof(r)));
  strOrNull(*c.out, "lon", cliValue(c, "get lon", r, sizeof(r)));
  return ST_OK;
}
// gps.advert.set {"policy"}: `gps advert none|share|prefs`, which position the self advert carries.
Status opGpsAdvertSet(OpContext& c) {
  char p[8];
  if (c.req->args < 0 || !jsonGetString(*c.doc, jsonFind(*c.doc, c.req->args, "policy"), p, sizeof(p), CS_NAME, 1) ||
      (strcmp(p, "none") != 0 && strcmp(p, "share") != 0 && strcmp(p, "prefs") != 0))
    return ST_INVALID;
  char line[24];
  snprintf(line, sizeof(line), "gps advert %s", p);
  const Status st = gpsLine(c, line);
  if (st == ST_OK) { c.out->key("policy"); c.out->str(p); }
  return st;
}
#endif
// device.reboot: answered first, then the reboot (1.5 s, or the LoRa delay so that the answer goes out).
Status opDeviceReboot(OpContext& c) { self(c)->rebootSoon(); return ST_OK; }
Status opDeviceResetW(OpContext& c) {
  ResetHost h = self(c)->resetHost();
  return opDeviceReset(c, h);
}
// identity.key.export: the console's stock path is `get prv.key`; the same custody here (KeyExportOps.h), no phone.
Status opKeyExportW(OpContext& c) {
  ConsoleIface* m = self(c);
  const Cli::Context& ctx = m->callerContext();
  const Dep::Member* member = m->auth().signingMember();
  KeyExportHost h{m->host().identity, m->host().identity != nullptr, false, Cli::principalOf(ctx),
                  ctx.posture != Posture::NORMAL, m->auth().state(), member ? member->public_key : nullptr,
                  member ? member->instance_id : nullptr, m->auth().signingJob()};
  return opKeyExport(c, h);
}
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
  {"report.now", opReportNow, PRIV_WRITE, false},
  {"standing.put", opStandingPut, PRIV_WRITE, true},
  {"standing.list", opStandingList, PRIV_READ, false},
  {"standing.remove", opStandingRemove, PRIV_WRITE, true},
  {"posture.set", opPostureW, PRIV_WRITE, true},
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
  {"identity.get", opIdentityGet, PRIV_READ, false},
  {"clock.get", opClockGet, PRIV_READ, false},
  {"clock.set", opClockSet, PRIV_WRITE, true},
  {"acl.list", opAclList, PRIV_READ, false},
  {"acl.put", opAclPut, PRIV_WRITE, true},
  {"password.set", opPasswordSet, PRIV_WRITE, true},
  // The role's actions (#492), each of its CLI line's class: none of those lines is registered, so all are writes.
  {"clock.reset", opClockReset, PRIV_WRITE, true},
  {"stats.clear", opStatsClear, PRIV_WRITE, true},
  {"log.start", opLogStart, PRIV_WRITE, true},
  {"log.stop", opLogStop, PRIV_WRITE, true},
  {"log.erase", opLogErase, PRIV_WRITE, true},
  {"log.read", opLogRead, PRIV_WRITE, false},
  {"radio.temp", opRadioTemp, PRIV_WRITE, true},
  {"neighbour.list", opNeighbourList, PRIV_READ, false},   // `neighbors` is registered as a read
  {"neighbour.remove", opNeighbourRemove, PRIV_WRITE, true},
  {"neighbour.discover", opNeighbourDiscover, PRIV_WRITE, true},
  {"room.post", opRoomPost, PRIV_WRITE, true},
  {"board.get", opBoardGet, PRIV_READ, false},
  {"board.adc.set", opBoardAdcSet, PRIV_WRITE, true},
  {"power.saving.set", opPowerSavingSet, PRIV_WRITE, true},
  {"sensor.list", opSensorList, PRIV_READ, false},
  {"sensor.get", opSensorGet, PRIV_READ, false},
  {"sensor.set", opSensorSet, PRIV_WRITE, true},
#if ENV_INCLUDE_GPS == 1
  {"gps.get", opGpsGet, PRIV_READ, false},
  {"gps.set", opGpsSet, PRIV_WRITE, true},
  {"gps.sync", opGpsSync, PRIV_WRITE, true},
  {"gps.setloc", opGpsSetloc, PRIV_WRITE, true},
  {"gps.advert.set", opGpsAdvertSet, PRIV_WRITE, true},
#endif
  {"advert.send", opAdvertSend, PRIV_WRITE, true},
  {"device.reboot", opDeviceReboot, PRIV_REBOOT, true},
  {"device.reset", opDeviceResetW, PRIV_DESTRUCTIVE, true},
  {"audit.list", opAuditListW, PRIV_READ, false},   // the posture class is checked in the handler
#if defined(ENABLE_WIFI_INTERFACE) || defined(ENABLE_MQTT)
  {"net.profile.clear", opNetClear, PRIV_WRITE, true},
#endif
#ifdef ENABLE_MQTT
  {"mqtt.profile.anchor", opNetAnchor, PRIV_WRITE, true},
#endif
  {"identity.key.export", opKeyExportW, PRIV_READ, false},   // the handler decides who may have it: never cached
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
  // The native region tree; every change confirm-or-revert for anyone but the cable (OPERATIONS.md, "Region scopes").
  {"region.list", opRegListW, PRIV_READ, false},
  {"region.put", opRegPutW, PRIV_WRITE, true},
  {"region.remove", opRegRemoveW, PRIV_WRITE, true},
  {"region.flood", opRegFloodW, PRIV_WRITE, true},
  {"region.home", opRegHomeW, PRIV_WRITE, true},
  {"region.default", opRegDefaultW, PRIV_WRITE, true},
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

static bool consoleReset(void* user, ResetScope s, uint8_t caller) {
  const ConsoleIface::Host& h = static_cast<ConsoleIface*>(user)->host();
  return h.reset && h.reset(h.user, s, caller);
}
static void consoleReboot(void* user) { static_cast<ConsoleIface*>(user)->rebootSoon(); }

ResetHost ConsoleIface::resetHost() {
  uint16_t scopes = 0;
  if (_host.reset) {
    scopes = (1u << RS_RADIO) | (1u << RS_FACTORY);
#if defined(ENABLE_WIFI_INTERFACE) || defined(ENABLE_MQTT)
    scopes |= 1u << RS_NETWORK;
#endif
  }
  return ResetHost{scopes, consoleReset, consoleReboot, Cli::auditCaller(Cli::principalOf(_ctx)), this};
}

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
  if (hasRegions()) { RegionHost rh = regionHost(); regionSnapBoot(rh); }   // and the region tree's copy
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

RegionHost ConsoleIface::regionHost() {
  return RegionHost{_host.regions, _host.regions_save, _host.regions_default_changed, _host.user, _host.region_snap, &_jobs,
                    confirmCable, confirmRecordCaller, confirmJobId, (uint8_t)(1u << Cli::C_WRITE), this};
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
bool ConsoleIface::rotationStored() {
  char text[256], sig[89];
  return _host.read_rotation && _host.read_rotation(_host.user, text, sizeof(text), sig, sizeof(sig));
}

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

// One part of the screen frame in hand. Beside the stream pass, not inside it: a signature on top of its frame would
// overrun the loop stack.
void ConsoleIface::screenTick(uint32_t now) {
  if (!_screen.due(now) || (_collecting && !mqttSubscribed())) return;
  if (!_subs.s[SID_SCREEN].active && !_mqtt_subs.s[SID_SCREEN].active) { _screen.cancel(); return; }
  char* data = static_cast<char*>(malloc(ScreenFeed::DATA_CAP));
  char* ev = static_cast<char*>(malloc(ScreenFeed::EVENT_CAP));
  if (data && ev && _screen.next(now, data, ScreenFeed::DATA_CAP)) {
    bool known;
    const uint32_t ts = knownSeconds(&known);
    int n = writeEvent(ev, ScreenFeed::EVENT_CAP, STREAMS[SID_SCREEN].name, ++_clock.seq[SID_SCREEN], _epoch, known, ts, data);
    if (n > 0) n = _prov.sign(STREAMS[SID_SCREEN].name, ev, (size_t)n, ScreenFeed::EVENT_CAP);
    if (n > 0) emit(SID_SCREEN, ev, (size_t)n);
  } else if (!data || !ev) {
    _screen.cancel();   // no memory for this part: the frame is lost, and the seq gap or missing part shows it
  }
  free(data);
  free(ev);
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
  return ConfigHost{cliRun, cliAllow, this, SERVER_KEYS, N_SERVER_KEYS, &_confirm,
                    [](void* u) { return static_cast<ConsoleIface*>(u)->onLora(); }};
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
    s->_lora_key_dirty = true;
    s->_jobs.table()->setState(&j, JOB_REVERTED);
    return true;
  }
  if (regionOwnsJob(j) && s->hasRegions()) {
    RegionHost rh = s->regionHost();
    if (!regionRevert(rh, j)) return false;
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
  if (regionOwnsJob(j) && static_cast<ConsoleIface*>(user)->hasRegions()) {
    RegionHost rh = static_cast<ConsoleIface*>(user)->regionHost();
    regionConfirmed(rh);
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
                      : op.priv == PRIV_REBOOT ? Cli::C_REBOOT
                      : op.priv == PRIV_DESTRUCTIVE ? Cli::C_DESTRUCTIVE
                                               : Cli::C_POSTURE;
  Cli::Context ctx = s->_ctx;
  // Beyond reads and a reboot, preAuthorize and the handlers have decided. A reboot is left to the table, which keeps it for
  // an unsigned local caller on an enrolled device and refuses it to an unsigned remote one.
  ctx.enrolled = (cls == Cli::C_READ || cls == Cli::C_REBOOT || cls == Cli::C_DESTRUCTIVE) && s->_auth.enrolled();
  const Cli::Verdict v = Cli::decide(ctx, cls, strcmp(op.name, "hello") == 0 ? Cli::F_IDENTIFY : 0);   // a refusal says why, as on the Companion
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
  d.setCaller((uint8_t)Cli::principalOf(_ctx));   // one console, one broker session: the principal tells them apart
  if (_on_lora) d.setOpFilter(_lora_channel ? loraChannelOpAllowed : loraOpAllowed);
  if (mgmtMayChangeBinding(msg, len)) _lora_key_dirty = true;
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
    case SID_RUNTIME:   // heap_low absent where the build does not check (HeapFloor.h: ESP32 only)
      writeRuntime(w, *_host.board, _gap, heapFloor().enabled() ? (heapFloor().low() ? 1 : 0) : -1, &Posture::staleLogins());
      break;
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

void ConsoleIface::setReportSend(ReportSendFn fn, void* user, SnapStore* store) {
  _report_send = fn;
  _report_user = user;
  _standing.begin(ReportHost{reportSend, reportProduced, reportMayRead, reportIssuer, reportIssuerValid, reportRandom, reportAirtimeOk, store, this},
                  millis());
}

uint8_t ConsoleIface::reportProduced(void* user) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  LocationFix f;
  const bool loc = s->_host.sensors && readLocation(s->_host.sensors->getLocationProvider(), f);
  return RF_HEALTH | RF_RADIO | (loc ? RF_LOCATION : 0);
}

bool ConsoleIface::reportMayRead(void* user) { return streamAllow(user, Cli::C_PRIVATE); }
const char* ConsoleIface::reportIssuer(void* user) { return static_cast<ConsoleIface*>(user)->_auth.signingIssuer(); }
bool ConsoleIface::reportIssuerValid(void* user, const char* issuer) { return static_cast<ConsoleIface*>(user)->_auth.issuerValid(issuer); }

// Room for the largest report, as an anonymous datagram with its packet's headers.
bool ConsoleIface::reportAirtimeOk(void* user) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (!s->_host.dispatcher || !s->_host.radio) return true;
  return s->_host.dispatcher->getRemainingTxBudget() >= s->_host.radio->getEstAirtimeFor((int)REPORT_MAX + 40);
}

uint32_t ConsoleIface::reportRandom(void*) {
  CryptoRNG rng;
  uint32_t v = 0;
  rng.random(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  return v;
}

bool ConsoleIface::reportSend(void* user, uint8_t fields, const uint8_t* to, int slot, uint32_t* seq) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (!s->_report_send) return false;
  ReportData d;
  fillReportCommon(d, *s->_host.board, Uptime::secs(millis()));
  if (s->_host.battery_mv) { const uint16_t mv = s->_host.battery_mv(s->_host.user); if (mv) d.battery_mv = mv; }
  RadioCounts counts;
  if (s->_host.radio_counts) s->_host.radio_counts(s->_host.user, &counts);
  fillReportRadio(d, *s->_host.dispatcher, *s->_host.radio, counts);
  if (fields & RF_LOCATION) {
    LocationFix f;
    if (!s->_host.sensors || !readLocation(s->_host.sensors->getLocationProvider(), f)) return false;
    fillReportLocation(d, f);
  }
  bool known;
  const uint32_t ts = s->knownSeconds(&known);
  uint8_t b[REPORT_MAX];
  const size_t n = s->_prov.makeReport(b, sizeof(b), fields, d, known ? ts : 0, seq);
  return n && s->_report_send(s->_report_user, to, slot, b, n);
}

// The bound channel's key, or false: a stale or absent binding takes nothing (MANAGEMENT_CHANNEL.md section 2). The mesh asks on
// every group packet it hears, and a file read here holds the radio's next packet, so it answers from what loraKeyRefresh read.
bool ConsoleIface::loraChannelKey(void* user, uint8_t out[16]) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (s->_lora_key_ok) memcpy(out, s->_lora_key, 16);
  return s->_lora_key_ok;
}

// On the main pass, never on a received packet: at boot, after a request that may have changed the binding, and when a lapsed
// confirmation reverts it. Never on a timer: a read holds the loop for about 0.9 s on an ESP32.
void ConsoleIface::loraKeyRefresh() {
  if (_lora_key_read && !_lora_key_dirty) return;
  _lora_key_read = true;
  _lora_key_dirty = false;
  _lora.noteBindingRead();
  MgmtHost h = mgmtHost();
  MgmtRecord rec;
  char name[CHANNEL_NAME_MAX + 1];
  _lora_key_ok = mgmtState(h, &rec) == MGMT_BOUND && _mgmt_slot.read(name, _lora_key);
}

bool ConsoleIface::onLoraChannelFrame(void* user, const uint8_t* data, size_t n) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  if (n <= LORA_CH_ADDR + LORA_HDR || !s->_host.node_key || memcmp(data, s->_host.node_key, LORA_CH_PREFIX) != 0) return false;
  s->_lora.feedChannel(data + LORA_CH_PREFIX, data + LORA_CH_ADDR, n - LORA_CH_ADDR, millis());
  return true;
}

// One complete message on this pass, then its reply a frame at a time. Over LoRa only a signed request runs (section 2).
void ConsoleIface::loraTick(uint32_t now) {
  if (!_lora_send) return;
  loraKeyRefresh();
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
        const Cli::Context ctx(Cli::T_LORA, Cli::A_NONE, _host.posture(_host.user));
        n = (int)onMqtt(ctx, reinterpret_cast<const uint8_t*>(msg), len, res, LORA_MSG_MAX + 1);
        _on_lora = false;
        verified = _auth.lastVerified();
      } else {
        n = writeResult(res, LORA_MSG_MAX + 1, "", ST_REFUSED, nullptr, "unauthenticated");
      }
      // A result that came out empty or past the LoRa bound is answered, not dropped: the manager learns why, at once.
      if (n <= 0 || (size_t)n > LORA_MSG_MAX) { n = writeResult(res, LORA_MSG_MAX + 1, "", ST_BUSY, nullptr, "too-large"); verified = false; }
      if (n > 0) _lora.queueReply(*p, res, (size_t)n, verified, now);
      free(res);
    }
    _lora.release(*p);
  }
  _lora.tick(now, _lora_send, _lora_send_user);
}

void ConsoleIface::loop(uint32_t now, Print& out) {
  _out = &out;
  nativeUsbTxNudge(now);
  loraTick(now);
  _standing.tick(now);
  if (!cableLineUp()) _unlock.clear();   // the cable's host went (AUTHORITY.md section 10)
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
  screenTick(now);
  if (_collecting && !mqttSubscribed()) return;   // an event never lands in the middle of a request being typed
  SubSet* sets[2] = { &_subs, &_mqtt_subs };
  const uint32_t due = _clock.due(sets, 2, now);
  bool known;
  const uint32_t ts = knownSeconds(&known);
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    if (s == SID_SCREEN) { _screen.start(now); continue; }   // in parts: screenTick sends it
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[800];   // an event of up to 640 bytes, and its signature
    int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, known, ts, data);
    if (n > 0) n = _prov.sign(STREAMS[s].name, ev, (size_t)n, sizeof(ev));   // -1: not signable, so not sent
    if (n > 0) emit(s, ev, (size_t)n);
  }
  if (_boot_pending && (due & (1u << SID_RUNTIME))) {   // once per epoch, right after the first runtime snapshot
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
