#pragma once
// The Deployment record's files on a platform file system, crash-safe (AtomicFile). Used by the Companion's DataStore and
// by the Repeater and Room server directly. The caller includes the platform file system header first (IdentityStore.h
// does); it is not built in the native tests. Header-only, so a build without the deployment sources links it.
#include "DepStore.h"
#include <Arduino.h>
#include "DepRecord.h"
#include "helpers/AtomicFile.h"
#include "helpers/IdentityStore.h"

namespace Dep {

class FsStore : public CopyStore {
public:
  explicit FsStore(FILESYSTEM* fs = nullptr) : _fs(fs) {}
  void setFs(FILESYSTEM* fs) { _fs = fs; }
  bool read(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, StoreFile which) override;
  bool write(int slot, const uint8_t* frame, size_t len, StoreFile which) override;
  bool removeBoth(int newer_slot, StoreFile which) override;
private:
  FILESYSTEM* _fs;
};



inline const char* fsPathOf(int slot, StoreFile which) {
  if (which == SF_MARKS) return slot == 0 ? "/mecon_depm_a" : "/mecon_depm_b";
  return slot == 0 ? "/mecon_dep_a" : "/mecon_dep_b";
}

inline File fsOpenRead(FILESYSTEM* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(path, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(path, "r");
#else
  return fs->open(path, "r", false);
#endif
}

inline bool FsStore::read(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, StoreFile which) {
  const char* path = fsPathOf(slot, which);
  *len = 0;
  *exists = false;
  if (!_fs) return false;
  AtomicFile::recover(_fs, path);
  *exists = AtomicFile::exists(_fs, path);
  if (!*exists) return false;
  File file = fsOpenRead(_fs, path);
  if (!file) return false;
  size_t size = file.size();
  bool ok = size <= cap && file.read(buf, size) == (int)size;
  file.close();
  if (ok) *len = size;
  return ok;
}

inline bool FsStore::write(int slot, const uint8_t* frame, size_t len, StoreFile which) {
  if (!_fs) return false;
  return AtomicFile::save(_fs, fsPathOf(slot, which), [frame, len](Stream& s) { return s.write(frame, len) == len; });
}

inline bool FsStore::removeBoth(int newer_slot, StoreFile which) {
  if (!_fs) return false;
  // With any .tmp / .new beside them: a leftover verified .new would otherwise be swapped back in at the next boot by
  // AtomicFile::recover and resurrect the record a reset removed. The older copy goes first, so a power cut between the
  // two deletions leaves the newer one rather than bringing the older back.
  int order[2];
  removalOrder(newer_slot, order);
  AtomicFile::discard(_fs, fsPathOf(order[0], which));
  AtomicFile::discard(_fs, fsPathOf(order[1], which));
  return !AtomicFile::exists(_fs, fsPathOf(0, which)) && !AtomicFile::exists(_fs, fsPathOf(1, which));
}

}  // namespace Dep
