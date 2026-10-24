#pragma once
// device_id and the boot epoch for the device interface (docs/contract/DEVICE_INTERFACE.md section 1), shared by
// every role. "/mecon_dev" holds the 16-byte device_id and a little-endian 32-bit epoch; a first boot or a damaged
// record draws a new device_id and starts the epoch from 0, and every boot increments it. The caller includes its
// platform file system header before this one (as for AtomicFile.h).
#include <Mesh.h>
#include <string.h>
#include "helpers/AtomicFile.h"

namespace Iface {

// Fills device_id and *epoch for this boot. False when the record could not be saved: this boot still has its
// values, the next one may repeat the epoch.
template <typename FS_T>
bool bootDeviceMeta(FS_T* fs, mesh::RNG* rng, uint8_t device_id[16], uint32_t* epoch) {
  static const char* PATH = "/mecon_dev";
  uint8_t rec[20];
  bool have = false;
  AtomicFile::recover(fs, PATH);
  if (AtomicFile::exists(fs, PATH)) {
    File file = AtomicFile::openForRead(fs, PATH);
    if (file) {
      have = file.read(rec, sizeof(rec)) == (int)sizeof(rec);
      file.close();
    }
  }
  bool zero = true;
  for (int i = 0; have && i < 16 && zero; i++) zero = rec[i] == 0;
  if (!have || zero) {
    rng->random(rec, 16);
    memset(rec + 16, 0, 4);
  }
  uint32_t e;
  memcpy(&e, rec + 16, 4);
  e++;
  memcpy(rec + 16, &e, 4);
  memcpy(device_id, rec, 16);
  *epoch = e;
  return AtomicFile::save(fs, PATH, [&rec](Stream& s) { return s.write(rec, sizeof(rec)) == sizeof(rec); }, !have);
}

}  // namespace Iface
