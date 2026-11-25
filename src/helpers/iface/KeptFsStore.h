#pragma once
// The files of the kept reports (KeptReports.h) on a platform file system: one file per segment, each only ever appended to,
// and a small meta file replaced atomically (AtomicFile). Header-only like PathFsStore; not built in the native tests. The
// caller includes the platform file system header first.
#include "KeptReports.h"
#include "helpers/deployment/DepFsStore.h"

namespace Iface {

class KeptFsStore : public KeptStore {
public:
  void setFs(FILESYSTEM* fs) { _fs = fs; }

  size_t size(int slot) override {
    char path[12];
    pathOf(slot, path);
    if (!_fs) return 0;
    if (slot == META) AtomicFile::recover(_fs, path);   // finish or drop a meta save a power cut stopped
    if (!AtomicFile::exists(_fs, path)) return 0;
    File f = Dep::fsOpenRead(_fs, path);
    if (!f) return 0;
    const size_t n = f.size();
    f.close();
    return n;
  }

  bool read(int slot, size_t off, uint8_t* buf, size_t n) override {
    char path[12];
    pathOf(slot, path);
    if (!_fs) return false;
    File f = Dep::fsOpenRead(_fs, path);
    if (!f) return false;
    bool ok = f.seek(off) && f.read(buf, n) == (int)n;
    f.close();
    return ok;
  }

  // One write: the file is opened for appending, so what is there is not copied (the packet log does the same).
  bool append(int slot, const uint8_t* buf, size_t n) override {
    char path[12];
    pathOf(slot, path);
    if (!_fs) return false;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = _fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File f = _fs->open(path, "a");
#else
    File f = _fs->open(path, "a", true);
#endif
    if (!f) return false;
    const bool ok = f.write(buf, n) == n;
    f.flush();
    f.close();
    return ok;
  }

  bool replace(int slot, const uint8_t* buf, size_t n) override {
    char path[12];
    pathOf(slot, path);
    if (!_fs) return false;
    return AtomicFile::save(_fs, path, [buf, n](Stream& s) { return s.write(buf, n) == n; });
  }

  void remove(int slot) override {
    char path[12];
    pathOf(slot, path);
    if (!_fs) return;
    if (slot == META) AtomicFile::discard(_fs, path);   // a meta save leaves .tmp and .new files
    else _fs->remove(path);
  }

  uint32_t freeBytes() override {
#if defined(ESP32)
    const size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
    return total > used ? (uint32_t)(total - used) : 0;
#else
    return 0xFFFFFFFFu;   // the Adafruit file system does not say; the write fails when it is full, and the report is counted dropped
#endif
  }

private:
  FILESYSTEM* _fs = nullptr;
  static void pathOf(int slot, char* out) {
    if (slot == META) { strcpy(out, "/krpm"); return; }
    strcpy(out, "/krp0");
    out[4] = (char)('0' + slot);
  }
};

}  // namespace Iface
