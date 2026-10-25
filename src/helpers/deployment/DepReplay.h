#pragma once
// Replay protection for signed requests: one sliding window per issuer and job class (Deployment authority
// contract v1, section 7), the RFC 6479 technique. The state is a Watermark (DepRecord.h): the highest job id
// accepted, and a bitmap of which of the WINDOW ids below it were accepted. Both are persisted together, so a
// reboot reopens nothing.
//
// No sequence-reset adoption for signed issuers. An unsigned guard may adopt a sender's restarted sequence after
// a run of out-of-window ids, because there nothing stops a publisher from sending new ids anyway. With signing,
// a run of out-of-window ids that verify can only be old signed requests replayed in order, and adopting them
// would execute one again. A Backend whose job sequence really restarted is re-admitted to the authority set
// instead: its watermarks are emptied and check 6 (admitted_revision) refuses everything it signed before.
#include <stdint.h>
#include "DepRecord.h"

namespace Dep {

const uint8_t REPLAY_WINDOW = 24;

enum ReplayVerdict : uint8_t {
  RV_NEW = 0,       // not seen: run it, then markAccepted
  RV_DUPLICATE,     // inside the window and already accepted: answer from the result cache, run nothing
  RV_TOO_OLD        // below the window: no record either way, refused and reported
};

// Job id 0 is never a real id: refused as too old.
ReplayVerdict replayCheck(const Watermark& w, uint32_t job_id);

// Records job_id as accepted. Call only after replayCheck returned RV_NEW, and persist w BEFORE the request runs:
// at most once, not at least once. A power cut between the write and the run then loses the request, which its issuer
// sees as no result and re-signs under a new job id; the other order would let a cut after the run but before the
// write execute it a second time when the same signed request is delivered again.
void markAccepted(Watermark* w, uint32_t job_id);

}  // namespace Dep
