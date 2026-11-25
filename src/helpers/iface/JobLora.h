#pragma once
// What a firmware update over LoRa keeps in its job's entry (docs/contract/LORA_UPDATES.md sections 4 and 7): the
// transfer token, the manifest it was offered for, and the progress that survives a reboot (`next`). It lives in the
// job's private bytes (Job::priv), so the job table's layout and persistence are the ones every job has. Header-only and
// platform-free: the native tests drive it.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Jobs.h"

namespace Iface {

struct JobLora {
  static constexpr uint8_t MAGIC = 0xB1;
  static constexpr uint32_t STALL_S = 7u * 24 * 3600;   // a paused transfer waits this long for a resume (section 7)
  uint8_t block;               // bulk frames per block, 1 to 8
  uint8_t reason;              // while paused: TransferReason (LoraTransfer.h)
  uint32_t next;               // the first frame not yet written, kept as the transfer goes (flash is the slot's own)
  uint32_t window_end;         // the offer's, unix seconds
  uint32_t frames_total;
  uint8_t token[16];           // the transfer token: the bulk and answer keys come from it
  uint8_t manifest[32];        // the manifest's SHA-256 the job was offered for: a resume must name the same one
};

// magic, block, reason and a spare byte; next, window_end and frames_total; the token; the manifest hash
static_assert(4 + 12 + 16 + 32 == Job::PRIV_CAP, "a LoRa transfer's state fills the job's private bytes exactly");

inline void jobLoraPut(Job& j, const JobLora& l) {
  uint8_t* p = j.priv();
  p[0] = JobLora::MAGIC;
  p[1] = l.block;
  p[2] = l.reason;
  p[3] = 0;
  memcpy(p + 4, &l.next, 4);
  memcpy(p + 8, &l.window_end, 4);
  memcpy(p + 12, &l.frames_total, 4);
  memcpy(p + 16, l.token, 16);
  memcpy(p + 32, l.manifest, 32);
}

// False for a job that is over or never held a transfer.
inline bool jobLoraGet(const Job& j, JobLora* l) {
  if (!j.state || jobTerminal(j.state) || j.result_len > Job::RESULT_CAP - Job::PRIV_CAP) return false;
  const uint8_t* p = j.priv();
  if (p[0] != JobLora::MAGIC) return false;
  if (!l) return true;
  l->block = p[1];
  l->reason = p[2];
  memcpy(&l->next, p + 4, 4);
  memcpy(&l->window_end, p + 8, 4);
  memcpy(&l->frames_total, p + 12, 4);
  memcpy(l->token, p + 16, 16);
  memcpy(l->manifest, p + 32, 32);
  return true;
}

inline void jobLoraClear(Job& j) { memset(j.priv(), 0, Job::PRIV_CAP); }

}  // namespace Iface
