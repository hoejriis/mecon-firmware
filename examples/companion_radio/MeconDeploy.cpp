// The Deployment authority layer of the Companion's device interface (docs/contract/AUTHORITY.md). The layer itself is
// shared with the Repeater and Room server console and lives in src/helpers/iface/DeployAuth.cpp; this file is the
// Companion's glue to it: its DataStore as the record's file store, the boot load, and the forwarders the operation table
// and the rest of the Companion call.
#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <new>
#include <helpers/iface/Wipe.h>
#include <helpers/iface/JobFsStore.h>
#include <helpers/cli/CliRegistry.h>

using namespace Iface;

// ---- storage

bool DataStoreCopies::read(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, Dep::StoreFile which) {
  return _s->readDeploymentCopy(slot, buf, cap, len, exists, which == Dep::SF_MARKS ? DataStore::DEP_MARKS : DataStore::DEP_RECORD);
}

bool DataStoreCopies::write(int slot, const uint8_t* frame, size_t len, Dep::StoreFile which) {
  return _s->writeDeploymentCopy(slot, frame, len, which == Dep::SF_MARKS ? DataStore::DEP_MARKS : DataStore::DEP_RECORD);
}

bool DataStoreCopies::removeBoth(int newer_slot, Dep::StoreFile which) {
  return _s->removeDeploymentCopies(newer_slot, which == Dep::SF_MARKS ? DataStore::DEP_MARKS : DataStore::DEP_RECORD);
}

// ---- jobs and commit-or-revert (OPERATIONS.md section 3)

namespace {
Iface::JobFsStore g_job_file;    // the DataStore's file system
Iface::SnapFsStore g_snap_file;
}  // namespace

void MeconIface::jobsLoad() {
  g_job_file.setFs(_mesh->_store->getPrimaryFS());
  g_snap_file.setFs(_mesh->_store->getPrimaryFS());
  mgmtLoad();   // before the boot rule below, which may revert a management channel change
  _confirm = Iface::ConfirmHost{&_jobs, &g_snap_file, confirmCable, confirmLineClass, confirmReadSecret,
                                confirmRecordCaller, confirmRunAs, confirmJobId};
  _jobs.setClassCheck(jobClasses, this);
  _jobs.setOnChange(jobChanged, this);
  _updates.setHost(Upd::UpdateService::Host{&_jobs, this, confirmJobId, confirmRecordCaller, updSigned, updReboot});
  _jobs_booting = true;   // a revert at boot needs no reboot: the links start after this, with the restored prefs
  _jobs.begin(&g_job_file, abnormalReset(board) == 1, jobRevert, jobConfirm, this);
  _jobs_booting = false;
  _updates.bootReconcile();   // an installed image attests, or the bootloader rolled it back
  Iface::configSnapBoot(configHost());   // a snapshot no awaiting job owns goes: it holds old secrets
  Iface::mgmtSnapBoot(mgmtHost());       // likewise the management channel's: it holds an old key
}

// The unsigned cable keeps its path whatever it changes. A signed request on the cable is a signed caller.
bool MeconIface::confirmCable(void* user) {
  return Cli::principalOf(static_cast<MeconIface*>(user)->_ctx) == Cli::P_CABLE;
}

uint8_t MeconIface::confirmLineClass(void*, const char* line) { return Cli::resolve(line, Cli::ROLE_COMPANION).cls; }

// The write-only values, read from the prefs into the snapshot only: never into a result, an event or a log.
bool MeconIface::confirmReadSecret(void* user, const Iface::KeyDef& k, unsigned idx, char* out, size_t cap) {
  NodePrefs* p = static_cast<MeconIface*>(user)->_mesh->getNodePrefs();
  const char* v = nullptr;
#ifdef ENABLE_CONNMODE
  if (strcmp(k.key, "wifi.profile.{n}.pwd") == 0 && idx < WifiProfiles::COUNT) v = p->wifi_profiles.pwd((uint8_t)idx);
#endif
#ifdef ENABLE_MQTT
  if (idx < MqttProfiles::COUNT) {
    if (strcmp(k.key, "mqtt.profile.{n}.user") == 0) v = p->mqtt_profiles.p[idx].user;
    if (strcmp(k.key, "mqtt.profile.{n}.pwd") == 0) v = p->mqtt_profiles.p[idx].pwd;
  }
#endif
  (void)p;
  if (!v || strlen(v) >= cap) return false;
  strcpy(out, v);
  return true;
}

void MeconIface::confirmRecordCaller(void* user, Iface::Job& j) {
  const MeconIface* m = static_cast<MeconIface*>(user);
  j.who_transport = m->_ctx.transport;
  j.who_auth = m->_ctx.auth;
  j.who_posture = m->_ctx.posture;
  j.who_mgmt = m->_ctx.mgmt ? 1 : 0;
  j.who_audit = m->_caller;
}

// A revert line runs as the caller who applied the change, decided by the CLI table under today's posture: it can do
// nothing that caller could not, and the audit names them.
bool MeconIface::confirmRunAs(void* user, const Iface::Job& j, const char* line, char* reply, size_t cap) {
  MeconIface* m = static_cast<MeconIface*>(user);
  MyMesh* mesh = m->_mesh;
  const Cli::Context ctx(j.who_transport, j.who_auth, mesh->postureValue(), j.who_mgmt != 0);
  mesh->setCliCaller(ctx, j.who_audit);
  char buf[sizeof(mesh->reply_buf)];
  buf[0] = 0;
  const bool known = mesh->handleCommand(line, ctx.remote ? 1 : 0, buf);
  mesh->setCliCaller(Cli::Context(), Posture::CALLER_NET);
  snprintf(reply, cap, "%s", buf);
  Iface::wipe(buf, sizeof(buf));
  return known;
}

bool MeconIface::confirmJobId(void* user, const Iface::Request& req, char* out, size_t cap) {
  return Iface::JobService::identity(&static_cast<MeconIface*>(user)->_auth.answering(), req, out, cap);
}

bool MeconIface::jobRevert(void* user, Iface::Job& j) {
  MeconIface* m = static_cast<MeconIface*>(user);
  if (Upd::UpdateService::owns(j)) return m->_updates.jobRevert(j);   // reboots itself when an image changes
  if (Iface::mgmtOwnsJob(j)) {   // the management channel's ops, by exact name (MgmtChannel.cpp)
    if (!Iface::mgmtRevert(m->mgmtHost(), j)) return false;
    m->_jobs.table()->setState(&j, Iface::JOB_REVERTED);
    return true;
  }
  if (strcmp(j.op, "config.apply") != 0) return false;
  bool reboot = false;
  if (!Iface::configRevert(m->configHost(), j, &reboot)) return false;   // the job ends failed, revert-failed
  m->_jobs.table()->setState(&j, Iface::JOB_REVERTED);
  if (reboot && !m->_jobs_booting) m->_reboot_at = millis() + 1500;   // the result and the job event go out first
  return true;
}

bool MeconIface::jobConfirm(void* user, Iface::Job& j) {
  if (Upd::UpdateService::owns(j)) return static_cast<MeconIface*>(user)->_updates.jobConfirm(j);
  if (Iface::mgmtOwnsJob(j)) {
    Iface::mgmtConfirmed(static_cast<MeconIface*>(user)->mgmtHost());
    return true;
  }
  if (strcmp(j.op, "config.apply") != 0) return false;
  Iface::configConfirmed(static_cast<MeconIface*>(user)->configHost());
  return true;
}

bool MeconIface::updSigned(void* user) { return static_cast<MeconIface*>(user)->_auth.isSigned(); }

// After the answer has gone out: an activated image boots, or a reverted one goes back to the previous image. At boot
// too, unlike a configuration revert: the image to run is chosen only by a reboot.
void MeconIface::updReboot(void* user) { static_cast<MeconIface*>(user)->_reboot_at = millis() + 1500; }

// A job event goes to every connected transport whose last caller may read, and over the broker session, not only to
// the transport that started the job: a result goes out over whichever path works (OPERATIONS.md 3.2).
void MeconIface::jobChanged(void* user, const Iface::Job& j) {
  MeconIface* m = static_cast<MeconIface*>(user);
  char* ev = static_cast<char*>(malloc(RESULT_MAX));
  if (!ev) return;   // the state is still in job.get and job.list
  const uint32_t ts = m->_mesh->getRTCClock()->getCurrentTime();
  int n = m->_jobs.writeEvent(ev, RESULT_MAX, m->_epoch, ts >= 1704067200, ts, j);
  if (n > 0) n = m->_prov.sign("job", ev, (size_t)n, RESULT_MAX);
  if (n > 0) {
    for (Link& l : m->_links) {
      if (!l.src || !l.may_read || !l.src->isConnected() || !m->ensureQueue(l)) continue;
      l.q->push(Iface::OutQueue::EVENT, reinterpret_cast<const uint8_t*>(ev), (size_t)n);
    }
    if (m->_mqtt_may_read && m->_ev_publish && m->_ev_up && m->_ev_up()) m->_ev_publish("job", ev, (size_t)n);
  }
  free(ev);
}

bool MeconIface::jobClasses(void* user, uint8_t cls) {
  const MeconIface* m = static_cast<MeconIface*>(user);
  for (uint8_t c = 0; c < Cli::C_COUNT; c++)
    if ((cls & (1u << c)) && !Cli::decide(m->_ctx, c).allow) return false;
  return true;
}

namespace {

bool profileExists(void* user, uint8_t index) {
#ifdef ENABLE_MQTT
  return static_cast<MyMesh*>(user)->getNodePrefs()->mqtt_profiles.configured(index);
#else
  (void)user; (void)index;
  return false;
#endif
}

MeconIface* self(OpContext& c) { return static_cast<MeconIface*>(c.user); }

}  // namespace

// ---- boot

void MeconIface::deployLoad() {
  _copies.set(_mesh->_store);
  DeployAuth::Env env;
  env.store = &_copies;
  env.node_key = _mesh->self_id.pub_key;
  env.profile_exists = profileExists;
  env.user = _mesh;
  env.now_s = [](void* u) -> uint32_t { return static_cast<MyMesh*>(u)->getRTCClock()->getCurrentTime(); };
  _auth.begin(env);
}

void MeconIface::writeDeploymentStatus(JsonWriter& w, bool hardened) { _auth.writeStatus(w, hardened); }

// identity.rotate (AUTHORITY.md section 11). The result is signed with the key being retired (dispatch() does it from
// _old_id), so the manager that enrolled that key can verify the answer and learn the new one from it.
Status MeconIface::opIdentityRotate(OpContext& c) {
  MeconIface* m = self(c);
  Status st = m->_auth.rotateGate(c);
  if (st != ST_OK) return st;
  char oldhex[2 * PUB_KEY_SIZE + 1], newhex[2 * PUB_KEY_SIZE + 1];
  static const char H[] = "0123456789abcdef";
  const uint8_t* ok = m->_mesh->self_id.pub_key;
  for (int i = 0; i < PUB_KEY_SIZE; i++) { oldhex[2 * i] = H[ok[i] >> 4]; oldhex[2 * i + 1] = H[ok[i] & 15]; }
  oldhex[2 * PUB_KEY_SIZE] = 0;
  mesh::LocalIdentity* old = new (std::nothrow) mesh::LocalIdentity(m->_mesh->self_id);
  if (!old) { c.error = "no-memory"; return ST_BUSY; }
  const char* err = "failed";
  uint32_t job = 0;
  {
    int64_t j;
    if (c.doc && jsonGetInt(*c.doc, jsonFind(*c.doc, 0, "job_id"), 0, 0xFFFFFFFFLL, &j)) job = (uint32_t)j;
  }
  const int r = m->_mesh->rotateIdentity(&err, m->deviceIdHex(), job);
  if (r == 0) { Iface::wipe(old, sizeof(*old)); delete old; c.error = err; return ST_FAILED; }
  m->_old_id = old;   // signs this result; dispatch() wipes it
  if (r == 2) { c.error = "contacts_not_dropped"; return ST_FAILED; }
  const uint8_t* nk = m->_mesh->self_id.pub_key;
  for (int i = 0; i < PUB_KEY_SIZE; i++) { newhex[2 * i] = H[nk[i] >> 4]; newhex[2 * i + 1] = H[nk[i] & 15]; }
  newhex[2 * PUB_KEY_SIZE] = 0;
  JsonWriter& w = *c.out;
  w.key("old_public_key"); w.str(oldhex);
  w.key("new_public_key"); w.str(newhex);
  w.key("removed"); w.beginArray(); w.str("node_key"); w.str("contacts"); w.str("message_queue"); w.endArray();
  w.key("kept"); w.beginArray();
  w.str("configuration"); w.str("record"); w.str("channels"); w.str("posture"); w.str("device_id");
  w.endArray();
  return ST_OK;
}

// The statement the old key signed before the swap (AUTHORITY.md section 11): {"statement":"<signed text>","sig":"<base64>"}.
// Written as members of the current object; false when there is none.
static bool writeRotationMember(JsonWriter& w, MyMesh* mesh, const char* key) {
  char text[256], sig[89];
  if (!mesh->readRotation(text, sizeof(text), sig, sizeof(sig))) return false;
  w.key(key);
  w.beginObject();
  w.key("statement"); w.str(text);
  w.key("sig"); w.str(sig);
  w.endObject();
  return true;
}

// identity.rotation: a plain read. The stored statement, or an empty result when the key was never rotated.
Status MeconIface::opIdentityRotation(OpContext& c) {
  writeRotationMember(*c.out, self(c)->_mesh, "rotation");
  return ST_OK;
}

// The stored set is an open read; the rotation statement in it names device_id, so only a caller the table lets read
// gets it (AUTHORITY.md section 3.5: an untrusted link never learns device_id).
Status MeconIface::opDeployGet(OpContext& c) {
  const Status st = self(c)->_auth.opGet(c);
  if (st == ST_OK && Cli::decide(self(c)->_ctx, Cli::C_READ).allow) writeRotationMember(*c.out, self(c)->_mesh, "rotation");
  return st;
}

const char* MeconIface::helloNonce() { return _auth.helloNonce(nonceOf(_cur_link)); }

// ---- signed requests and operations

struct MeconIface::SignedRun {
  MeconIface* m;
  uint8_t caller;
  Link* link;
};

int MeconIface::runSigned(void* user, const Cli::Context& ctx, const char* body, size_t len, char* out, size_t cap) {
  SignedRun* r = static_cast<SignedRun*>(user);
  return r->m->runDispatch(ctx, r->caller, r->link, body, len, out, cap, nullptr);   // job ids guard replay; no id cache
}

int MeconIface::dispatchSigned(const Cli::Context& uctx, uint8_t caller, Link* link, const char* msg, size_t len,
                               char* out, size_t cap) {
  SignedRun run = { this, caller, link };
  return _auth.dispatchSigned(uctx, nonceOf(link), msg, len, _cache, runSigned, &run, out, cap);
}

Status MeconIface::opDeployProvision(OpContext& c) { return self(c)->_auth.opProvision(c); }
Status MeconIface::opDeployUpdate(OpContext& c) { return self(c)->_auth.opUpdate(c); }
Status MeconIface::opDeployReset(OpContext& c) { return self(c)->_auth.opReset(c); }
Status MeconIface::opDeploySetUnlock(OpContext& c) { return self(c)->_auth.opSetUnlock(c); }
Status MeconIface::opDeployUnlockChallenge(OpContext& c) { return self(c)->_auth.opUnlockChallenge(c); }
Status MeconIface::opDeployUnlock(OpContext& c) { return self(c)->_auth.opUnlock(c); }

void MeconIface::onNewSession(const BaseSerialInterface* src) {
  Link* l = linkFor(src, false);
  if (l) l->unlock.clear();
}

bool MeconIface::unlockedFor(const BaseSerialInterface* src) {
  Link* l = linkFor(src, false);
  return l && _auth.unlocked(&l->unlock);
}

#endif  // ENABLE_MECON_IFACE
