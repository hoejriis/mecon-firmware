#pragma once

// Crash-safe replacement of a small file (prefs, identity, channel table).
//
// A save writes the complete new content to "<path>.tmp", flushes it, reads it back and compares
// length and CRC, and only then swaps it in. A power cut at any point leaves either the previous
// complete file or the new complete one, never a truncated file at <path>. The caller must have
// included the platform file system header (FS.h / Adafruit_LittleFS.h) before this header.
//
// Swap: LittleFS (nRF52, STM32, RP2040) renames over the existing file atomically. ESP32 SPIFFS does
// not replace an existing destination, so there the verified copy is first renamed to "<path>.new",
// the old file is removed, and "<path>.new" is renamed to <path>. A cut after the old file was
// removed leaves "<path>.new" (verified, complete); atomicRecover() finishes the swap at boot.

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "BootPresence.h"

#if defined(ESP32)
  #define ATOMIC_FILE_TWO_STEP_SWAP 1
#endif

namespace AtomicFile {

// Forwards writes to a file while counting bytes and keeping a CRC32 for the read-back check.
class TeeStream : public Stream {
  Stream& _out;
public:
  uint32_t crc = 0xFFFFFFFFu;
  size_t count = 0;
  bool failed = false;

  explicit TeeStream(Stream& out) : _out(out) { }

  static uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    while (n--) {
      crc ^= *p++;
      for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    size_t w = _out.write(buf, n);
    if (w != n) failed = true;
    crc = crcUpdate(crc, buf, w);
    count += w;
    return w;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override { _out.flush(); }
};

template <typename FS_T>
inline File openForWrite(FS_T* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(path);
  return fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(path, "w");
#else
  return fs->open(path, "w", true);
#endif
}

template <typename FS_T>
inline File openForRead(FS_T* fs, const char* path) {
#if defined(RP2040_PLATFORM)
  return fs->open(path, "r");
#else
  return fs->open(path);
#endif
}

inline void nameWith(char* dst, size_t n, const char* path, const char* suffix) {
  snprintf(dst, n, "%s%s", path, suffix);
}

// Call before reading <path> at boot: drops an unfinished temp file and completes an interrupted swap.
template <typename FS_T>
inline void recover(FS_T* fs, const char* path) {
  char tmp[48], nw[48];
  nameWith(tmp, sizeof(tmp), path, ".tmp");
  nameWith(nw, sizeof(nw), path, ".new");
  // At boot, a leftover that the one listing did not show is not looked up (BootPresence.h): almost always neither exists.
  if (BootPresence::mayExist(tmp) && fs->exists(tmp)) fs->remove(tmp);   // never verified: not promoted
  if (BootPresence::mayExist(nw) && fs->exists(nw)) {
    if (!fs->exists(path)) {
      fs->rename(nw, path);   // verified copy; the cut came after the old file was removed
      BootPresence::note(path);
    } else {
      fs->remove(nw);
    }
  }
}

// fs->exists(path), except that at boot a file the one listing did not show is not looked up (BootPresence.h).
template <typename FS_T>
inline bool exists(FS_T* fs, const char* path) {
  return BootPresence::mayExist(path) && fs->exists(path);
}

// Removes <path> together with any leftover temp or swap file: for a file that is deleted on purpose.
template <typename FS_T>
inline void discard(FS_T* fs, const char* path) {
  char tmp[48], nw[48];
  nameWith(tmp, sizeof(tmp), path, ".tmp");
  nameWith(nw, sizeof(nw), path, ".new");
  if (fs->exists(tmp)) fs->remove(tmp);
  if (fs->exists(nw)) fs->remove(nw);
  if (fs->exists(path)) fs->remove(path);
}

// writer: bool(Stream&) writes the complete content. Returns true once the new file is in place.
template <typename FS_T, typename WRITER>
inline bool save(FS_T* fs, const char* path, WRITER writer) {
  char tmp[48], nw[48];
  nameWith(tmp, sizeof(tmp), path, ".tmp");
  nameWith(nw, sizeof(nw), path, ".new");
  if (BootPresence::mayExist(tmp) && fs->exists(tmp)) fs->remove(tmp);
  if (BootPresence::mayExist(nw) && fs->exists(nw)) fs->remove(nw);

  uint32_t want_crc;
  size_t want_len;
  {
    File f = openForWrite(fs, tmp);
    if (!f) return false;
    TeeStream tee(f);
    bool ok = writer(tee) && !tee.failed;
    f.flush();
    f.close();
    want_crc = tee.crc;
    want_len = tee.count;
    if (!ok) { fs->remove(tmp); return false; }
  }
  {
    File r = openForRead(fs, tmp);   // read back what actually reached the file system
    if (!r) { fs->remove(tmp); return false; }
    uint32_t crc = 0xFFFFFFFFu;
    size_t len = 0;
    uint8_t buf[64];
    for (;;) {
      int n = r.read(buf, sizeof(buf));
      if (n <= 0) break;
      crc = TeeStream::crcUpdate(crc, buf, n);
      len += n;
    }
    r.close();
    if (len != want_len || crc != want_crc) { fs->remove(tmp); return false; }
  }
#ifdef ATOMIC_FILE_TWO_STEP_SWAP
  if (!fs->rename(tmp, nw)) { fs->remove(tmp); return false; }
  if (BootPresence::mayExist(path)) fs->remove(path);   // SPIFFS does not replace an existing destination
  bool ok = fs->rename(nw, path);   // if cut here, recover() completes it from <path>.new
  if (ok) BootPresence::note(path);
  return ok;
#else
  if (!fs->rename(tmp, path)) { fs->remove(tmp); return false; }
  return true;
#endif
}

}  // namespace AtomicFile
