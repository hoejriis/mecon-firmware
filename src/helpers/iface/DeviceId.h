#pragma once
// This boot's device_id as 32 lowercase hex characters, for `get device.id` (docs/contract/DEVICE_INTERFACE.md
// section 1). Set by bootDeviceMeta(); empty on a build without the device interface. No dependencies, so the CLI
// of every role can include it.

namespace Iface {

inline char* deviceIdHexBuf() {
  static char hex[33] = {0};
  return hex;
}
inline const char* deviceIdHex() { return deviceIdHexBuf(); }

}  // namespace Iface
