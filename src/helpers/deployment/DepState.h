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
  bool other_damaged;       // the other copy exists but is not intact: its replay state may have been newer
  Record rec;               // valid when LS_OK
  AuthoritySet set;         // valid when LS_OK
  uint32_t auth_rejections; // contract section 8 refusals since boot, corrupt-record refusals included
};

// From the two copies as read from storage. Returns LS_ABSENT when neither exists (a device with no Deployment;
// *out is nullptr), else LS_OK or LS_CORRUPT with *out the loaded state. When the state cannot be allocated while a
// copy exists, the answer is LS_CORRUPT with *out nullptr: never absent, which would reopen a first provisioning. A
// copy that decodes but whose stored set no longer verifies against the stored key, or names another revision than
// the record, is treated like a damaged copy.
LoadState depLoad(const Copy& a, const Copy& b, DepState** out);
void depFree(DepState* s);

// The status.deployment object (contract section 11) under the key "deployment"; writes nothing for LS_ABSENT
// (absent means legacy, not empty). Not reported by any role yet: a device only claims a record once it enforces it.
void depWriteStatus(Iface::JsonWriter& w, LoadState state, const DepState* s, bool hardened);

}  // namespace Dep
