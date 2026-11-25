#pragma once
// The target's side of one open LoRa transfer (docs/contract/LORA_UPDATES.md sections 6 and 7): which bulk frames to
// write, which to keep in RAM until a gap fills, when to answer a block, when to keep `next` durably and when the
// transfer pauses, stalls or is cancelled. Pure logic on top of LoraBulk: the radio, the job store and the flash are the
// caller's, reached through the Sink callbacks, so this runs in the native unit tests.
// Not in this slice: deflate. The sink receives the transferred bytes as they are; inflating is the caller's.
#include <stddef.h>
#include <stdint.h>
#include "LoraBulk.h"

namespace Iface {

const uint32_t TRANSFER_PERSIST_EVERY = 64;          // frames between durable saves of `next` (about 7.4 KB)
const uint32_t TRANSFER_QUIET_MS = 8000;             // after this long without a frame of the block, answer anyway
const uint32_t TRANSFER_DUTY_PAUSE_MS = 10u * 60 * 1000;      // an answer unsendable this long: pause `duty`
const uint32_t TRANSFER_MANAGER_PAUSE_MS = 30u * 60 * 1000;   // no bulk frame this long: pause `manager`
const uint32_t TRANSFER_STALL_MS = 7u * 24 * 3600 * 1000;     // paused this long: failed `stalled`
const uint32_t TRANSFER_WINDOW_MAX_S = 12u * 3600;            // the longest window an offer may open
// Every timer is compared wrap-safe as (int32_t)(now - t), which is right only below 2^31 ms (24.8 days).
static_assert(TRANSFER_STALL_MS < 0x7FFFFFFFu, "the 7-day stall must fit a wrap-safe uint32 millisecond comparison");

// A window is valid when it ends after it was issued and lasts at most 12 hours (the caller answers `bad-args` if not).
bool windowValid(uint32_t window_end, uint32_t issued_at);
// The deadline, in the caller's millisecond clock, for a window: never longer than window_end - issued_at, and shorter
// when the device's own unix clock (if known) says less time is left. A window already over gives `now_ms`.
uint32_t windowDeadlineMs(uint32_t window_end, uint32_t issued_at, bool unix_known, uint32_t device_unix,
                          uint32_t now_ms);

struct TransferSink {
  void* ctx;
  bool (*write)(void* ctx, uint32_t offset, const uint8_t* data, size_t len);   // false: the flash write failed
  bool (*sendAnswer)(void* ctx, const uint8_t* bytes, size_t len);              // false: no room in the duty window now
  void (*saveNext)(void* ctx, uint32_t next);                                   // durable progress
  void (*eraseSlot)(void* ctx);
};

enum TransferState : uint8_t { TS_OPEN, TS_PAUSED, TS_COMPLETE, TS_FAILED };
enum TransferReason : uint8_t {
  TR_NONE,
  TR_WINDOW,      // paused: the window ended
  TR_DUTY,        // paused: no room for an answer for 10 minutes
  TR_MANAGER,     // paused: no bulk frame for 30 minutes
  TR_STALLED,     // failed: paused 7 days
  TR_CANCELLED,   // failed
  TR_WRITE,       // failed: the sink could not write
};
const char* transferReasonName(TransferReason r);

struct TransferStatus {
  TransferState state;
  TransferReason reason;
  uint32_t next;
  uint32_t frames_total;
  uint32_t dup;              // a frame already written or held
  uint32_t out_of_block;     // ahead of the current block
  uint32_t dropped_paused;   // arrived while paused
  uint32_t dropped_closed;   // arrived after the transfer ended
  uint32_t bad[6];           // by BulkCheck: not-bulk, other job, past end, bad length, bad tag (index 0, BULK_OK, unused)
  uint32_t answers;          // block answers sent
};

class LoraTransfer {
 public:
  explicit LoraTransfer(const TransferSink& sink, uint32_t quiet_ms = TRANSFER_QUIET_MS);

  // Opens the transfer. `next` is 0 for a new job, or the persisted value on a resume after a reboot (frames between it
  // and the `next` that was lost are written again: on NOR flash the bytes are identical, so that is harmless). False
  // for a block outside 1..8 or a `next` past the end. A transfer already whole at `next` begins `complete`.
  bool begin(uint32_t job, uint32_t transfer_size, unsigned block, const BulkKeys& keys, uint32_t deadline_ms,
             uint32_t now_ms, uint32_t next);
  void onFrame(const uint8_t* f, size_t n, uint32_t now_ms);
  void tick(uint32_t now_ms);
  // Reopens a paused transfer at base = next. False unless paused.
  bool resume(uint32_t deadline_ms, uint32_t now_ms);
  void cancel();
  TransferStatus status() const;

  TransferState state() const { return state_; }
  uint32_t next() const { return next_; }

 private:
  void fail(TransferReason r, bool erase);
  void pause(TransferReason r, uint32_t now_ms);
  void persist(uint32_t upto);
  bool trySend(uint32_t now_ms);
  uint8_t haveMask() const;
  bool writeAt(uint32_t index, const uint8_t* d, size_t n);

  TransferSink sink_;
  uint32_t quiet_ms_;
  BulkKeys keys_;
  uint32_t job_ = 0, size_ = 0, total_ = 0;
  unsigned block_ = 1;
  uint32_t base_ = 0, next_ = 0, saved_next_ = 0;
  uint32_t deadline_ = 0;
  uint8_t buf_[BULK_BLOCK_MAX][BULK_DATA];
  uint8_t buffered_ = 0;   // bit i: frame base + i is in buf_[i], not yet written
  bool fresh_ = false;     // a frame of this block arrived since the last answer
  bool pending_ = false;   // an answer is due and not yet sent
  bool blocked_ = false;   // an answer was refused by the duty window
  uint32_t last_rx_ = 0, last_any_ = 0, blocked_since_ = 0, paused_at_ = 0;
  TransferState state_ = TS_FAILED;
  TransferReason reason_ = TR_NONE;
  TransferStatus st_;
};

}  // namespace Iface
