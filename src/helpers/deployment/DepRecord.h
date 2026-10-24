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

// Job classes with their own replay window per issuer (contract section 7). The public line has two today;
// the slot count keeps room for the contract's backfill and OTA classes without a format change.
enum JobClass : uint8_t { JC_CMD = 0, JC_SETTINGS = 1 };
const int JOB_CLASSES = 4;

struct Watermark {
  uint32_t mark;   // highest job id accepted
  uint32_t mask;   // bit i set: mark - 1 - i was accepted (a window of 24 is used, contract section 7)
};

struct IssuerReplay {
  uint8_t instance_id[ID_LEN];
  Watermark wm[JOB_CLASSES];
};

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
  bool has_usb_auth;
  uint8_t usb_auth_hash[32];
  uint8_t set_sig[SIG_LEN];
  uint16_t set_len;                  // the signed set text lives in the frame; see encodeRecord
  uint8_t n_roles;
  BrokerRoleEntry roles[MAX_ROLES];  // transport metadata only, never authority
  uint8_t n_replay;
  IssuerReplay replay[MAX_MEMBERS];  // only for current members (contract section 7)
};

const size_t FRAME_OVERHEAD = 4 + 4 + 2 + 4;
const size_t MAX_PAYLOAD = 1 + ID_LEN + 1 + MAX_NAME + KEY_LEN + 4 + 4 + 4 + 4 + 1 + 32 + SIG_LEN + 2 + MAX_SET_TEXT +
                           1 + MAX_ROLES * 2 + 1 + MAX_MEMBERS * (ID_LEN + JOB_CLASSES * 8);
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

// The record's members' replay state after an authority set change: kept for members still present, dropped for
// members removed, empty for members added (contract section 7).
void reconcileReplay(Record* rec, const AuthoritySet& set);

}  // namespace Dep
