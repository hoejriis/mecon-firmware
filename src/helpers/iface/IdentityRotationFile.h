#pragma once
// The rotation statement's file on a role with no DataStore (Repeater, Room server): the same path, and the same atomic
// save, as the Companion's. On nRF52 the recovery at boot runs on the worker, for the same reason as the Companion's: two
// 512 B buffers and a save through LittleFS are deeper than the loop task has room for.
#include <helpers/AtomicFile.h>
#include <helpers/NrfWorker.h>
#include "IdentityRotation.h"

namespace Iface {

template <typename FS_T>
class RoleRotation {
public:
  void begin(FS_T* fs, mesh::LocalIdentity* self, bool (*save_key)(void*, const mesh::LocalIdentity&), bool (*drop)(void*),
             void* user) {
    _fs = fs;
    _self = self;
    _save_key = save_key;
    _drop = drop;
    _user = user;
  }
  __attribute__((noinline)) void recoverAtBoot() {
    if (!NrfWorker::run(recoverJob, this, 30000)) recoverOnLoop();
  }
  int run(const char* device_id_hex, uint32_t job_id, const char** err) {
    return rotationRun(store(), *_self, device_id_hex, job_id, err);
  }
  bool read(char* text, size_t tcap, char* sig_b64, size_t scap) { return rotationRead(store(), text, tcap, sig_b64, scap); }

private:
  RotationStore store() { return RotationStore{loadFile, saveFile, removeFile, saveKey, drop, this}; }
  static bool saveKey(void* u, const mesh::LocalIdentity& id) {
    RoleRotation* r = static_cast<RoleRotation*>(u);
    return r->_save_key(r->_user, id);
  }
  static bool drop(void* u) {
    RoleRotation* r = static_cast<RoleRotation*>(u);
    return r->_drop(r->_user);
  }
  __attribute__((noinline)) static void recoverJob(void* self) {
    RoleRotation* r = static_cast<RoleRotation*>(self);
    rotationRecover(r->store(), *r->_self);
  }
  __attribute__((noinline)) void recoverOnLoop() { rotationRecover(store(), *_self); }   // no worker: a degraded mode
  static size_t loadFile(void* u, uint8_t* buf, size_t cap) {
    FS_T* fs = static_cast<RoleRotation*>(u)->_fs;
    AtomicFile::recover(fs, "/rotation");   // finish or drop a save that was cut by a power loss
    if (!AtomicFile::exists(fs, "/rotation")) return 0;
    File f = AtomicFile::openForRead(fs, "/rotation");
    if (!f) return 0;
    const int n = f.read(buf, cap);
    f.close();
    return n > 0 ? (size_t)n : 0;
  }
  static bool saveFile(void* u, const uint8_t* buf, size_t len) {
    return AtomicFile::save(static_cast<RoleRotation*>(u)->_fs, "/rotation", [buf, len](Stream& s) { return s.write(buf, len) == len; });
  }
  static void removeFile(void* u) { AtomicFile::discard(static_cast<RoleRotation*>(u)->_fs, "/rotation"); }

  FS_T* _fs = nullptr;
  mesh::LocalIdentity* _self = nullptr;
  bool (*_save_key)(void*, const mesh::LocalIdentity&) = nullptr;
  bool (*_drop)(void*) = nullptr;
  void* _user = nullptr;
};

}  // namespace Iface
