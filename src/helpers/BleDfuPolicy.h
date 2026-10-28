#pragma once

// Whether an nRF52 BLE build registers the Adafruit BLE DFU service (Issue #143). The bootloader checks no signature,
// so whoever can start a BLE DFU can install any image:
//
//   - a build with the device interface (ENABLE_MECON_IFACE) never registers it. Such a device updates through the
//     signed update path (Issue #110) or, until that exists on nRF52, over the USB cable only (the bootloader's serial
//     DFU after a 1200-baud touch, unsigned, trusted by physical possession as AUTHORITY.md says). A Deployment record
//     check would be weaker: the service is registered once, at BLE start, so a device enrolled later would keep it.
//   - any other build registers it only with a PIN the user chose, never 0 (build default) or the published 123456,
//     which would let anyone in range pair and flash.
//
// The PIN is the one BLE starts with: `set pin` takes effect at the next start, the same moment this is decided.

#include <stdint.h>

namespace BleDfuPolicy {

inline bool mayRegister(bool device_interface_build, uint32_t pin) {
  return !device_interface_build && pin != 0 && pin != 123456;
}

// The Repeater / Room server CLI's `start ota` restarts BLE in an OTA-only mode with the DFU service up. A
// device-interface build refuses it outright and starts nothing; other builds keep it, with the service protected.
inline bool otaStartAllowed(bool device_interface_build) { return !device_interface_build; }

}  // namespace BleDfuPolicy
