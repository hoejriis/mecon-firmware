#pragma once
// deployment_provision, device side (Deployment authority contract v1, section 5): the checks in the contract's
// order, stopping at the first failure and producing nothing on any failure, then the new record to persist in
// one commit. Pure: the caller decodes the payload, decides whether its transport may carry an unsigned first
// provisioning, persists the result and answers with values read back from storage.
#include <stddef.h>
#include <stdint.h>
#include "DepAuthority.h"
#include "DepRecord.h"

namespace Dep {

enum ProvErr : uint8_t {
  P_OK = 0,
  P_DUPLICATE,                     // same provision_revision, same content: answer with the stored read-back
  P_UNSUPPORTED_CONTRACT_VERSION,
  P_WRONG_DEVICE,
  P_WRONG_DEPLOYMENT,
  P_UNTRUSTED_AUTHORITY_KEY,
  P_BAD_AUTHORITY_SIGNATURE,
  P_BAD_AUTHORITY_SET,
  P_STALE_REVISION,
  P_SYNC_KEY_GENERATION_BEHIND,
  P_BAD_BROKER_ROLES,
  P_BAD_DEPLOYMENT_NAME
};
const char* provErrCode(ProvErr e);

struct Rotation {
  const char* text;
  size_t len;
  uint8_t sig[SIG_LEN];
};

// The decoded `deployment` object. Strings point into the request and are not NUL terminated.
struct ProvisionPayload {
  uint32_t contract_version;
  uint8_t deployment_id[ID_LEN];
  const char* name;
  size_t name_len;
  uint8_t node_public_key[KEY_LEN];
  uint32_t provision_revision;
  uint8_t dak[KEY_LEN];
  uint32_t authority_epoch;
  const char* set_text;
  size_t set_len;
  uint8_t set_sig[SIG_LEN];
  uint8_t n_rotations;
  Rotation rotations[MAX_ROTATIONS];
  uint32_t sync_key_generation;
  uint8_t n_roles;
  BrokerRoleEntry roles[MAX_ROLES];
  bool has_migrating_issuer;
  uint8_t migrating_issuer[ID_LEN];
};

struct ProvisionContext {
  const Loaded* current;            // LS_ABSENT for a first provisioning; never LS_CORRUPT (refused before this)
  const uint8_t* own_node_key;      // KEY_LEN
  bool (*profile_exists)(void* user, uint8_t index);
  void* user;
};

// On P_OK, *out is the record to persist (replay state carried for members still present) and *set the parsed
// authority set. On P_DUPLICATE nothing is to be written.
ProvErr checkProvision(const ProvisionPayload& p, const ProvisionContext& ctx, Record* out, AuthoritySet* set);

}  // namespace Dep
