#pragma once
// What an installed update leaves for the image that boots next (docs/contract/UPDATES.md section 7): the job, the
// image's digest, the image that installed it, and which slot holds each. Written just before activation, read at every
// boot. The job table keeps the job's state; this keeps what attestation and a revert need to know about the images, so a
// revert acts on the slots it names and never on a guess. Marked confirmed by job.confirm (not cleared: a power cut
// before the job table is saved, or a confirmed job later dropped from the table, must not read as an unproven image),
// and cleared by the first boot that finds the job over and the image it left running settled.
//
//   ESP32 (ESP_PLATFORM)   NVS namespace "mecon", key "updjob" (a factory reset erases it, with the job table)
//   simulated boards       a file beside the data directory (<dir>.updjob)
//   anything else          not kept: save fails, so nothing activates
#include <stdint.h>

namespace Upd {

struct Record {
  static const uint32_t MAGIC = 0x32445055u;   // "UPD2"
  uint32_t magic;
  char job[33];            // the job's identity (Iface::Job::ID_CAP + 1)
  char version[25];        // the manifest's version
  char prev_version[48];   // the version of the image that installed it (mecon::versionFull())
  uint8_t image_sha[32];   // the manifest's image.sha256: what a booted new image hashes to
  uint8_t prev_sha[32];    // the running image's hash when it was installed
  uint32_t image_size;
  uint32_t new_id;         // the slot the image was written to (UpdSlot.h slot identities)
  uint32_t prev_id;        // the slot that ran when it was activated: the previous image
  uint8_t confirmed;       // job.confirm (or a supersede) accepted the image
};

bool recordLoad(Record* out);   // false: none, or not an intact record of this layout
bool recordSave(const Record& r);
bool recordClear();

}  // namespace Upd
