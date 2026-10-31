#pragma once
// The private management channel (docs/contract/MANAGEMENT_CHANNEL.md): one slot of the native channel table marked
// as the channel a manager reaches this device on. Knowing the channel's key lets a node read and send on it, nothing
// more: a command carried over it still needs its own authority (#95), and binding starts no forwarding, sync or
// reporting.
//
// The binding is a small record of its own (slot, key fingerprint, generation), never a second copy of the key. When
// the slot's key no longer matches the fingerprint, because something changed the slot natively, the binding is
// "stale" and is not silently moved to the new key.
//
// bind, rotate and unbind, and a channel.put or channel.remove of the bound slot, can cut the path a remote manager
// uses (OPERATIONS.md section 3.4): a caller other than the unsigned cable must ask for confirmation, and the change
// is then a job whose previous slot and record are kept in a snapshot file of their own until it is confirmed or
// reverted. The channel table, the record and the snapshot are injected, so the logic runs in the native unit tests.
#include <stddef.h>
#include <stdint.h>
#include "ChannelOps.h"
#include "ConfigSnap.h"
#include "IfaceDispatch.h"
#include "JobService.h"

namespace Iface {

const unsigned MGMT_FP_BYTES = 8;

struct MgmtRecord {
  bool bound = false;
  uint8_t slot = 0;
  uint8_t fp[MGMT_FP_BYTES] = {0};   // the first bytes of SHA-256 of the bound key: neither the key nor its on-air hash
  uint32_t generation = 0;           // counts every bind, rotation and move of the binding

  static constexpr size_t IMAGE = 4 + 1 + 1 + MGMT_FP_BYTES + 4 + 4;
  size_t serialize(uint8_t* out) const;
  bool parse(const uint8_t* in, size_t n);   // the whole image intact, or false
};

// The snapshot of a pending change: the bound slot's previous name and key and the previous record, bound to its job
// by id and checked with a CRC. Device-internal: the key is never reported, and the image is wiped once used.
struct MgmtSnap {
  char job[Job::ID_CAP + 1] = {0};
  uint8_t slot = 0;
  // What the change does to the slot: nothing (bind, an unbind that keeps the channel), writes a key whose fingerprint
  // is applied_fp (rotate, a guarded put), or clears it (a guarded remove, an unbind with remove). A revert puts the old
  // contents back only while the slot still shows what the change left there, so an app change in the window is never
  // overwritten; it also finds a change that a cut stopped before it was applied.
  enum : uint8_t { SLOT_NONE = 0, SLOT_KEY = 1, SLOT_CLEARED = 2 };
  uint8_t slot_change = SLOT_NONE;
  uint8_t applied_fp[MGMT_FP_BYTES] = {0};
  char name[CHANNEL_NAME_MAX + 1] = {0};
  uint8_t key[CHANNEL_SECRET_BYTES] = {0};
  MgmtRecord rec;

  static constexpr size_t FIXED = 4 + 1 + 2 + MGMT_FP_BYTES + 32 + CHANNEL_SECRET_BYTES + MgmtRecord::IMAGE + 4;
  static constexpr size_t IMAGE_MAX = FIXED + Job::ID_CAP;
  size_t serialize(uint8_t* out, size_t cap) const;
  bool parse(const uint8_t* in, size_t n);
  void clear();
};

struct MgmtHost {
  ChannelHost* channels;
  SnapStore* record;   // the binding record's file
  SnapStore* snap;     // the pending change's snapshot file
  JobService* jobs;    // null: no confirmation possible, so only the cable may change the binding
  // The fingerprint of a slot's key (false when the slot holds no key) and of a key; the public channel's fingerprint.
  bool (*slot_fp)(void* user, int slot, uint8_t out[MGMT_FP_BYTES]);
  void (*key_fp)(void* user, const uint8_t key[CHANNEL_SECRET_BYTES], uint8_t out[MGMT_FP_BYTES]);
  const uint8_t* public_fp;
  // A slot's name and key, for the snapshot only (never into a result, an event or a log).
  bool (*read_slot)(void* user, int slot, char name[CHANNEL_NAME_MAX + 1], uint8_t key[CHANNEL_SECRET_BYTES]);
  bool (*cable)(void* user);                       // the unsigned cable keeps its path and may omit confirmation
  void (*record_caller)(void* user, Job& j);
  bool (*job_id)(void* user, const Request& req, char* out, size_t cap);
  uint8_t cls;                                     // the job's class bits (1 << the write class)
  void* user;
};

enum MgmtState : uint8_t { MGMT_NONE, MGMT_BOUND, MGMT_STALE };
const uint8_t MGMT_NO_SLOT = 0xFF;   // a record that cannot be read names no slot

// The current binding and its state. A record that cannot be read is stale with no slot (MGMT_NO_SLOT), never absent,
// so nothing guards or erases a slot it may not have named.
MgmtState mgmtState(const MgmtHost& h, MgmtRecord* rec);

// OpContext.user is a MgmtHost.
Status opMgmtGet(OpContext& c);
Status opMgmtBind(OpContext& c);
Status opMgmtRotate(OpContext& c);
Status opMgmtUnbind(OpContext& c);

// channel.put and channel.remove go through this: a change of the bound slot needs confirmation from anyone but the
// cable and becomes a job (op "channel.put" / "channel.remove"); afterwards a put moves the binding to the new key
// and a remove ends it. Any other slot runs the operation as it is.
Status mgmtGuardChannel(OpContext& c, MgmtHost& h, Status (*op)(OpContext&), const char* op_name);

// The job hooks for the operations above, by exact op name; false for any other job.
bool mgmtOwnsJob(const Job& j);
bool mgmtRevert(const MgmtHost& h, const Job& j);   // puts the slot and the record back; false when it cannot
void mgmtConfirmed(const MgmtHost& h);              // the change stays; its snapshot goes
void mgmtSnapBoot(const MgmtHost& h);               // a snapshot no awaiting job owns is removed at boot

}  // namespace Iface
