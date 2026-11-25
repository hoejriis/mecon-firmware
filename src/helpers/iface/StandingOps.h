#pragma once
// report.now and the standing instructions (docs/contract/OPERATIONS.md): a manager asks for an allowlisted report now, or
// every so often, and the device sends it while the manager is away. It runs only what was asked: a compact report to a named
// destination. No script, no rule, no catch-up after a gap.
#include <stddef.h>
#include <stdint.h>
#include "ConfigSnap.h"
#include "CompactReport.h"
#include "KeptReports.h"
#include "IfaceDispatch.h"

namespace Iface {

struct ReportHost {
  // Builds, signs and sends the report; the seq it used, or false when it could not go. With `built` (a REPORT_MAX buffer), the
  // report's exact signed bytes are put there and their length in `built_len` as soon as it is signed, whether or not it then went.
  bool (*send)(void* user, uint8_t fields, const uint8_t* to, int slot, uint32_t* seq, uint8_t* built, size_t* built_len);
  uint8_t (*produced)(void* user);                 // the report fields this board has
  bool (*may_read_private)(void* user);            // the caller of the request being handled
  const char* (*issuer)(void* user);               // the signing member of that request, "" for an unsigned one
  bool (*issuer_valid)(void* user, const char* issuer);   // still a member: a revoked member's instruction stops
  uint32_t (*random)(void* user);
  bool (*airtime_ok)(void* user);                  // the duty window has room for a report now
  SnapStore* store;
  void* user;
  // Both may be left out of an initializer, which makes them null (the firmware builds are C++11, where a default member
  // initializer would make this no longer an aggregate).
  KeptStore* kept;                                           // where kept reports go; null: this board keeps none
  size_t (*page_budget)(void* user);                         // what a report.list result may fill on this link; null: 1,100 bytes
  int kept_batch;                                            // reports to a write; 0: the platform's (keptBatchFor)
};

struct Standing {
  bool used = false, durable = true;
  char id[17] = {0};
  char issuer[17] = {0};
  uint8_t fields = 0;
  uint32_t every_s = 0;
  uint16_t runs_left = 0;   // 0: until removed
  bool keep = false;        // keep what it sends, for report.list (TELEMETRY.md, "Reports kept for later")
  bool has_to = false;
  uint8_t to[32] = {0};
  int16_t slot = -1;
  // in RAM only
  uint32_t from_ms = 0, wait_ms = 0;   // since when, and for how long: a period can be longer than a signed difference holds
  uint32_t runs = 0, failed = 0, skipped = 0;
  uint8_t held = 0;                    // passes this run has waited for airtime
  bool fresh = false;                  // put since the last tick: its period starts at the next tick's time
};

class StandingService {
public:
  static const int CAPACITY = 4;
  static const uint32_t MIN_EVERY_S = 900;
  static const uint32_t MAX_EVERY_S = 30UL * 24 * 3600;
  static const uint32_t HOLD_MS = 60000;     // with no airtime, a run due looks again this often
  static const uint8_t HOLDS = 5;            // and after this many looks it is skipped, until its next period
  static const uint32_t NOW_GAP_MS = 30000;   // report.now: at most one per this long, on every link
  static const size_t RECORD = 75;

  void begin(const ReportHost& h, uint32_t now);
  void tick(uint32_t now);
  void clear();   // the Deployment went: its instructions go with it

  Status opNow(OpContext& c, uint32_t now);
  Status opPut(OpContext& c, uint32_t now);
  Status opList(OpContext& c);
  Status opRemove(OpContext& c);
  Status opReportList(OpContext& c);
  Status opReportAck(OpContext& c);
  bool keeps() const { return _kept.available(); }
  uint32_t keptAppends() { return _kept.appends(); }

private:
  ReportHost _h = {};
  Standing _s[CAPACITY];
  KeptReports _kept;
  bool _begun = false, _now_sent = false;
  uint32_t _now_ms = 0;
  bool parseTarget(OpContext& c, uint8_t* fields, uint8_t to[32], bool* has_to, int* slot);
  void schedule(Standing& s, uint32_t now, bool first);
  bool persist();
  void load(uint32_t now);
};

}  // namespace Iface
