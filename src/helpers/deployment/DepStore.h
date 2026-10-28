#pragma once
// Where the Deployment record's four files live (docs/contract/AUTHORITY.md section 4): the record's two copies and the
// sequence marks' two copies, A and B each. A role provides one of these over its own file system (the Companion's
// DataStore, the servers' FILESYSTEM); the layer above (Iface::DeployAuth) never touches a file system itself.
#include <stddef.h>
#include <stdint.h>

namespace Dep {

enum StoreFile : uint8_t { SF_RECORD = 0, SF_MARKS = 1 };

class CopyStore {
public:
  virtual ~CopyStore() {}
  // slot 0 = A, 1 = B. *exists says whether the file is there at all, apart from whether it could be read whole: an
  // existing copy that cannot be read makes the record corrupt, never absent.
  virtual bool read(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, StoreFile which) = 0;
  virtual bool write(int slot, const uint8_t* frame, size_t len, StoreFile which) = 0;
  // Removes both copies, the older first (newer_slot: the slot that loaded as the winner, -1 when none did), with any
  // .tmp / .new beside them. True once neither exists.
  virtual bool removeBoth(int newer_slot, StoreFile which) = 0;
};

}  // namespace Dep
