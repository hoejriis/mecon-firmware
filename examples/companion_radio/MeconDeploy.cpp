// The Deployment authority layer of the Companion's device interface (docs/contract/AUTHORITY.md). The layer itself is
// shared with the Repeater and Room server console and lives in src/helpers/iface/DeployAuth.cpp; this file is the
// Companion's glue to it: its DataStore as the record's file store, the boot load, and the forwarders the operation table
// and the rest of the Companion call.
#include "MeconIface.h"
#ifdef ENABLE_MECON_IFACE
#include "MyMesh.h"

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

Status MeconIface::opDeployGet(OpContext& c) { return self(c)->_auth.opGet(c); }
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
