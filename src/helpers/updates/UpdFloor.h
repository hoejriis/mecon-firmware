#pragma once
// The security floor (docs/contract/UPDATES.md section 4): the lowest security_version this device installs. It only
// rises, it is raised to the security version compiled into the running image once that image is proven (at boot, or
// for a fresh install once its start-up checks pass or it is confirmed: Upd::UpdateService), and a factory reset keeps
// it.
//
//   ESP32 (ESP_PLATFORM)   NVS namespace "mecon", key "secfloor". The Companion's factory reset erases all of NVS, so
//                          it reads the floor first and raises it back after the erase. A power cut between the two
//                          leaves the floor at the running image's compiled version from the next boot on.
//   simulated boards       a file beside the data directory (<dir>.floor), like the provisioned marker
//   anything else          not kept: floorRead() is 0 and floorRaise() fails. The nRF52 store comes with its update
//                          path (#110).
#include <stdint.h>

#ifndef MECON_SECURITY_VERSION
#define MECON_SECURITY_VERSION 1   // every build with the authority layer is at least 1 (UPDATES.md section 4)
#endif

namespace Upd {

// false: the store cannot be read, and callers fail closed (preflight takes the floor as unreachable). Nothing stored
// reads as 0.
bool floorRead(uint32_t* out);
// Stores max(stored, v); true once the stored value reads back at least v. An unreadable store is left as it is.
bool floorRaise(uint32_t v);
inline bool floorAtBoot() { return floorRaise(MECON_SECURITY_VERSION); }   // to this image's compiled version

}  // namespace Upd
