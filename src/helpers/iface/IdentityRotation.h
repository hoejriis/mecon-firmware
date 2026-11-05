#pragma once
// identity.rotate's steps (docs/contract/AUTHORITY.md section 11), the same on every role: a new key from the cryptographic
// RNG, the old key's signed statement stored before anything changes, the atomic key save, the swap, the role's drop of
// what belonged to the old key, then the statement's pending mark cleared. What a role stores and drops is injected.
#include <stddef.h>
#include <stdint.h>
#include <Identity.h>

namespace Iface {

struct RotationStore {
  size_t (*load)(void* user, uint8_t* buf, size_t cap);   // the statement file; 0 when there is none
  bool (*save)(void* user, const uint8_t* buf, size_t len);
  void (*remove)(void* user);
  bool (*save_key)(void* user, const mesh::LocalIdentity& id);
  bool (*drop)(void* user);                                // false: tried again at the next boot
  void* user;
};

// Copies the stored statement out; false when there is none or it is not whole.
bool rotationRead(const RotationStore& s, char* text, size_t tcap, char* sig_b64, size_t scap);
// A rotation cut by a power loss: deletes a statement that never took effect, finishes a pending drop.
void rotationRecover(const RotationStore& s, const mesh::LocalIdentity& self);
// 1: rotated. 2: rotated, but the drop failed (the statement stays pending). 0: nothing changed, *err says why.
int rotationRun(const RotationStore& s, mesh::LocalIdentity& self, const char* device_id_hex, uint32_t job_id, const char** err);

}  // namespace Iface
