#pragma once
// The Deployment record a device persists (Deployment authority contract v1, section 3), and its crash-safe
// encoding. Two copies, A and B, each framed as
//
//   "MDR1" | u32 generation | u16 payload length | payload | u32 CRC-32 (over everything before it)
//
// A write goes to the copy that does not hold the current newest record, with generation + 1, so a power cut
// leaves the previous record intact. On load the intact copy with the highest generation wins. A copy that is
// present but fails its frame check makes the record CORRUPT when no intact copy exists: never ABSENT, because
// absent means "no Deployment" and would invite an unsigned first provisioning (contract sections 11 and 12).
//
// Pure functions over byte buffers; the file system glue is per platform.
#include <stddef.h>
#include <stdint.h>
#include "DepAuthority.h"

namespace Dep {

// A member's place in its strictly increasing sequence (docs/contract/AUTHORITY.md section 8): the highest job id
// accepted. MARK_BLOCKED refuses every id: the state of a member whose real mark was lost, until it is re-admitted.
struct Watermark {
  uint32_t mark;
};
const uint32_t MARK_BLOCKED = 0xFFFFFFFFu;

// A current member's admission, kept in the record so that membership checks need no parse of the set text.
struct Admission {
  uint8_t instance_id[ID_LEN];
  uint32_t admitted_revision;
};

const int UNLOCK_LEN = 32;
const int MAX_NAME = 64;   // deployment_name: 1 to 64 UTF-8 bytes (contract section 13)
const int MAX_ROLES = 10;  // broker_roles: at most one per MQTT profile

enum BrokerRole : uint8_t { ROLE_PRIMARY = 0, ROLE_SECONDARY = 1, ROLE_LOCAL = 2 };

struct BrokerRoleEntry {
  uint8_t profile;   // MQTT profile index
  uint8_t role;      // BrokerRole
};

struct Record {
  uint8_t contract_version;
  uint8_t deployment_id[ID_LEN];
  char deployment_name[MAX_NAME + 1];
  uint8_t dak[KEY_LEN];
  uint32_t authority_epoch;
  uint32_t authority_revision;       // the stored set's, kept beside its text so checks need no re-parse
  uint32_t provision_revision;
  uint32_t sync_key_generation;
  bool has_unlock;
  uint8_t unlock_secret[UNLOCK_LEN]; // the local unlock key itself: an HMAC challenge needs it (contract section 10)
  uint8_t set_sig[SIG_LEN];
  uint16_t set_len;                  // the signed set text lives in the frame; see encodeRecord
  uint8_t n_roles;
  BrokerRoleEntry roles[MAX_ROLES];  // transport metadata only, never authority
  uint8_t n_adm;
  Admission adm[MAX_MEMBERS];        // the stored set's members and their admitted_revision
};

const size_t FRAME_OVERHEAD = 4 + 4 + 2 + 4;
const size_t MAX_PAYLOAD = 1 + ID_LEN + 1 + MAX_NAME + KEY_LEN + 4 + 4 + 4 + 4 + 1 + 32 + SIG_LEN + 2 + MAX_SET_TEXT +
                           1 + MAX_ROLES * 2 + 1 + MAX_MEMBERS * (ID_LEN + 4);
const size_t MAX_FRAME = FRAME_OVERHEAD + MAX_PAYLOAD;

// Writes the frame for rec with its set text into out. Returns the frame length, 0 when it does not fit or a
// field is out of range (a name over 64 bytes, a set over MAX_SET_TEXT).
size_t encodeRecord(const Record& rec, const char* set_text, uint32_t generation, uint8_t* out, size_t cap);

// Checks one copy's frame. On success fills rec, points *set_text into buf (not NUL terminated, rec.set_len
// bytes) and *generation. False for anything that is not a complete, intact frame.
bool decodeRecord(const uint8_t* buf, size_t len, Record* rec, const char** set_text, uint32_t* generation);

enum LoadState : uint8_t {
  LS_ABSENT = 0,   // neither copy exists: a device with no Deployment
  LS_OK,           // at least one intact copy
  LS_CORRUPT       // something exists, nothing intact
};

struct Copy {
  const uint8_t* data;   // nullptr when the file does not exist
  size_t len;            // an existing empty file is present (and not intact)
};

struct Loaded {
  LoadState state;
  int slot;               // 0 = A, 1 = B; valid when LS_OK
  uint32_t generation;
  Record rec;
  const char* set_text;   // into the winning copy's buffer
};

void loadRecord(const Copy& a, const Copy& b, Loaded* out);

// Where the next write goes and with which generation: the copy that does not hold the winner. With nothing
// intact (absent or corrupt) it is A at generation 1.
void nextWrite(const Loaded& cur, int* slot, uint32_t* generation);

// The order in which to delete the two copies (of the record or the marks) when removing them: the older first, so a
// power cut between the two deletions leaves the newer copy, never resurrects the older one. newer_slot is the slot that
// loaded as the winner, or -1 when none did (then A, B).
inline void removalOrder(int newer_slot, int order[2]) {
  const bool known = newer_slot == 0 || newer_slot == 1;
  order[0] = known ? 1 - newer_slot : 0;
  order[1] = known ? newer_slot : 1;
}

// Membership rules for a new authority set against the stored record (contract sections 4 and 7): a member the device
// does not hold must have been admitted after the device's stored revision (a removed and re-added Backend carries the
// revision of its re-admission, never an older one), and a member it holds may not have its admitted_revision lowered.
// BAD_AUTHORITY_SET otherwise. Not applied to a first provisioning, which has nothing stored.
Err checkMembership(const Record& cur, const AuthoritySet& set);

// The record's admissions from a newly accepted set.
void setAdmissions(Record* rec, const AuthoritySet& set);

// ---- The sequence marks: a small record of their own, so a signed request rewrites tens of bytes and not the whole
// record with its set text (flash wear, notably on nRF52's 28 KB file system). Two copies framed like the record,
//
//   "MDM1" | u32 generation | u16 payload length | payload | u32 CRC-32
//
// written record first, marks second. So the marks may lag the record (a cut between the two writes), or lead it (the
// record fell back to an older copy). reconcileMarks settles both towards refusing, never towards reopening.
struct MemberMark {
  uint8_t instance_id[ID_LEN];
  uint32_t admitted_revision;   // the admission this mark belongs to
  Watermark wm;
};

struct Marks {
  uint32_t revision;            // the authority_revision of the set these marks were last reconciled against
  uint8_t n;
  MemberMark m[MAX_MEMBERS];
  int indexOf(const uint8_t id[ID_LEN]) const;   // -1 when absent
};

const size_t MARKS_FRAME = FRAME_OVERHEAD + 4 + 1 + MAX_MEMBERS * (ID_LEN + 4 + 4);

size_t encodeMarks(const Marks& m, uint32_t generation, uint8_t* out, size_t cap);
bool decodeMarks(const uint8_t* buf, size_t len, Marks* m, uint32_t* generation);

struct LoadedMarks {
  LoadState state;   // LS_OK, LS_CORRUPT (present, nothing intact) or LS_ABSENT
  int slot;
  uint32_t generation;
  Marks marks;
};
void loadMarks(const Copy& a, const Copy& b, LoadedMarks* out);
void nextMarksWrite(const LoadedMarks& cur, int* slot, uint32_t* generation);

// The marks for set, from what was stored (nullptr when the marks were lost: absent or corrupt while a record exists).
//  - a member present in both at the same admission keeps its mark;
//  - a member re-admitted (the set's admitted_revision above the stored one) starts at 0: check 6 refuses everything it
//    signed before;
//  - a stored admission above the set's (the record fell back to an older copy) keeps its mark;
//  - a member missing from the stored marks starts at 0 only if it was admitted after the revision those marks were
//    reconciled against; otherwise its mark was lost, and it is blocked;
//  - with no stored marks at all, every member is blocked.
// Returns the number of members left blocked.
int reconcileMarks(const Marks* stored, const AuthoritySet& set, Marks* out);

}  // namespace Dep
