#include "DepReplay.h"

namespace Dep {

ReplayVerdict replayCheck(const Watermark& w, uint32_t job_id) {
  if (job_id == 0) return RV_TOO_OLD;
  if (job_id > w.mark) return RV_NEW;
  return job_id == w.mark ? RV_DUPLICATE : RV_TOO_OLD;
}

void markAccepted(Watermark* w, uint32_t job_id) {
  if (job_id > w->mark) w->mark = job_id;
}

}  // namespace Dep
