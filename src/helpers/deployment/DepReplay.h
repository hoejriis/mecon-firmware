#pragma once
// Replay protection for signed requests (docs/contract/AUTHORITY.md section 8): one strictly increasing sequence per
// authority-set member. The state is the member's mark, the highest job id accepted, kept in a Watermark (DepRecord.h).
//
// What is on flash is a reserved mark at or above it, saved before a request above the last reservation runs, so a reboot
// reopens nothing: the device comes back at the reserved mark, and ids up to it are refused. A save costs about 5 flash
// erases on one page on an nRF52 (measured, 10,000 rated), so only one id in MARK_RESERVE writes. The cost is that a
// manager skips up to MARK_RESERVE ids after a reboot: it is refused as replay, told the mark, and continues above it.
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

const uint32_t MARK_RESERVE = 256;

// Records job_id as the mark. Call only after replayCheck returned RV_NEW. True when job_id is above the reservation: w
// then holds a new one, and must be persisted BEFORE the request runs. At most once, not at least once: a power cut
// after the write loses the request, and its manager resends the same bytes, is answered replay or duplicate, and reads
// the state. The other order would let a cut after the run but before the write execute it a second time.
bool markAccepted(Watermark* w, uint32_t job_id);

}  // namespace Dep
