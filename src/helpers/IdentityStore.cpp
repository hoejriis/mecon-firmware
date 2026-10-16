#include "IdentityStore.h"
#include "AtomicFile.h"

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  _damaged = false;
  AtomicFile::recover(_fs, filename);   // finish or drop a save that was cut by a power loss
  if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "r");
#else
    File file = _fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);
      file.close();
    }
    if (!loaded) keepDamaged(filename);
  }
  return loaded;
}

bool IdentityStore::load(const char *name, mesh::LocalIdentity& id, char display_name[], int max_name_sz) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  _damaged = false;
  AtomicFile::recover(_fs, filename);   // finish or drop a save that was cut by a power loss
  if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "r");
#else
    File file = _fs->open(filename);
#endif
    if (file) {
      loaded = id.readFrom(file);

      int n = max_name_sz;   // up to 32 bytes
      if (n > 32) n = 32;
      file.read((uint8_t *) display_name, n);
      display_name[n - 1] = 0;  // ensure null terminator

      file.close();
    }
    if (!loaded) keepDamaged(filename);
  }
  return loaded;
}

// A file that exists but cannot be read back as an identity is never overwritten by the fresh
// identity the caller will generate next: it is kept as <file>.damaged so it can be recovered.
void IdentityStore::keepDamaged(const char* filename) {
  char damaged[48];
  snprintf(damaged, sizeof(damaged), "%s.damaged", filename);
  _fs->remove(damaged);
  _fs->rename(filename, damaged);
  _damaged = true;
  MESH_DEBUG_PRINTLN("IdentityStore: %s is damaged, kept as %s", filename, damaged);
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  bool ok = AtomicFile::save(_fs, filename, [&id](Stream& s) { return id.writeTo(s); });
  MESH_DEBUG_PRINTLN("IdentityStore::save() write - %s", ok ? "OK" : "Err");
  return ok;
}

bool IdentityStore::save(const char *name, const mesh::LocalIdentity& id, const char display_name[]) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  return AtomicFile::save(_fs, filename, [&id, display_name](Stream& s) {
    if (!id.writeTo(s)) return false;

    uint8_t tmp[32];
    memset(tmp, 0, sizeof(tmp));
    int n = strlen(display_name);
    if (n > sizeof(tmp)-1) n = sizeof(tmp)-1;
    memcpy(tmp, display_name, n);
    return s.write(tmp, sizeof(tmp)) == sizeof(tmp);
  });
}
