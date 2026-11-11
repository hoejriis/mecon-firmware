#pragma once
// audit.list {"cursor"} (OPERATIONS.md section 5.1): the audit ring's history, newest first, a page at a time, the same
// entries and order as the CLI's `get audit [start]`. The `logs` stream sends only what is recorded after it starts;
// this reads what is already there. Its class is the CLI line's and the stream's, posture: the caller's host decides.
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

const unsigned AUDIT_PAGE = 8;

// allow(user, cls): may the caller of this request use the class (the host's stream check, Cli::C_POSTURE here).
Status opAuditList(OpContext& c, bool (*allow)(void* user, uint8_t cls), void* user);

}  // namespace Iface
