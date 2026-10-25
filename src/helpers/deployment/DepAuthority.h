#pragma once
// Deployment authority, device side: the checks a device applies to a DAK-signed authority set and to a DAK
// rotation chain (Deployment authority contract v1, sections 4 and 9.3). Pure functions over the received bytes:
// signatures are verified over the exact text first and the JSON is parsed afterwards, so nothing is
// canonicalised. Persistence and transports live elsewhere.
#include <stddef.h>
#include <stdint.h>

namespace Dep {

const int ID_LEN = 16;          // deployment_id, instance_id: 16 bytes, 32 hex characters on the wire
const int KEY_LEN = 32;
const int SIG_LEN = 64;
const int MAX_MEMBERS = 8;
const int MAX_REVOKED = 32;
const int MAX_ROTATIONS = 8;
const size_t MAX_SET_TEXT = 3072;  // an authority set text; 8 members and 32 revoked ids fit in about 2.4 KB

// The contract's error codes (section 8) this module can produce, plus OK and DUPLICATE (an equal revision:
// acknowledged, nothing changes).
enum Err : uint8_t {
  OK = 0,
  DUPLICATE,
  BAD_AUTHORITY_SIGNATURE,
  BAD_AUTHORITY_SET,
  STALE_REVISION,
  WRONG_DEPLOYMENT,
  UNTRUSTED_AUTHORITY_KEY
};
const char* errCode(Err e);   // the wire spelling, e.g. "bad_authority_set"

struct Member {
  uint8_t instance_id[ID_LEN];
  uint8_t public_key[KEY_LEN];
  uint32_t admitted_revision;
};

struct AuthoritySet {
  uint8_t deployment_id[ID_LEN];
  uint32_t authority_epoch;
  uint32_t authority_revision;
  uint8_t n_members;
  Member members[MAX_MEMBERS];
  uint8_t n_revoked;
  uint8_t revoked[MAX_REVOKED][ID_LEN];

  int memberIndex(const uint8_t id[ID_LEN]) const;   // -1 when absent
  bool isRevoked(const uint8_t id[ID_LEN]) const;
};

// What a set is checked against: the trust root and the device's current state.
struct SetContext {
  const uint8_t* dak;                // KEY_LEN bytes: the stored DAK, or the payload's on a first provisioning
  const uint8_t* deployment_id;      // ID_LEN bytes: the stored id, or the payload's on a first provisioning
  uint32_t authority_epoch;          // after any rotation delivered with the set
  bool has_stored;                   // false on a first provisioning
  uint32_t stored_revision;          // meaningful when has_stored
};

// Section 4. text is the authority_set string exactly as received (after JSON unescaping of the wrapper), sig
// the decoded 64-byte signature. On OK or DUPLICATE, out holds the parsed set.
Err acceptSet(const char* text, size_t len, const uint8_t sig[SIG_LEN], const SetContext& ctx, AuthoritySet* out);

// The trust root as rotations move it.
struct RootState {
  uint8_t dak[KEY_LEN];
  uint32_t authority_epoch;
};

// Section 9.3: applies one rotation statement to st. On failure st is unchanged. The caller applies a chain in
// order and stops at the first failure (an UNTRUSTED_AUTHORITY_KEY for the whole message).
Err applyRotation(const char* text, size_t len, const uint8_t sig[SIG_LEN], const uint8_t deployment_id[ID_LEN],
                  RootState* st);

// A public key that must not be trusted: not a point on the curve, or a point of small order (eight times it is the
// identity), against which a signature proves nothing. Checked for every key a device would trust: a set's members,
// a rotation's new DAK, the DAK of a first provisioning.
bool weakKey(const uint8_t key[KEY_LEN]);

// Wire encodings (section 2).
bool hexToBytes(const char* hex, size_t hex_len, uint8_t* out, size_t n);   // exactly 2n chars, either case
bool base64ToSig(const char* b64, size_t len, uint8_t out[SIG_LEN]);        // exactly 88 chars with padding

}  // namespace Dep
