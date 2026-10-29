#pragma once
// The job operations of the device interface (docs/contract/OPERATIONS.md section 3), shared by the Companion and the
// Repeater/Room console: job.get, job.list, job.ack, job.cancel, job.confirm, and the `jobs` member of the status
// snapshot. It owns the job table (Jobs.h), allocated on first use (about 5 KB), and saves it through the role's
// JobStore after every change, so open jobs and owed results survive a reboot.
//
// The operations that create jobs, and what a revert or a confirmation does to the device, belong to the role: it
// passes hooks. With no hook, an awaiting-confirm job cannot be cancelled or confirmed (nothing creates one yet).
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"
#include "IfaceJson.h"
#include "Jobs.h"

namespace Iface {

// The table's file: one image, replaced atomically by the role's file system.
class JobStore {
public:
  virtual ~JobStore() {}
  virtual size_t load(uint8_t* buf, size_t cap) = 0;   // 0: no file, or unreadable
  virtual bool save(const uint8_t* buf, size_t len) = 0;
};

class JobService {
public:
  // The role's part of a pending change: revert puts the previous state back (and the role sets the terminal state),
  // confirm makes it permanent. False: it could not be done now.
  typedef bool (*Hook)(void* user, Job& j);

  ~JobService() { delete _t; }

  // At boot: loads a saved table if there is one, and applies the boot rule to an awaiting-confirm job (abnormal: the
  // reset was a panic, the watchdog or a brownout).
  void begin(JobStore* store, bool abnormal_boot, Hook revert, Hook confirm, void* user);

  JobTable* table();   // allocated on first use; null when the heap cannot hold it
  bool persist();      // saves the table; false (and the caller answers busy) when the save fails

  Status opGet(OpContext& c);
  Status opList(OpContext& c);
  Status opAck(OpContext& c);
  Status opCancel(OpContext& c);
  Status opConfirm(OpContext& c);

  // "jobs":{"open","owed"}: a manager that reconnects over any path learns there is something to collect (3.2).
  void writeStatus(JsonWriter& w);

  // Running time, for the confirm deadline: call from the loop; it counts whole seconds.
  void tick(uint32_t now_ms);

  // One job's summary members (job, op, state, owed), as job.get and job.list write them.
  static void writeSummary(JsonWriter& w, const Job& j);

private:
  JobTable* _t = nullptr;
  JobStore* _store = nullptr;
  Hook _revert = nullptr, _confirm = nullptr;
  void* _user = nullptr;
  uint32_t _tick_ms = 0;
  bool _tick_init = false;
  static bool jobArg(OpContext& c, char* id, size_t cap);
};

}  // namespace Iface
