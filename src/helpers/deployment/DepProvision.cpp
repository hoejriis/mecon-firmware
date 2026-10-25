#include "DepProvision.h"
#include <string.h>

namespace Dep {

const char* provErrCode(ProvErr e) {
  switch (e) {
    case P_OK: return "ok";
    case P_DUPLICATE: return "duplicate";
    case P_UNSUPPORTED_CONTRACT_VERSION: return "unsupported_contract_version";
    case P_WRONG_DEVICE: return "wrong_device";
    case P_WRONG_DEPLOYMENT: return "wrong_deployment";
    case P_UNTRUSTED_AUTHORITY_KEY: return "untrusted_authority_key";
    case P_BAD_AUTHORITY_SIGNATURE: return "bad_authority_signature";
    case P_BAD_AUTHORITY_SET: return "bad_authority_set";
    case P_STALE_REVISION: return "stale_revision";
    case P_SYNC_KEY_GENERATION_BEHIND: return "sync_key_generation_behind";
    case P_BAD_BROKER_ROLES: return "bad_broker_roles";
    case P_BAD_DEPLOYMENT_NAME: return "bad_deployment_name";
  }
  return "failed";
}

static ProvErr fromSetErr(Err e) {
  switch (e) {
    case BAD_AUTHORITY_SIGNATURE: return P_BAD_AUTHORITY_SIGNATURE;
    case STALE_REVISION: return P_STALE_REVISION;
    case WRONG_DEPLOYMENT: return P_WRONG_DEPLOYMENT;
    case UNTRUSTED_AUTHORITY_KEY: return P_UNTRUSTED_AUTHORITY_KEY;
    default: return P_BAD_AUTHORITY_SET;
  }
}

// Whether the payload says exactly what the stored record says (the duplicate rule: same revision, same content).
static bool sameContent(const ProvisionPayload& p, const Record& r, const char* stored_text) {
  if (p.name_len != strlen(r.deployment_name) || memcmp(p.name, r.deployment_name, p.name_len) != 0) return false;
  if (memcmp(p.dak, r.dak, KEY_LEN) != 0 || p.authority_epoch != r.authority_epoch) return false;
  if (p.set_len != r.set_len || memcmp(p.set_text, stored_text, p.set_len) != 0) return false;
  if (memcmp(p.set_sig, r.set_sig, SIG_LEN) != 0) return false;
  if (p.sync_key_generation != r.sync_key_generation || p.n_roles != r.n_roles) return false;
  for (int i = 0; i < p.n_roles; i++)
    if (p.roles[i].profile != r.roles[i].profile || p.roles[i].role != r.roles[i].role) return false;
  return true;
}

ProvErr checkProvision(const ProvisionPayload& p, const ProvisionContext& ctx, Record* out, AuthoritySet* set) {
  const bool has = ctx.current && ctx.current->state == LS_OK;
  const Record* cur = has ? &ctx.current->rec : nullptr;

  // 1. A contract version this device speaks.
  if (p.contract_version != 1) return P_UNSUPPORTED_CONTRACT_VERSION;
  // 2. Meant for this device.
  if (memcmp(p.node_public_key, ctx.own_node_key, KEY_LEN) != 0) return P_WRONG_DEVICE;
  // 3. The Deployment never changes for the life of the enrolment.
  if (has && memcmp(p.deployment_id, cur->deployment_id, ID_LEN) != 0) return P_WRONG_DEPLOYMENT;

  // 4. The trust root: pinned by the first provisioning, moved only by rotations signed by the current key.
  RootState root;
  if (has) {
    memcpy(root.dak, cur->dak, KEY_LEN);
    root.authority_epoch = cur->authority_epoch;
    // A provisioning carries no rotations: a device whose key has moved gets authority_update first, which keeps a
    // provisioning inside the message bound (docs/contract/AUTHORITY.md section 6.1).
    if (p.n_rotations > 0) return P_UNTRUSTED_AUTHORITY_KEY;
    if (memcmp(root.dak, p.dak, KEY_LEN) != 0 || root.authority_epoch != p.authority_epoch)
      return P_UNTRUSTED_AUTHORITY_KEY;
  } else {
    // Nothing to chain from: the payload's key is the one being pinned (trust on first use).
    if (p.n_rotations > 0 || weakKey(p.dak) || p.authority_epoch == 0 || p.authority_epoch > MAX_EPOCH)
      return P_UNTRUSTED_AUTHORITY_KEY;
    memcpy(root.dak, p.dak, KEY_LEN);
    root.authority_epoch = p.authority_epoch;
  }

  // 5. The authority set, against the resulting key.
  SetContext sc = { root.dak, has ? cur->deployment_id : p.deployment_id, root.authority_epoch, has,
                    has ? cur->authority_revision : 0 };
  Err se = acceptSet(p.set_text, p.set_len, p.set_sig, sc, set);
  if (se != OK && se != DUPLICATE) return fromSetErr(se);
  // An equal revision is the set already held, byte for byte; a different text or signature at the same revision is
  // two sets claiming one revision, refused rather than written over the stored one.
  if (se == DUPLICATE && (p.set_len != cur->set_len || memcmp(p.set_text, ctx.current->set_text, p.set_len) != 0 ||
                          memcmp(p.set_sig, cur->set_sig, SIG_LEN) != 0))
    return P_BAD_AUTHORITY_SET;
  if (se == OK && has && checkMembership(*cur, *set) != OK) return P_BAD_AUTHORITY_SET;
  if (p.has_migrating_issuer && set->memberIndex(p.migrating_issuer) < 0) return P_BAD_AUTHORITY_SET;

  // 6. provision_revision: higher applies; equal is a duplicate only when nothing differs.
  if (has) {
    if (p.provision_revision < cur->provision_revision) return P_STALE_REVISION;
    if (p.provision_revision == cur->provision_revision)
      return sameContent(p, *cur, ctx.current->set_text) ? P_DUPLICATE : P_STALE_REVISION;
  } else if (p.provision_revision == 0) {
    return P_STALE_REVISION;
  }
  // 7. The sync key generation never goes back.
  if (has && p.sync_key_generation < cur->sync_key_generation) return P_SYNC_KEY_GENERATION_BEHIND;
  // 8. Broker roles name stored profiles and known roles, one role per profile.
  if (p.n_roles > MAX_ROLES) return P_BAD_BROKER_ROLES;
  for (int i = 0; i < p.n_roles; i++) {
    if (p.roles[i].role > ROLE_LOCAL || !ctx.profile_exists(ctx.user, p.roles[i].profile)) return P_BAD_BROKER_ROLES;
    for (int j = 0; j < i; j++)
      if (p.roles[j].profile == p.roles[i].profile) return P_BAD_BROKER_ROLES;
  }
  // The name is display only, outside the signed set; refused rather than clamped (a clamp can split a character).
  if (p.name_len < 1 || p.name_len > (size_t)MAX_NAME || memchr(p.name, 0, p.name_len)) return P_BAD_DEPLOYMENT_NAME;

  // The record to commit.
  Record r;
  if (has) r = *cur;
  else memset(&r, 0, sizeof(r));
  r.contract_version = 1;
  memcpy(r.deployment_id, has ? cur->deployment_id : p.deployment_id, ID_LEN);
  memcpy(r.deployment_name, p.name, p.name_len);
  r.deployment_name[p.name_len] = 0;
  memcpy(r.dak, root.dak, KEY_LEN);
  r.authority_epoch = root.authority_epoch;
  r.authority_revision = set->authority_revision;
  r.provision_revision = p.provision_revision;
  r.sync_key_generation = p.sync_key_generation;
  memcpy(r.set_sig, p.set_sig, SIG_LEN);
  r.set_len = (uint16_t)p.set_len;
  r.n_roles = p.n_roles;
  memcpy(r.roles, p.roles, sizeof(r.roles));
  if (se == OK) setAdmissions(&r, *set);   // a duplicate set leaves membership as it is
  if (p.has_unlock) {
    r.has_unlock = true;
    memcpy(r.unlock_secret, p.unlock_secret, UNLOCK_LEN);
  }
  *out = r;
  return P_OK;
}

ProvErr checkAuthorityUpdate(const Loaded& current, const Rotation* rotations, uint8_t n_rotations,
                             const char* set_text, size_t set_len, const uint8_t set_sig[SIG_LEN], Record* out,
                             AuthoritySet* set) {
  if (current.state != LS_OK) return P_UNTRUSTED_AUTHORITY_KEY;   // nothing pinned to verify against
  const Record& cur = current.rec;
  RootState root;
  memcpy(root.dak, cur.dak, KEY_LEN);
  root.authority_epoch = cur.authority_epoch;
  if (n_rotations > MAX_ROTATIONS) return P_UNTRUSTED_AUTHORITY_KEY;
  for (int i = 0; i < n_rotations; i++) {
    Err e = applyRotation(rotations[i].text, rotations[i].len, rotations[i].sig, cur.deployment_id, &root);
    if (e != OK) return e == WRONG_DEPLOYMENT ? P_WRONG_DEPLOYMENT : P_UNTRUSTED_AUTHORITY_KEY;
  }
  SetContext sc = { root.dak, cur.deployment_id, root.authority_epoch, true, cur.authority_revision };
  Err se = acceptSet(set_text, set_len, set_sig, sc, set);
  // An equal revision is the set already held, byte for byte; anything else at that revision is the key signing two
  // sets for one revision, refused and never acknowledged (as for a provisioning).
  if (se == DUPLICATE) {
    if (set_len != cur.set_len || memcmp(set_text, current.set_text, set_len) != 0 ||
        memcmp(set_sig, cur.set_sig, SIG_LEN) != 0)
      return P_BAD_AUTHORITY_SET;
    return P_DUPLICATE;
  }
  if (se != OK) return fromSetErr(se);
  if (checkMembership(cur, *set) != OK) return P_BAD_AUTHORITY_SET;
  Record r = cur;
  memcpy(r.dak, root.dak, KEY_LEN);
  r.authority_epoch = root.authority_epoch;
  r.authority_revision = set->authority_revision;
  memcpy(r.set_sig, set_sig, SIG_LEN);
  r.set_len = (uint16_t)set_len;
  setAdmissions(&r, *set);
  *out = r;
  return P_OK;
}

}  // namespace Dep
