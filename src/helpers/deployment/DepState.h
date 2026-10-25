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
  uint32_t auth_rejections; // contract section 7 refusals since boot, corrupt-record refusals included
  // The sequence marks (DepRecord.h), reconciled against set by depLoadMarks.
  Marks marks;
  int marks_slot;           // the copy they came from (-1: none intact), for the next write
  uint32_t marks_generation;
  bool marks_lost;          // absent or corrupt while the record exists: every member blocked until re-admitted
  bool marks_dirty;         // the reconciled marks differ from the stored ones and must be written back
  int blocked;              // members whose mark is MARK_BLOCKED
};

// From the two copies as read from storage. Returns LS_ABSENT when neither exists (a device with no Deployment;
// *out is nullptr), else LS_OK or LS_CORRUPT with *out the loaded state. When the state cannot be allocated while a
// copy exists, the answer is LS_CORRUPT with *out nullptr: never absent, which would reopen a first provisioning. A
// copy that decodes but whose stored set no longer verifies against the stored key, or names another revision than
// the record, is treated like a damaged copy.
LoadState depLoad(const Copy& a, const Copy& b, DepState** out);
void depFree(DepState* s);

// The marks for a loaded record (state LS_OK), from their two copies: loaded, then reconciled against the record's set
// (reconcileMarks), so a lagging or leading marks record settles towards refusing. Sets marks_dirty when the result
// must be written back before any signed request runs.
void depLoadMarks(DepState* s, const Copy& a, const Copy& b);

// The status.deployment object under the key "deployment"; writes nothing for LS_ABSENT (absent means no Deployment, not
// empty). It carries the fallback flag (the newer copy of the record is damaged) and how many members are blocked. Not
// reported by any role yet: a device only claims a record once it enforces it.
void depWriteStatus(Iface::JsonWriter& w, LoadState state, const DepState* s, bool hardened);

}  // namespace Dep
