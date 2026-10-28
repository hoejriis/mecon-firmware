#pragma once
// Replay protection for signed requests (docs/contract/AUTHORITY.md section 8): one strictly increasing sequence per
// authority-set member. The state is the member's mark, the highest job id accepted, kept in a Watermark (DepRecord.h)
// and persisted before the request runs, so a reboot reopens nothing.
//
// Strict, not a window: with a window a request an intermediary held back could still run after newer ones, out of
// order (an old config.apply putting back a setting, a reboot firing late). With one strict sequence, the moment a newer
// request runs every older one still held is dead. A manager retries a lost request with the same signed bytes, which
// lands on the mark and is answered without running again.
//
// No sequence-reset adoption: ids below the mark that verify can only be old signed requests. A manager that lost its
// sequence reads its mark (deployment.get) and continues above it, or is re-admitted, which resets its mark while
// check 6 (admitted_revision) refuses everything it signed before.
#include <stdint.h>
#include "DepRecord.h"

namespace Dep {

enum ReplayVerdict : uint8_t {
  RV_NEW = 0,       // above the mark: run it, after markAccepted has been persisted
  RV_DUPLICATE,     // the mark itself: the same request again; answer from the result cache or "duplicate", run nothing
  RV_TOO_OLD        // below the mark, or 0: refused as replay
};

ReplayVerdict replayCheck(const Watermark& w, uint32_t job_id);

// Records job_id as the mark. Call only after replayCheck returned RV_NEW, and persist w BEFORE the request runs: at
// most once, not at least once. A power cut between the write and the run loses the request; its manager resends the
// same bytes and is answered "duplicate", and reads the state. The other order would let a cut after the run but before
// the write execute it a second time when the same signed request is delivered again.
void markAccepted(Watermark* w, uint32_t job_id);

}  // namespace Dep
