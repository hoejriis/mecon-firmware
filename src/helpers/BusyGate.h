#pragma once
#include <stdint.h>

// Whether the device is in the middle of something a protective sleep must not cut (BatterySleep.h). Platform-free,
// tested natively (test/test_battery_sleep). One predicate, so the guard and the tests share it.
//
// The inputs, and how the firmware tells each:
//   flash_write      a file save in progress: AtomicFile::save() holds a FlashScope (flashWrites() != 0)
//   job_open         the job table has an open job: a settings change or an update awaiting its confirmation, or any
//                    state-changing job not yet terminal (OPERATIONS.md 3.3)
//   update_transfer  the update service is receiving, downloading, verifying or erasing an image, or a reset into the
//                    bootloader is pending (UpdService::transferBusy())
//   update_trial     the running image is on trial: installed and not yet marked valid, so any reset returns to the
//                    previous image (Upd::runningState() == RS_PENDING)
// The loop is the only caller, and a save is synchronous there, so flash_write is normally clear when the guard looks;
// the counter covers a save made from another task.
namespace BusyGate {

inline volatile uint8_t& flashWrites() {
  static volatile uint8_t n = 0;
  return n;
}

// Held for the length of one file save. It counts only where something reads the count: a board with the protective
// sleep, and the native tests. Elsewhere it is empty, so a board without the sleep carries no code for it in any save.
#if defined(BATTERY_SLEEP_BOARD) || !defined(ARDUINO)
struct FlashScope {
  FlashScope() { flashWrites() = (uint8_t)(flashWrites() + 1); }
  ~FlashScope() { flashWrites() = (uint8_t)(flashWrites() - 1); }
};
#else
struct FlashScope {};
#endif

struct Inputs {
  bool flash_write;
  bool job_open;
  bool update_transfer;
  bool update_trial;
};

// Why the device may not sleep now, or null when nothing holds it. At the emergency level only a save in progress
// holds it: an open job, a transfer or a trial is given up rather than the cell (BatterySleep.h).
inline const char* reason(const Inputs& in, bool emergency) {
  if (in.flash_write) return "flash-write";
  if (emergency) return nullptr;
  if (in.update_trial) return "update-trial";
  if (in.update_transfer) return "update-transfer";
  if (in.job_open) return "job-open";
  return nullptr;
}

}  // namespace BusyGate
