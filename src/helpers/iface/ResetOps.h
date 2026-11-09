#pragma once
// device.reset {"scope"} (OPERATIONS.md section 3.5): removes exactly one named scope and keeps everything else. The class is
// the privilege table's destructive one, so the unsigned cable may ask on any device and paired BLE on one with no
// Deployment record; no signature and no broker session may. The role does the removal; every scope is saved at once and
// the device reboots about 1.5 s after the answer, so nothing runs half reset.

#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

enum ResetScope : uint8_t { RS_NETWORK, RS_CONTACTS, RS_CHANNELS, RS_RADIO, RS_BONDS, RS_DEPLOYMENT, RS_IDENTITY, RS_FACTORY, RS_COUNT };

struct ResetHost {
  uint16_t scopes;                                             // bit per ResetScope the role removes
  bool (*reset)(void* user, ResetScope s, uint8_t caller);     // false: it could not be saved; caller: Posture::CALLER_*
  void (*reboot_soon)(void* user);
  uint8_t caller;
  void* user;
};

Status opDeviceReset(OpContext& c, ResetHost& h);

}  // namespace Iface
