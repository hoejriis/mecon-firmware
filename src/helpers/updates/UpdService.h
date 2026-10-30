#pragma once
// The update operations that stage and report (docs/contract/UPDATES.md section 8): update.manifest and update.status.
// One instance per device interface; the Companion and the Repeater and Room server console share the code.
//
// update.manifest keeps up to two manifests in RAM, keyed by their SHA-256, for an update.offer to name. A stage
// verifies the release signature and the target at once (preflight steps 5 to 7) and answers what the rest of
// preflight would say. The slots are heap, freed on eviction; a reboot empties them.
#include <stddef.h>
#include <stdint.h>
#include "UpdPreflight.h"
#include "helpers/iface/IfaceDispatch.h"

#ifndef MECON_LAYOUT
#define MECON_LAYOUT 1           // the partition or flash-layout version this build is made for
#endif
#ifndef MECON_DATA_FORMAT
#define MECON_DATA_FORMAT 1      // the version of the on-flash data this build reads and writes (section 9)
#endif
#ifndef MECON_HARDWARE_REV
#define MECON_HARDWARE_REV "1"   // this board's revision, as a manifest's hardware_rev lists it
#endif
#ifndef MECON_VARIANT
#define MECON_VARIANT "unknown"  // the build environment; bin/mecon_version.py passes it
#endif

namespace Upd {

class UpdateService {
public:
  static const uint32_t STAGE_GAP_MS = 1000;   // at most one stage a second: each costs a signature check
  static const uint32_t HEAP_NEEDED = 48 * 1024;

  struct Slot {
    char* text;            // the manifest's bytes, heap; nullptr when free
    uint16_t len;
    uint8_t sha256[32];
    uint8_t sig[SIG_LEN];
    uint32_t staged_ms;
  };

  // hardware and role as hello reports them; both outlive the service.
  void begin(const char* hardware, const char* role);

  // { "manifest": "<text>", "manifest_sig": "<base64>" } -> { "sha256", "version", "preflight" }
  Iface::Status opManifest(Iface::OpContext& c, uint32_t now_ms);
  // { "job": null, "security_floor", "staged": [{ "sha256", "version" }] }
  Iface::Status opStatus(Iface::OpContext& c);

  // The staged manifest with this hash, or nullptr (manifest-not-staged).
  const Slot* staged(const uint8_t sha256[32]) const;

  // The device's facts for preflight, read now (heap, slot, floor and running version included).
  Device device() const;

private:
  Slot _s[2] = {};
  uint32_t _last_stage_ms = 0;
  bool _staged_once = false;
  const char* _hardware = "";
  const char* _role = "";
};

}  // namespace Upd
