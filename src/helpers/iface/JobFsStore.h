#pragma once
// The job table's file (JobService.h) on a platform file system, replaced atomically (AtomicFile). Used by the Companion
// and by the Repeater and Room server. Header-only, like Dep::FsStore beside it; not built in the native tests.
#include "JobService.h"
#include "helpers/deployment/DepFsStore.h"

namespace Iface {

class JobFsStore : public JobStore {
public:
  void setFs(FILESYSTEM* fs) { _fs = fs; }
  size_t load(uint8_t* buf, size_t cap) override {
    if (!_fs) return 0;
    AtomicFile::recover(_fs, PATH);   // finish or drop a save that was cut by a power loss
    if (!AtomicFile::exists(_fs, PATH)) return 0;
    File f = Dep::fsOpenRead(_fs, PATH);
    if (!f) return 0;
    const size_t size = f.size();
    const bool ok = size <= cap && f.read(buf, size) == (int)size;
    f.close();
    return ok ? size : 0;
  }
  bool save(const uint8_t* buf, size_t len) override {
    if (!_fs) return false;
    return AtomicFile::save(_fs, PATH, [buf, len](Stream& s) { return s.write(buf, len) == len; });
  }
private:
  static constexpr const char* PATH = "/mecon_jobs";
  FILESYSTEM* _fs = nullptr;
};

}  // namespace Iface
