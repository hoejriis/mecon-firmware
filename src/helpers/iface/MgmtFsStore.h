#pragma once
// A small file replaced atomically (AtomicFile), at a path of its own: the management channel's binding record and
// the snapshot of its pending change (MgmtChannel.h). As SnapFsStore, with the path given. Header-only; not built in
// the native tests.
#include "ConfigSnap.h"
#include "helpers/deployment/DepFsStore.h"

namespace Iface {

class PathFsStore : public SnapStore {
public:
  explicit PathFsStore(const char* path) : _path(path) {}
  void setFs(FILESYSTEM* fs) { _fs = fs; }
  size_t load(uint8_t* buf, size_t cap) override {
    if (!_fs) return 0;
    AtomicFile::recover(_fs, _path);   // finish or drop a save that was cut by a power loss
    if (!AtomicFile::exists(_fs, _path)) return 0;
    File f = Dep::fsOpenRead(_fs, _path);
    if (!f) return 0;
    const size_t size = f.size();
    const bool ok = size <= cap && f.read(buf, size) == (int)size;
    f.close();
    return ok ? size : 0;
  }
  bool save(const uint8_t* buf, size_t len) override {
    if (!_fs) return false;
    return AtomicFile::save(_fs, _path, [buf, len](Stream& s) { return s.write(buf, len) == len; });
  }
  // Through discard, never a plain remove: a leftover verified .new would otherwise be swapped back in at the next boot.
  void discard() override {
    if (_fs) AtomicFile::discard(_fs, _path);
  }

private:
  const char* _path;
  FILESYSTEM* _fs = nullptr;
};

}  // namespace Iface
