#pragma once
#include "FS.h"

namespace fs {
class SPIFFSFS : public FS {
public:
  // Mounts the per-node host directory chosen by the runner (SIM_DIR).
  bool begin(bool formatOnFail = false, const char* basePath = "/spiffs", uint8_t maxOpenFiles = 10, const char* partitionLabel = nullptr);
  bool format();
  size_t totalBytes();
  size_t usedBytes();
  void end() {}
};
}  // namespace fs
extern fs::SPIFFSFS SPIFFS;
