#pragma once
// The provisioned marker (docs/contract/AUTHORITY.md section 4): one byte, written at the first provisioning, that
// lives OUTSIDE the file system holding the record, because a file system that fails to mount may be formatted by the
// platform. With both record copies gone and the marker present the record is corrupt, never absent: losing both files
// must not reopen an unsigned first provisioning.
//
//   ESP32 (ESP_PLATFORM)  NVS namespace "mecon", key "prov"
//   simulated boards      a file beside the data directory (<dir>.prov), not inside it, so a simulated format leaves it
//   nRF52 with MECON_MARKER_PAGE   that flash page, a slot log (DepMarkerPage.h); the env names it only where the page is
//                         in the bootloader's preserved region and in no file system of the build
//   anything else         unsupported (including the T1000-E Companion, whose ExtraFS covers the preserved region)
//
// Only deployment.reset and the factory reset clear it. A file system format does not.
#include <stdint.h>
#include "DepRecord.h"

namespace Dep {

enum MarkerState : uint8_t {
  MK_ABSENT = 0,
  MK_PRESENT,       // also the answer when the store cannot be read: fail closed
  MK_UNSUPPORTED    // this platform has nowhere outside the file system to keep it
};

MarkerState markerRead();
bool markerSet();     // true once the marker is stored (and reads back as present)
bool markerClear();   // true once the marker is gone (and reads back as absent)

// The load state of the record given the marker: nothing found (no copy of the record exists) but the marker present is
// corrupt, never absent. Every other combination is the record's own state.
inline LoadState withMarker(LoadState ls, MarkerState m) { return (ls == LS_ABSENT && m == MK_PRESENT) ? LS_CORRUPT : ls; }

}  // namespace Dep
