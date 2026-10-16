#pragma once

// Persistence of the security audit log (AuditRing.h) in /audit.bin.
//
// Flash cost: one 268-byte file, saved through AtomicFile (temp file, read-back check, swap), so it exists
// twice for a moment and costs at most two small file-system blocks. Writes: a posture change or factory reset
// is written at once; every other event at most once per AuditRing::FLUSH_INTERVAL_MS (10 minutes), the first
// one after a boot at once, so even a sustained attack costs at most 144 writes a day. A power cut loses at
// most the events of the last interval (never the file: the previous complete log survives).
//
// The caller must have included the platform file system headers (IdentityStore.h does) before this header.

#include <helpers/IdentityStore.h>
#include "AtomicFile.h"
#include "Posture.h"

class AuditStore {
  FILESYSTEM* _fs = nullptr;
  static const char* path() { return "/audit.bin"; }

public:
  // Load the persisted log (an unreadable or damaged file leaves it empty).
  void begin(FILESYSTEM* fs) {
    _fs = fs;
    Posture::AuditRing& ring = Posture::audit();
    ring.clear();
    AtomicFile::recover(fs, path());
    if (!fs->exists(path())) return;
    uint8_t buf[Posture::AuditRing::FILE_SIZE + 1];
    size_t got = 0;
    File f = AtomicFile::openForRead(fs, path());
    if (f) {
      int n;
      while (got < sizeof(buf) && (n = f.read(buf + got, sizeof(buf) - got)) > 0) got += n;
      f.close();
    }
    ring.deserialize(buf, got);
  }

  // Call from the main loop.
  void service(uint32_t now_ms) {
    if (_fs && Posture::audit().shouldFlush(now_ms)) flush(now_ms);
  }

  // Write the log now (after a file-system format erased it, for instance).
  void flush(uint32_t now_ms) {
    if (!_fs) return;
    Posture::AuditRing& ring = Posture::audit();
    uint8_t buf[Posture::AuditRing::FILE_SIZE];
    ring.serialize(buf);
    bool ok = AtomicFile::save(_fs, path(), [&](Stream& s) { return s.write(buf, sizeof(buf)) == sizeof(buf); });
    ring.markFlushed(now_ms, ok);
  }
};

inline AuditStore& auditStore() {
  static AuditStore s;
  return s;
}
