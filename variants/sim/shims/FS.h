// ESP32-flavoured filesystem API (fs::FS / fs::File) backed by a host directory.
// Flat-namespace semantics like SPIFFS: opening a file for write creates any
// missing parent directories. See sim_fs.cpp for the fault-injection hook.
#pragma once
#include <Arduino.h>
#include <memory>
#include <string>
#include <vector>
#include <stdio.h>

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

namespace fs {

class FS;

struct FileImpl {
  FILE* fp = nullptr;
  std::string host_path;   // absolute host path
  std::string fs_path;     // path as the application sees it
  bool is_dir = false;
  std::vector<std::string> entries;  // directory listing
  size_t next_entry = 0;
  size_t written = 0;      // bytes written through this handle (fault hook)
  const FS* owner = nullptr;
  ~FileImpl();
};

class File : public Stream {
  std::shared_ptr<FileImpl> _p;
public:
  File() {}
  explicit File(std::shared_ptr<FileImpl> p) : _p(p) {}
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override;
  int read() override;
  size_t read(uint8_t* buf, size_t n);
  int peek() override;
  void flush() override;
  size_t size() const;
  size_t position() const;
  bool seek(uint32_t pos);
  void close();
  const char* name() const;
  const char* path() const;
  bool isDirectory() const { return _p && _p->is_dir; }
  File openNextFile(const char* mode = "r");
  void rewindDirectory();
  operator bool() const { return _p && (_p->fp || _p->is_dir); }
};

class FS {
protected:
  std::string _root;
public:
  bool setRoot(const char* host_dir);
  const std::string& root() const { return _root; }
  std::string hostPath(const char* path) const;
  File open(const char* path, const char* mode = "r", const bool create = false);
  File open(const std::string& path, const char* mode = "r", const bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* from, const char* to);
  bool mkdir(const char* path);
  bool rmdir(const char* path);
};

}  // namespace fs

using fs::File;
