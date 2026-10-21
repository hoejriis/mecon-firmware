#pragma once
// Which files exist, from one listing of the file system at boot, so that boot never looks up a file that is not there.
//
// ESP32 SPIFFS is flat and scans its file table on every lookup, and a lookup that finds nothing scans all of it:
// about 0.9 s per lookup on a Heltec V4's 3.4 MB partition. A steady-state Companion boot made 15 such lookups (the
// .tmp/.new leftovers that AtomicFile::recover() checks for, and data files not yet written), which put
// the_mesh.begin() at 9.8 s. Between begin() and end(), mayExist(path) answers false for a file that was not in the
// listing, and the boot-time lookups (AtomicFile::recover(), AtomicFile::exists()) skip it.
//
// Safe direction: names are compared by their last path part, so a file listed under any directory counts, and an
// unknown answer is always "may exist" (look it up as before): outside begin()..end(), on a platform other than ESP32,
// when the listing failed, or when it held more names than fit. A file written during boot is added with note().
#include <new>
#include <stdint.h>
#include <string.h>

namespace BootPresence {

static const int MAX_NAMES = 160;
static const int NAME_LEN = 32;

struct State {
  char (*names)[NAME_LEN] = nullptr;
  int count = 0;
  bool active = false;
};

inline State& state() {
  static State s;
  return s;
}

inline const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

// Records a name; false when the table is full (the listing is then not trusted).
inline bool note(const char* path) {
  State& s = state();
  if (!s.names) return false;
  const char* b = baseName(path);
  for (int i = 0; i < s.count; i++) if (strncmp(s.names[i], b, NAME_LEN - 1) == 0) return true;
  if (s.count >= MAX_NAMES) { s.active = false; return false; }
  strncpy(s.names[s.count], b, NAME_LEN - 1);
  s.names[s.count][NAME_LEN - 1] = 0;
  s.count++;
  return true;
}

inline bool mayExist(const char* path) {
  State& s = state();
  if (!s.active) return true;
  const char* b = baseName(path);
  for (int i = 0; i < s.count; i++) if (strncmp(s.names[i], b, NAME_LEN - 1) == 0) return true;
  return false;
}

inline void end() {
  State& s = state();
  s.active = false;
  delete[] s.names;
  s.names = nullptr;
  s.count = 0;
}

// Lists the file system once: the root, and one level below it (LittleFS and the simulated board keep the identity in
// /identity/; SPIFFS is flat and lists every file at the root).
template <typename FS_T>
inline void begin(FS_T& fs) {
#if defined(ESP32)
  end();
  State& s = state();
  s.names = new (std::nothrow) char[MAX_NAMES][NAME_LEN];
  if (!s.names) return;
  auto root = fs.open("/", "r");
  if (!root || !root.isDirectory()) { end(); return; }
  bool complete = true;
  for (auto f = root.openNextFile(); f && complete; f = root.openNextFile()) {
    if (f.isDirectory()) {
      for (auto g = f.openNextFile(); g && complete; g = f.openNextFile()) complete = note(g.name());
    } else {
      complete = note(f.name());
    }
  }
  if (!complete) { end(); return; }
  s.active = true;
#else
  (void)fs;
#endif
}

}  // namespace BootPresence
