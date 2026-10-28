// The Deployment authority layer of the Companion's device interface (docs/contract/AUTHORITY.md). The layer itself is
// shared with the Repeater and Room server console and lives in src/helpers/iface/DeployAuth.cpp; this file is the
// Companion's glue to it: its DataStore as the record's file store, the boot load, and the forwarders the operation table
// and the rest of the Companion call.
#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"
#include <new>

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
  if (r == 0) { memset(old, 0, sizeof(*old)); delete old; c.error = err; return ST_FAILED; }
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

Status MeconIface::opDeployGet(OpContext& c) {
  const Status st = self(c)->_auth.opGet(c);
  if (st == ST_OK) writeRotationMember(*c.out, self(c)->_mesh, "rotation");
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
