#pragma once

// Whether an nRF52 BLE build registers the Adafruit BLE DFU service (Issue #143). The bootloader checks no signature,
// so whoever can start a BLE DFU can install any image:
//
//   - a build with the device interface (ENABLE_MECON_IFACE) never registers it. Such a device updates through the
//     signed update path (Issue #110) or, until that exists on nRF52, over the USB cable only (the bootloader's serial
//     DFU after a 1200-baud touch, unsigned, trusted by physical possession as AUTHORITY.md says). A Deployment record
//     check would be weaker: the service is registered once, at BLE start, so a device enrolled later would keep it.
//   - any other build registers it only with a PIN the user chose and stored (NodePrefs::ble_pin != 0), never the
//     build default, the published 123456, or the random session PIN a display build draws when no PIN is stored:
//     that one is shown on the screen and at every pairing, so it is not the owner's secret.
//
// The PIN is the one BLE starts with: `set pin` takes effect at the next start, the same moment this is decided.

#include <stdint.h>

namespace BleDfuPolicy {

inline bool mayRegister(bool device_interface_build, uint32_t pin, bool pin_chosen_by_user) {
  return !device_interface_build && pin_chosen_by_user && pin != 0 && pin != 123456;
}

// The Repeater / Room server CLI's `start ota` restarts the radio stack in an OTA-only mode. A device-interface build
// refuses it outright and starts nothing. So does every nRF52 build: it would put the BLE DFU service up, and these
// roles have no BLE PIN, so the link could be neither paired with a passkey nor protected (an MITM requirement with no
// PIN would make it a silent no-op). Stock-like ESP32 builds keep upstream's behaviour.
inline bool otaStartAllowed(bool device_interface_build, bool nrf52_build) { return !device_interface_build && !nrf52_build; }

}  // namespace BleDfuPolicy
