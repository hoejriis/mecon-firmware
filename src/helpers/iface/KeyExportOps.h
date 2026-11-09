#pragma once
// identity.key.export (OPERATIONS.md 5.1; SECURITY_AND_AUTHORITY.md "Device identity and private-key custody"; the owner's
// decision of 2026-10-29, "Fork rule once enrolled"):
//   no Deployment record: the key in clear, to the cable, and on a Companion to a phone paired with the device's own PIN
//     in Normal (stock behaviour); never to a broker or over LoRa;
//   a healthy record: only to a member's signed request (with the link's nonce: DepWrapper, sensitive), and wrapped for
//     that member (KeyWrap.h), never in clear;
//   a corrupt record: the cable, unsigned, in clear (no authority can be verified);
//   Hardened: refused on every path.
// The handler decides all of it, so the operation is never answered from the result cache.

#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"
#include "../cli/CliPolicy.h"
#include "../deployment/DepRecord.h"

namespace mesh { class LocalIdentity; }

namespace Iface {

struct KeyExportHost {
  const mesh::LocalIdentity* id;
  bool build_allows;          // the build's own switch for key export (ENABLE_PRIVATE_KEY_EXPORT on a Companion)
  bool phone_allowed;         // a paired phone may export on a device with no record (Companion only)
  Cli::Principal caller;
  bool hardened;
  Dep::LoadState record;
  const uint8_t* member_key;  // the signing member's public key and instance id, during a signed request; else null
  const uint8_t* member_id;
  uint32_t job_id;
};

Status opKeyExport(OpContext& c, KeyExportHost& h);

}  // namespace Iface
