#pragma once
// device_id and the boot epoch for the device interface (docs/contract/DEVICE_INTERFACE.md section 1), shared by
// every role. "/mecon_dev" and its copy "/mecon_dev2" hold the 16-byte device_id and a little-endian 32-bit epoch; a first
// boot, or both damaged, draws a new device_id and starts the epoch from 0, and every boot increments it. The caller includes its
// platform file system header before this one (as for AtomicFile.h).
#include <Mesh.h>
#include <string.h>
#include "helpers/AtomicFile.h"
#include "DeviceId.h"

namespace Iface {

// The record's two copies: losing one must not start the epoch again, or change device_id, while the node key and a
// Deployment record live on. A manager rejects a usig_boot lower than one it has seen.
template <typename FS_T>
bool readDeviceMeta(FS_T* fs, const char* path, uint8_t rec[20]) {
  bool have = false;
  AtomicFile::recover(fs, path);
  if (AtomicFile::exists(fs, path)) {
    File file = AtomicFile::openForRead(fs, path);
    if (file) {
      have = file.read(rec, 20) == 20;
      file.close();
    }
  }
  bool zero = true;
  for (int i = 0; have && i < 16 && zero; i++) zero = rec[i] == 0;
  return have && !zero;
}

// Fills device_id and *epoch for this boot. False when the record could not be saved: this boot still has its
// values, the next one may repeat the epoch.
template <typename FS_T>
bool bootDeviceMeta(FS_T* fs, mesh::RNG* rng, uint8_t device_id[16], uint32_t* epoch) {
  static const char* PATH = "/mecon_dev";
  static const char* COPY = "/mecon_dev2";
  uint8_t rec[20], other[20];
  const bool have_main = readDeviceMeta(fs, PATH, rec);
  const bool have_copy = readDeviceMeta(fs, COPY, other);
  bool have = have_main || have_copy;
  if (have_copy) {
    uint32_t a = 0, b = 0;
    if (have_main) memcpy(&a, rec + 16, 4);
    memcpy(&b, other + 16, 4);
    if (!have_main || b > a) memcpy(rec, other, sizeof(rec));
  }
  if (!have) {
    rng->random(rec, 16);
    memset(rec + 16, 0, 4);
  }
  uint32_t e;
  memcpy(&e, rec + 16, 4);
  e++;
  memcpy(rec + 16, &e, 4);
  memcpy(device_id, rec, 16);
  *epoch = e;
  static const char H[] = "0123456789abcdef";
  char* hex = deviceIdHexBuf();
  for (int i = 0; i < 16; i++) {
    hex[2 * i] = H[rec[i] >> 4];
    hex[2 * i + 1] = H[rec[i] & 15];
  }
  hex[32] = 0;
  const bool saved = AtomicFile::save(fs, PATH, [&rec](Stream& s) { return s.write(rec, sizeof(rec)) == sizeof(rec); }, !have_main);
  AtomicFile::save(fs, COPY, [&rec](Stream& s) { return s.write(rec, sizeof(rec)) == sizeof(rec); }, !have_copy);
  return saved;
}

}  // namespace Iface
