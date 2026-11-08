#pragma once
// Region scopes on a Repeater or Room server (OPERATIONS.md, "Region scopes"): the native RegionMap behind named
// operations. Exact names only, the native bounds, and a few checks the console does not make (a parent that exists, no
// cycle, no removing the home or the default). Every change is saved at once, atomically, as the console's
// `region default` already is. A change a remote caller makes must ask for confirmation: it can cut that caller's path
// (the default scope carries the server's adverts and unsolicited replies, a flood flag decides what it forwards), so it
// runs as a job, with a copy of the whole map kept until the job is confirmed, and put back if it is not.

#include <stddef.h>
#include <stdint.h>
#include "ConfigSnap.h"
#include "IfaceDispatch.h"
#include "JobService.h"
#include "../RegionMap.h"

namespace Iface {

struct RegionHost {
  RegionMap* map;                                               // null: the role has no region tree
  bool (*save)(void* role_user);                                // /regions2, atomically
  void (*default_changed)(void* role_user, const RegionEntry* r);   // the role's live default scope key
  void* role_user;
  SnapStore* snap;                                              // the pending change's copy of the map
  JobService* jobs;                                             // null: no confirmation, so only the cable may change it
  bool (*cable)(void* user);                                    // the unsigned cable keeps its path and may omit confirm
  void (*record_caller)(void* user, Job& j);
  bool (*job_id)(void* user, const Request& req, char* out, size_t cap);
  uint8_t cls;                                                  // the job's class bits (1 << the write class)
  void* user;
};

const unsigned REGION_PAGE = 8;
const unsigned REGION_NAME_MAX = sizeof(((RegionEntry*)nullptr)->name) - 1;   // 30: the native field, without its NUL

Status opRegionList(OpContext& c, RegionHost& h);
Status opRegionPut(OpContext& c, RegionHost& h);
Status opRegionRemove(OpContext& c, RegionHost& h);
Status opRegionFlood(OpContext& c, RegionHost& h);
Status opRegionHome(OpContext& c, RegionHost& h);
Status opRegionDefault(OpContext& c, RegionHost& h);

bool regionOwnsJob(const Job& j);
bool regionRevert(RegionHost& h, const Job& j);   // puts the map back and saves it; false when it cannot
void regionConfirmed(RegionHost& h);              // the change stays; its copy goes
void regionSnapBoot(RegionHost& h);               // a copy no awaiting job owns is removed at boot

}  // namespace Iface
