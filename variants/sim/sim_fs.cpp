// SimFS: the board filesystem (ESP32 SPIFFS API) mapped to a host directory,
// with a power-cut fault hook for storage-integrity tests.
#include "shims/SPIFFS.h"
#include "sim_config.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <algorithm>

fs::SPIFFSFS SPIFFS;

namespace fs {

// Call counts for tests that bound the file system work of a boot (--fs-calls FILE): the file holds
// "exists=N open=N remove=N rename=N missing=N", rewritten after each call. A real SPIFFS scans its file table on each
// call, and a lookup that finds nothing scans all of it (about 0.9 s per lookup on a Heltec V4's 3.4 MB partition), so
// "missing" counts the exists/open/remove calls that found no file, and each one is logged as "SIMFS missing <path>".
static unsigned long g_calls[5];
static void write_calls() {
  if (g_sim.fs_calls.empty()) return;
  if (FILE* f = fopen(g_sim.fs_calls.c_str(), "w")) {
    fprintf(f, "exists=%lu open=%lu remove=%lu rename=%lu missing=%lu\n", g_calls[0], g_calls[1], g_calls[2], g_calls[3],
            g_calls[4]);
    fclose(f);
  }
}
static void count_call(int which) { g_calls[which]++; write_calls(); }
static void count_missing(const char* path) {
  g_calls[4]++;
  if (!g_sim.fs_calls.empty()) fprintf(stderr, "SIMFS missing %s\n", path ? path : "");
  write_calls();
}

FileImpl::~FileImpl() { if (fp) fclose(fp); }

static void mkdirs(const std::string& path) {
  for (size_t i = 1; i < path.size(); i++) {
    if (path[i] == '/') {
      std::string p = path.substr(0, i);
      ::mkdir(p.c_str(), 0777);
    }
  }
}

bool FS::setRoot(const char* d) { _root = d; while (_root.size() > 1 && _root.back() == '/') _root.pop_back(); return true; }

std::string FS::hostPath(const char* path) const {
  std::string p = path ? path : "";
  if (p.empty() || p[0] != '/') p = "/" + p;
  return _root + p;
}

File FS::open(const char* path, const char* mode, const bool create) {
  count_call(1);
  std::string hp = hostPath(path);
  bool writing = mode && (mode[0] == 'w' || mode[0] == 'a');
  struct stat st;
  auto impl = std::make_shared<FileImpl>();
  impl->host_path = hp; impl->fs_path = path; impl->owner = this;
  if (stat(hp.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    impl->is_dir = true;
    if (DIR* d = opendir(hp.c_str())) {
      while (dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) impl->entries.push_back(e->d_name);
      }
      closedir(d);
      std::sort(impl->entries.begin(), impl->entries.end());
    }
    return File(impl);
  }
  if (writing) mkdirs(hp);
  impl->fp = fopen(hp.c_str(), mode && mode[0] == 'w' ? "w+b" : mode && mode[0] == 'a' ? "ab+" : "rb");
  if (!impl->fp) { count_missing(path); return File(); }
  return File(impl);
}

bool FS::exists(const char* path) {
  count_call(0);
  struct stat st;
  if (stat(hostPath(path).c_str(), &st) == 0) return true;
  count_missing(path);
  return false;
}
// Power cut just before a filesystem operation (--fault-before OP:PATH).
static void fault_before(const char* op, const char* path) {
  if (!g_sim.fault_op.empty() && g_sim.fault_op == op && g_sim.fault_op_path == (path ? path : "")) {
    fprintf(stderr, "SIM_FAULT: power cut before %s of %s\n", op, path);
    _exit(99);
  }
}
bool FS::remove(const char* path) {
  count_call(2);
  fault_before("remove", path);
  if (::unlink(hostPath(path).c_str()) == 0) return true;
  count_missing(path);
  return false;
}
// Like ESP32 SPIFFS (not like POSIX or littlefs), rename does not replace an existing destination:
// the simulated board must not let firmware rely on rename-over.
bool FS::rename(const char* a, const char* b) {
  count_call(3);
  fault_before("rename", a);
  std::string hb = hostPath(b);
  struct stat st;
  if (stat(hb.c_str(), &st) == 0) return false;
  mkdirs(hb);
  return ::rename(hostPath(a).c_str(), hb.c_str()) == 0;
}
bool FS::mkdir(const char* path) { std::string h = hostPath(path); mkdirs(h); return ::mkdir(h.c_str(), 0777) == 0 || errno == EEXIST; }
bool FS::rmdir(const char* path) { return ::rmdir(hostPath(path).c_str()) == 0; }

// ------------------------------------------------------------------ File
size_t File::write(uint8_t b) { return write(&b, 1); }

size_t File::write(const uint8_t* buf, size_t n) {
  if (!_p || !_p->fp) return 0;
  // Fault hook: after N bytes written to the named file, persist exactly those
  // N bytes and kill the process without closing anything (a power cut).
  if (g_sim.fault_after >= 0 && _p->fs_path == g_sim.fault_path) {
    long room = g_sim.fault_after - (long)_p->written;
    if ((long)n > room) {
      if (room > 0) fwrite(buf, 1, room, _p->fp);
      fflush(_p->fp);
      fprintf(stderr, "SIM_FAULT: power cut after %ld bytes of %s\n", g_sim.fault_after, _p->fs_path.c_str());
      _exit(99);
    }
  }
  size_t w = fwrite(buf, 1, n, _p->fp);
  fflush(_p->fp);   // every write reaches the host file, so a kill leaves it as-is
  _p->written += w;
  return w;
}

int File::available() {
  if (!_p || !_p->fp) return 0;
  long cur = ftell(_p->fp);
  fseek(_p->fp, 0, SEEK_END); long end = ftell(_p->fp); fseek(_p->fp, cur, SEEK_SET);
  return (int)(end - cur);
}
int File::read() { if (!_p || !_p->fp) return -1; return fgetc(_p->fp); }
size_t File::read(uint8_t* buf, size_t n) { return (_p && _p->fp) ? fread(buf, 1, n, _p->fp) : 0; }
int File::peek() { if (!_p || !_p->fp) return -1; int c = fgetc(_p->fp); if (c != EOF) ungetc(c, _p->fp); return c; }
void File::flush() { if (_p && _p->fp) fflush(_p->fp); }
size_t File::size() const {
  if (!_p || !_p->fp) return 0;
  struct stat st; fflush(_p->fp);
  return fstat(fileno(_p->fp), &st) == 0 ? st.st_size : 0;
}
size_t File::position() const { return (_p && _p->fp) ? ftell(_p->fp) : 0; }
bool File::seek(uint32_t pos) { return _p && _p->fp && fseek(_p->fp, pos, SEEK_SET) == 0; }
void File::close() { if (_p && _p->fp) { fclose(_p->fp); _p->fp = nullptr; } }
const char* File::path() const { return _p ? _p->fs_path.c_str() : ""; }
const char* File::name() const {
  if (!_p) return "";
  const char* s = strrchr(_p->fs_path.c_str(), '/');
  return s ? s + 1 : _p->fs_path.c_str();
}
File File::openNextFile(const char* mode) {
  if (!_p || !_p->is_dir || _p->next_entry >= _p->entries.size()) return File();
  std::string child = _p->fs_path;
  if (child.empty() || child.back() != '/') child += "/";
  child += _p->entries[_p->next_entry++];
  return _p->owner ? const_cast<FS*>(_p->owner)->open(child.c_str(), mode) : File();
}
void File::rewindDirectory() { if (_p) _p->next_entry = 0; }

// ------------------------------------------------------------------ SPIFFS
bool SPIFFSFS::begin(bool, const char*, uint8_t, const char*) {
  setRoot(g_sim.dir.c_str());
  ::mkdir(_root.c_str(), 0777);
  return true;
}

static void rmtree(const std::string& d, bool keep_root) {
  if (DIR* dir = opendir(d.c_str())) {
    while (dirent* e = readdir(dir)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      std::string p = d + "/" + e->d_name; struct stat st;
      if (lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) rmtree(p, false); else unlink(p.c_str());
    }
    closedir(dir);
  }
  if (!keep_root) rmdir(d.c_str());
}
bool SPIFFSFS::format() { rmtree(_root, true); return true; }

static size_t treesize(const std::string& d) {
  size_t total = 0;
  if (DIR* dir = opendir(d.c_str())) {
    while (dirent* e = readdir(dir)) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      std::string p = d + "/" + e->d_name; struct stat st;
      if (lstat(p.c_str(), &st) != 0) continue;
      total += S_ISDIR(st.st_mode) ? treesize(p) : (size_t)st.st_size;
    }
    closedir(dir);
  }
  return total;
}
size_t SPIFFSFS::totalBytes() { return g_sim.fs_size; }
size_t SPIFFSFS::usedBytes() { return treesize(_root); }

}  // namespace fs

// Free heap the simulated board reports at boot (see helpers/ContactPlatform.h).
uint32_t sim_free_heap() { return g_sim.heap_budget; }

// The heap the node reports now (MemStats): the boot budget until the test writes --heap-file, then "FREE [LARGEST]"
// from that file (LARGEST defaults to FREE). The file is read at most every 100 ms of node time. The contact store
// keeps sizing itself from the boot budget above.
void sim_heap_now(uint32_t& free, uint32_t& largest) {
  static unsigned long next = 0;
  static uint32_t f = 0, l = 0;
  static bool init = false;
  if (!init) { f = l = g_sim.heap_budget; init = true; }
  if (!g_sim.heap_file.empty()) {
    unsigned long now = millis();
    if (next == 0 || now >= next) {
      next = now + 100;
      if (FILE* fp = fopen(g_sim.heap_file.c_str(), "r")) {
        unsigned long a = 0, b = 0;
        int n = fscanf(fp, "%lu %lu", &a, &b);
        fclose(fp);
        if (n >= 1) { f = (uint32_t)a; l = n >= 2 ? (uint32_t)b : (uint32_t)a; }
      }
    }
  }
  free = f; largest = l;
}
