#pragma once
// A Companion's default flood scope (OPERATIONS.md, "Region scopes"): the scope its own floods go out in, the same one the
// native frames 63 and 64 set and read. scope.default.get gives the name, never the key; scope.default.set takes a name,
// deriving a hashtag scope's key as the firmware's own default does (SHA-256 of "#name", 16 bytes), or a name and a key for
// a private one; scope.default.clear sends floods unscoped again. A remote caller must confirm: a scope the network does not
// forward would cut the Companion's reports to its manager. The previous name and key are kept until then.

#include <stddef.h>
#include <stdint.h>
#include "ConfigSnap.h"
#include "IfaceDispatch.h"
#include "JobService.h"

namespace Iface {

const unsigned SCOPE_NAME_MAX = 30;   // the native prefs field holds 31 bytes with its NUL
const unsigned SCOPE_KEY_BYTES = 16;

struct ScopeHost {
  bool (*get)(void* role_user, char name[SCOPE_NAME_MAX + 1], uint8_t key[SCOPE_KEY_BYTES]);   // false: no default scope
  bool (*set)(void* role_user, const char* name, const uint8_t key[SCOPE_KEY_BYTES]);          // null name: clear; saves
  void* role_user;
  SnapStore* snap;                                              // the pending change's previous scope
  JobService* jobs;
  bool (*cable)(void* user);
  void (*record_caller)(void* user, Job& j);
  bool (*job_id)(void* user, const Request& req, char* out, size_t cap);
  uint8_t cls;
  void* user;
};

// A scope named in a request, as scope.default.set takes it: a hashtag name ("#" optional) whose key is derived, or a
// private "$name" with its key in hex. False with c.error set (bad-name, name-too-long, bad-char, bad-key, key-required).
bool scopeOf(OpContext& c, int name_tok, int key_tok, char name[SCOPE_NAME_MAX + 1], uint8_t key[SCOPE_KEY_BYTES]);

Status opScopeGet(OpContext& c, ScopeHost& h);
Status opScopeSet(OpContext& c, ScopeHost& h);
Status opScopeClear(OpContext& c, ScopeHost& h);

bool scopeOwnsJob(const Job& j);
bool scopeRevert(ScopeHost& h, const Job& j);
void scopeConfirmed(ScopeHost& h);
void scopeSnapBoot(ScopeHost& h);

}  // namespace Iface
