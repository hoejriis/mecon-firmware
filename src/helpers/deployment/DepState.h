#pragma once
// The Deployment state a running device holds: the loaded record, its parsed authority set, and the refusal
// counter. Allocated only when a record exists (or is corrupt), so a standalone device carries none of it.
#include <stddef.h>
#include <stdint.h>
#include "DepAuthority.h"
#include "DepRecord.h"
#include "helpers/iface/IfaceJson.h"

namespace Dep {

struct DepState {
  LoadState state;          // LS_OK or LS_CORRUPT (an absent record has no DepState)
  int slot;                 // the copy the record came from, for the next write
  uint32_t generation;
  Record rec;               // valid when LS_OK
  AuthoritySet set;         // valid when LS_OK
  uint32_t auth_rejections; // contract section 8 refusals since boot, corrupt-record refusals included
};

// From the two copies as read from storage. nullptr when neither exists (a device with no Deployment). A copy
// that decodes but whose stored set no longer verifies against the stored key, or names another revision than the
// record, is treated like a damaged copy: the record is corrupt unless the other copy is intact.
DepState* depLoad(const Copy& a, const Copy& b);
void depFree(DepState* s);

// The status.deployment object (contract section 11) under the key "deployment"; writes nothing for a device with
// no record (absent means legacy, not empty).
void depWriteStatus(Iface::JsonWriter& w, const DepState* s, bool hardened);

}  // namespace Dep
