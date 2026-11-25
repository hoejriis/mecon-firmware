#include "LoraTransfer.h"
#include <string.h>

namespace Iface {

static inline bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }   // wrap-safe now >= t
static inline bool elapsed(uint32_t now, uint32_t since, uint32_t span) { return (uint32_t)(now - since) >= span; }

bool windowValid(uint32_t window_end, uint32_t issued_at) {
  return window_end > issued_at && window_end - issued_at <= TRANSFER_WINDOW_MAX_S;
}

uint32_t windowDeadlineMs(uint32_t window_end, uint32_t issued_at, bool unix_known, uint32_t device_unix,
                          uint32_t now_ms) {
  if (window_end <= issued_at) return now_ms;
  uint32_t s = window_end - issued_at;
  if (s > TRANSFER_WINDOW_MAX_S) s = TRANSFER_WINDOW_MAX_S;
  if (unix_known) {
    const uint32_t left = (int32_t)(window_end - device_unix) > 0 ? window_end - device_unix : 0;
    if (left < s) s = left;
  }
  return now_ms + s * 1000u;
}

const char* transferReasonName(TransferReason r) {
  switch (r) {
    case TR_WINDOW: return "window";
    case TR_DUTY: return "duty";
    case TR_MANAGER: return "manager";
    case TR_STALLED: return "stalled";
    case TR_CANCELLED: return "cancelled";
    case TR_WRITE: return "write";
    default: return "";
  }
}

LoraTransfer::LoraTransfer(const TransferSink& sink, uint32_t quiet_ms) : sink_(sink), quiet_ms_(quiet_ms) {
  memset(&keys_, 0, sizeof(keys_));
  memset(buf_, 0, sizeof(buf_));
  memset(&st_, 0, sizeof(st_));
}

bool LoraTransfer::begin(uint32_t job, uint32_t transfer_size, unsigned block, const BulkKeys& keys,
                         uint32_t deadline_ms, uint32_t now_ms, uint32_t next) {
  if (block < 1 || block > BULK_BLOCK_MAX) return false;
  total_ = bulkFramesTotal(transfer_size);
  if (next > total_) return false;
  job_ = job;
  size_ = transfer_size;
  block_ = block;
  keys_ = keys;
  deadline_ = deadline_ms;
  base_ = next_ = saved_next_ = next;
  buffered_ = 0;
  fresh_ = pending_ = blocked_ = false;
  has_prev_ = re_fresh_ = re_pending_ = false;
  last_rx_ = last_any_ = now_ms;
  reason_ = TR_NONE;
  memset(&st_, 0, sizeof(st_));
  state_ = (next_ >= total_) ? TS_COMPLETE : TS_OPEN;
  return true;
}

void LoraTransfer::persist(uint32_t upto) {
  if (upto == saved_next_) return;
  saved_next_ = upto;
  sink_.saveNext(sink_.ctx, upto);
}

void LoraTransfer::fail(TransferReason r, bool erase) {
  state_ = TS_FAILED;
  reason_ = r;
  buffered_ = 0;
  pending_ = false;
  if (erase) sink_.eraseSlot(sink_.ctx);
}

void LoraTransfer::pause(TransferReason r, uint32_t now_ms) {
  persist(next_);
  buffered_ = 0;   // RAM-held frames are re-sent by the manager after the resume
  pending_ = fresh_ = blocked_ = false;
  has_prev_ = re_fresh_ = re_pending_ = false;
  state_ = TS_PAUSED;
  reason_ = r;
  paused_at_ = now_ms;
}

bool LoraTransfer::resume(uint32_t deadline_ms, uint32_t now_ms) {
  if (state_ != TS_PAUSED) return false;
  deadline_ = deadline_ms;
  base_ = next_;
  buffered_ = 0;
  fresh_ = pending_ = blocked_ = false;
  has_prev_ = re_fresh_ = re_pending_ = false;
  last_rx_ = last_any_ = now_ms;
  reason_ = TR_NONE;
  state_ = TS_OPEN;
  if (next_ >= total_) {
    persist(next_);
    state_ = TS_COMPLETE;
  }
  return true;
}

void LoraTransfer::cancel() {
  if (state_ == TS_OPEN || state_ == TS_PAUSED) fail(TR_CANCELLED, true);
}

uint8_t LoraTransfer::haveMask() const {
  uint8_t have = 0;
  for (unsigned i = 0; i < block_; i++) {
    const uint32_t idx = base_ + i;
    if (idx >= total_) break;
    if (idx < next_ || (buffered_ & (1u << i))) have |= (uint8_t)(1u << i);
  }
  return have;
}

bool LoraTransfer::writeAt(uint32_t index, const uint8_t* d, size_t n) {
  if (sink_.write(sink_.ctx, index * (uint32_t)BULK_DATA, d, n)) return true;
  fail(TR_WRITE, false);
  return false;
}

void LoraTransfer::onFrame(const uint8_t* f, size_t n, uint32_t now_ms) {
  if (state_ == TS_PAUSED) { st_.dropped_paused++; return; }
  if (state_ != TS_OPEN && state_ != TS_COMPLETE) { st_.dropped_closed++; return; }
  uint32_t index;
  const uint8_t* data;
  size_t len;
  const BulkCheck c = bulkFrameDecode(keys_, job_, size_, f, n, &index, &data, &len);
  if (c != BULK_OK) {
    if ((unsigned)c < 6) st_.bad[c]++;
    return;   // never answered
  }
  if (has_prev_ && index >= prev_base_ && index < base_) {
    // A resend of the block just answered: its answer was lost. Never written; answered again, once per batch.
    st_.dup++;
    last_any_ = now_ms;
    re_rx_ = now_ms;
    re_fresh_ = true;
    if (index == base_ - 1) re_pending_ = true;
    if (re_pending_) trySendPrev(now_ms);
    return;
  }
  if (state_ == TS_COMPLETE) { st_.dropped_closed++; return; }
  last_any_ = now_ms;
  if (index < next_) {   // already written; never written twice
    st_.dup++;
    if (index >= base_) { last_rx_ = now_ms; fresh_ = true; if (index == base_ + block_ - 1) pending_ = true; }
    if (pending_) trySend(now_ms);
    return;
  }
  if (index >= base_ + block_) { st_.out_of_block++; return; }
  const unsigned slot = index - base_;
  last_rx_ = now_ms;
  fresh_ = true;
  if (index == next_) {
    if (!writeAt(index, data, len)) return;
    const uint32_t before = next_;
    next_++;
    while (next_ < total_ && next_ < base_ + block_ && (buffered_ & (1u << (next_ - base_)))) {
      const unsigned s = next_ - base_;
      buffered_ &= (uint8_t)~(1u << s);
      if (!writeAt(next_, buf_[s], bulkFrameDataLen(size_, next_))) return;
      next_++;
    }
    if (next_ / TRANSFER_PERSIST_EVERY > before / TRANSFER_PERSIST_EVERY) persist(next_);
  } else if (buffered_ & (1u << slot)) {
    st_.dup++;
  } else {
    memcpy(buf_[slot], data, len);
    buffered_ |= (uint8_t)(1u << slot);
  }
  // The block's last frame has just arrived (or the transfer's): answer.
  if (index == base_ + block_ - 1 || index == total_ - 1) pending_ = true;
  if (pending_) trySend(now_ms);
}

bool LoraTransfer::trySend(uint32_t now_ms) {
  uint8_t out[ANSWER_LEN];
  answerEncode(keys_, job_, base_, next_, haveMask(), out);
  if (!sink_.sendAnswer(sink_.ctx, out, ANSWER_LEN)) {
    if (!blocked_) { blocked_ = true; blocked_since_ = now_ms; }
    return false;
  }
  st_.answers++;
  blocked_ = pending_ = fresh_ = false;
  const uint32_t end = base_ + block_ < total_ ? base_ + block_ : total_;
  if (next_ >= end) {   // the block is whole: the next one starts where this one ended
    prev_base_ = base_;
    has_prev_ = true;
    re_fresh_ = re_pending_ = false;
    base_ = next_;
    if (next_ >= total_) {
      persist(next_);
      state_ = TS_COMPLETE;   // the caller verifies the digest
    }
  }
  return true;
}

bool LoraTransfer::trySendPrev(uint32_t now_ms) {
  (void)now_ms;
  const unsigned len = base_ - prev_base_;   // the completed block: every frame held
  uint8_t out[ANSWER_LEN];
  answerEncode(keys_, job_, prev_base_, next_, (uint8_t)((1u << len) - 1), out);
  if (!sink_.sendAnswer(sink_.ctx, out, ANSWER_LEN)) return false;   // retried on a later tick; no pause for it
  st_.answers++;
  re_fresh_ = re_pending_ = false;
  return true;
}

void LoraTransfer::tick(uint32_t now_ms) {
  if ((state_ == TS_OPEN || state_ == TS_COMPLETE) && has_prev_) {
    if (!re_pending_ && re_fresh_ && elapsed(now_ms, re_rx_, quiet_ms_)) re_pending_ = true;
    if (re_pending_) trySendPrev(now_ms);
  }
  if (state_ == TS_PAUSED) {
    if (elapsed(now_ms, paused_at_, TRANSFER_STALL_MS)) fail(TR_STALLED, true);
    return;
  }
  if (state_ != TS_OPEN) return;
  if (reached(now_ms, deadline_)) { pause(TR_WINDOW, now_ms); return; }
  if (!pending_ && fresh_ && elapsed(now_ms, last_rx_, quiet_ms_)) pending_ = true;
  if (pending_ && !trySend(now_ms) && elapsed(now_ms, blocked_since_, TRANSFER_DUTY_PAUSE_MS)) {
    pause(TR_DUTY, now_ms);
    return;
  }
  if (state_ == TS_OPEN && elapsed(now_ms, last_any_, TRANSFER_MANAGER_PAUSE_MS)) pause(TR_MANAGER, now_ms);
}

TransferStatus LoraTransfer::status() const {
  TransferStatus s = st_;
  s.state = state_;
  s.reason = reason_;
  s.next = next_;
  s.frames_total = total_;
  return s;
}

}  // namespace Iface
