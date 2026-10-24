#include "DepReplay.h"

namespace Dep {

static const uint32_t WINDOW_MASK = (1u << REPLAY_WINDOW) - 1;

ReplayVerdict replayCheck(const Watermark& w, uint32_t job_id) {
  if (job_id == 0) return RV_TOO_OLD;
  if (job_id > w.mark) return RV_NEW;
  uint32_t age = w.mark - job_id;
  if (age >= REPLAY_WINDOW) return RV_TOO_OLD;
  // Bit 0 is the mark itself, accepted by definition, whatever a stored mask says.
  uint32_t mask = (w.mask | 1u) & WINDOW_MASK;
  return (mask >> age) & 1u ? RV_DUPLICATE : RV_NEW;
}

void markAccepted(Watermark* w, uint32_t job_id) {
  if (job_id == 0) return;
  if (job_id > w->mark) {
    uint32_t shift = job_id - w->mark;
    uint32_t mask = w->mark ? (w->mask | 1u) : 0u;
    w->mask = shift >= REPLAY_WINDOW ? 1u : (((mask << shift) | 1u) & WINDOW_MASK);
    w->mark = job_id;
  } else {
    uint32_t age = w->mark - job_id;
    if (age < REPLAY_WINDOW) w->mask = (w->mask | 1u | (1u << age)) & WINDOW_MASK;
  }
}

}  // namespace Dep
