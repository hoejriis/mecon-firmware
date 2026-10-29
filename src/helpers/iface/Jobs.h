#pragma once
// The job table (docs/contract/OPERATIONS.md section 3): a request that cannot finish within its own exchange becomes a
// job, and its terminal result stays owed until a caller who may read it acknowledges it. The table survives a reboot:
// the role saves serialize() through its own file system after every change and loads it at boot.
//
//   - 8 slots. One state-changing job is open (not terminal) at a time; open() refuses a second (3.3).
//   - A terminal result stays until acknowledged or until its slot is needed: the oldest acknowledged entry goes
//     first, then the oldest terminal one; an open job is never evicted. An evicted id's hash goes to a ring of 64,
//     so a later duplicate is answered result-expired, never run again (3.2).
//   - awaiting-confirm has a deadline in running seconds, kept across boots, and a second boot while it is pending
//     reverts (3.4). The role does the revert; the table says when.
//
// Header-only and platform-free: the native tests drive it directly.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Iface {

enum JobState : uint8_t {
  JOB_ACCEPTED = 1,   // checked and authorized; nothing applied yet
  JOB_PROGRESS,       // running
  JOB_APPLIED,        // in effect (terminal unless the operation needs confirmation)
  JOB_AWAITING,       // applied, reverted unless confirmed in time
  JOB_CONFIRMED,      // terminal
  JOB_REVERTED,       // terminal
  JOB_FAILED,         // terminal
  JOB_CANCELLED       // terminal
};

inline bool jobTerminal(uint8_t s) { return s == JOB_APPLIED || s >= JOB_CONFIRMED; }

inline const char* jobStateName(uint8_t s) {
  switch (s) {
    case JOB_ACCEPTED: return "accepted";
    case JOB_PROGRESS: return "progress";
    case JOB_APPLIED: return "applied";
    case JOB_AWAITING: return "awaiting-confirm";
    case JOB_CONFIRMED: return "confirmed";
    case JOB_REVERTED: return "reverted";
    case JOB_FAILED: return "failed";
    case JOB_CANCELLED: return "cancelled";
  }
  return "?";
}

struct Job {
  static constexpr size_t ID_CAP = 32, OP_CAP = 32, RESULT_CAP = 384;
  char id[ID_CAP + 1];         // the request id, or "#<issuer>:<job_id>" for a signed request (the job's identity)
  char op[OP_CAP + 1];
  uint8_t state;
  uint8_t acked;               // a terminal result a reader has acknowledged
  uint8_t boots_pending;       // boots counted while awaiting-confirm
  uint8_t boot_rule;           // BOOT_SECOND: a second boot reverts (settings, OPERATIONS 3.4); BOOT_ABNORMAL: only an
                               // abnormal reset reverts (firmware updates, UPDATES.md section 7)
  uint32_t seq;                // creation order, for eviction
  uint32_t confirm_left_s;     // running seconds left to confirm, while awaiting-confirm
  // Who started the job and what it needs, as the role records them: the CLI classes a job.confirm or job.cancel caller
  // must hold (a bit per class, 1 << class, for every line the job ran), and the caller's CLI context, which a revert
  // runs as, so that a revert grants nothing the caller did not have and is audited to them.
  uint8_t cls;
  uint8_t who_transport, who_auth, who_posture, who_mgmt, who_audit;
  uint16_t result_len;
  char result[RESULT_CAP];     // the latest result or state data, as JSON members (the role's choice); never a value
                               // read back from the device, which a CLI reply can echo
};

enum BootRule : uint8_t { BOOT_SECOND = 0, BOOT_ABNORMAL = 1 };

class JobTable {
public:
  static constexpr int SLOTS = 8, HASHES = 64;
  enum Lookup { NONE, FOUND, EXPIRED };

  JobTable() { clear(); }

  void clear() {
    memset(_j, 0, sizeof(_j));
    memset(_h, 0, sizeof(_h));
    _nh = _hnext = 0;
    _seq = 0;
  }

  // The open (non-terminal) job, or null.
  Job* openJob() {
    for (Job& j : _j)
      if (j.state && !jobTerminal(j.state)) return &j;
    return nullptr;
  }

  Lookup find(const char* id, Job** out) {
    for (Job& j : _j)
      if (j.state && strcmp(j.id, id) == 0) { *out = &j; return FOUND; }
    const uint64_t h = hash(id);
    for (int i = 0; i < _nh; i++)
      if (_h[i] == h) return EXPIRED;
    *out = nullptr;
    return NONE;
  }

  // A new job in state accepted. Null when a job is open (the caller answers busy, job-open:<open id>), when the id is
  // already known (the caller answers the job, or result-expired), or when the id or op does not fit.
  Job* open(const char* id, const char* op) {
    if (!id || !op || !*id || strlen(id) > Job::ID_CAP || strlen(op) > Job::OP_CAP) return nullptr;
    if (openJob()) return nullptr;
    Job* known;
    if (find(id, &known) != NONE) return nullptr;
    Job* slot = freeSlot();
    if (!slot) return nullptr;
    memset(slot, 0, sizeof(*slot));
    strcpy(slot->id, id);
    strcpy(slot->op, op);
    slot->state = JOB_ACCEPTED;
    slot->seq = ++_seq;
    return slot;
  }

  // A state change. A terminal job never moves again. awaiting-confirm is entered with awaitConfirm(), never here.
  bool setState(Job* j, uint8_t state, const char* result = nullptr, size_t len = 0) {
    if (!j || !j->state || jobTerminal(j->state) || state < JOB_ACCEPTED || state > JOB_CANCELLED || state == JOB_AWAITING)
      return false;
    if (result) {
      if (len > Job::RESULT_CAP) return false;
      memcpy(j->result, result, len);
      j->result_len = (uint16_t)len;
    }
    j->state = state;
    return true;
  }

  // The change is applied tentatively (OPERATIONS 3.4): within_s running seconds to confirm, and the boot rule.
  // applied_live: the change took effect in this boot, which counts as its first; a change that takes effect after a
  // reboot gets that reboot as its first.
  bool awaitConfirm(Job* j, uint32_t within_s, bool applied_live, uint8_t rule = BOOT_SECOND) {
    if (!j || !j->state || jobTerminal(j->state) || j->state == JOB_AWAITING || !within_s) return false;
    j->state = JOB_AWAITING;
    j->confirm_left_s = within_s;
    j->boots_pending = applied_live ? 1 : 0;
    j->boot_rule = rule;
    return true;
  }


  // job.ack: only a terminal result can be acknowledged.
  bool ack(const char* id) {
    Job* j;
    if (find(id, &j) != FOUND || !jobTerminal(j->state)) return false;
    j->acked = 1;
    return true;
  }

  int openCount() const { return openJobConst() ? 1 : 0; }
  int owedCount() const {
    int n = 0;
    for (const Job& j : _j) n += j.state && jobTerminal(j.state) && !j.acked;
    return n;
  }
  const Job* slot(int i) const { return (i >= 0 && i < SLOTS && _j[i].state) ? &_j[i] : nullptr; }

  // Running time passed (call at least once a second). An awaiting-confirm job whose deadline runs out is handed to
  // expired(); the role reverts and sets its terminal state.
  template <class F> void tick(uint32_t seconds, F expired) {
    Job* j = openJob();
    if (!j || j->state != JOB_AWAITING) return;
    j->confirm_left_s = seconds >= j->confirm_left_s ? 0 : j->confirm_left_s - seconds;
    if (!j->confirm_left_s) expired(j);
  }

  // A boot. abnormal: the reset was a panic, the watchdog or a brownout. Under BOOT_SECOND the second boot while
  // pending is handed to revert(); under BOOT_ABNORMAL only an abnormal reset is.
  template <class F> void booted(bool abnormal, F revert) {
    Job* j = openJob();
    if (!j || j->state != JOB_AWAITING) return;
    if (j->boot_rule == BOOT_ABNORMAL) {
      if (abnormal) revert(j);
      return;
    }
    if (++j->boots_pending >= 2) revert(j);
  }

  // ---- persistence: a framed, CRC-checked image of the whole table, written atomically by the role

  static constexpr uint32_t MAGIC = 0x3142534Au;   // "JSB1"
  static size_t imageSize() { return 16 + sizeof(Job) * SLOTS + 8 * HASHES + 4; }

  size_t serialize(uint8_t* out, size_t cap) const {
    if (cap < imageSize()) return 0;
    size_t n = 0;
    put32(out, n, MAGIC);
    put32(out, n, _seq);
    put32(out, n, (uint32_t)_nh);
    put32(out, n, (uint32_t)_hnext);
    memcpy(out + n, _j, sizeof(_j)); n += sizeof(_j);
    memcpy(out + n, _h, sizeof(_h)); n += sizeof(_h);
    put32(out, n, crc32(out, n));
    return n;
  }

  // False, with the table cleared, for anything that is not an intact image of this layout. A lost table forgets owed
  // results; it never invents a job.
  bool deserialize(const uint8_t* in, size_t len) {
    clear();
    if (len != imageSize()) return false;
    size_t n = 0;
    if (get32(in, n) != MAGIC) return false;
    const uint32_t crc_at = (uint32_t)(len - 4);
    size_t c = crc_at;
    if (get32(in, c) != crc32(in, crc_at)) return false;
    _seq = get32(in, n);
    const uint32_t nh = get32(in, n), hn = get32(in, n);
    if (nh > (uint32_t)HASHES || hn >= (uint32_t)HASHES) return false;
    memcpy(_j, in + n, sizeof(_j)); n += sizeof(_j);
    memcpy(_h, in + n, sizeof(_h));
    _nh = (int)nh;
    _hnext = (int)hn;
    for (Job& j : _j) {   // a damaged entry is dropped, never half trusted
      j.id[Job::ID_CAP] = 0;
      j.op[Job::OP_CAP] = 0;
      if (j.state > JOB_CANCELLED || j.boot_rule > BOOT_ABNORMAL || j.result_len > Job::RESULT_CAP)
        memset(&j, 0, sizeof(j));
    }
    return true;
  }

private:
  Job _j[SLOTS];
  uint64_t _h[HASHES];
  int _nh, _hnext;
  uint32_t _seq;

  const Job* openJobConst() const {
    for (const Job& j : _j)
      if (j.state && !jobTerminal(j.state)) return &j;
    return nullptr;
  }

  // A free slot, else the oldest acknowledged entry, else the oldest terminal one. An open job is never taken.
  Job* freeSlot() {
    for (Job& j : _j)
      if (!j.state) return &j;
    Job* pick = nullptr;
    for (int pass = 0; pass < 2 && !pick; pass++)
      for (Job& j : _j) {
        if (!jobTerminal(j.state) || (pass == 0 && !j.acked)) continue;
        if (!pick || j.seq < pick->seq) pick = &j;
      }
    if (pick) {
      _h[_hnext] = hash(pick->id);
      _hnext = (_hnext + 1) % HASHES;
      if (_nh < HASHES) _nh++;
    }
    return pick;
  }

  static uint64_t hash(const char* s) {   // FNV-1a, as the result cache
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
    return h;
  }
  static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
      c ^= p[i];
      for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
  }
  static void put32(uint8_t* o, size_t& n, uint32_t v) { for (int i = 0; i < 4; i++) o[n++] = (uint8_t)(v >> (8 * i)); }
  static uint32_t get32(const uint8_t* in, size_t& n) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)in[n++] << (8 * i);
    return v;
  }
};

}  // namespace Iface
