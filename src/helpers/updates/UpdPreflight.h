#pragma once
// Preflight's checks on the manifest against the device (docs/contract/UPDATES.md section 4, steps 5 to 12). Pure and
// platform-free, so the native tests drive every refusal. Steps 1 to 4 (authority, rate, one job, the staged manifest)
// depend on the request and the job table and are the operation's; it runs these after them.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "UpdManifest.h"

namespace Upd {

// What the device is, as the manifest must name it.
struct Device {
  const char* line;          // "public"
  const char* hardware;      // hello's hardware
  const char* hardware_rev;  // this board's revision
  const char* role;          // hello's role
  const char* variant;       // the build environment
  uint32_t layout;           // the partition or flash-layout version this build runs
  bool has_bootloader;       // the target reports a bootloader version
  uint32_t bootloader;
  uint8_t kinds;             // supported paths: a bitmask of (1 << Kind)
  uint32_t security_floor;
  bool running_known;        // the running version orders (parseVersion succeeded)
  Version running;
  uint32_t slot_size;        // bytes the update slot holds; 0 where the target has none (bootloader-dfu)
  uint32_t free_heap;
  uint32_t heap_needed;      // what a transfer needs
};

// The outcome of a check: ok, or a refusal with its status and error code (the contract's names).
struct Verdict {
  bool ok;
  const char* status;   // "refused" or "busy"
  const char* error;
};

inline Verdict pass() { return {true, nullptr, nullptr}; }
inline Verdict refuse(const char* e) { return {false, "refused", e}; }

// Step 5: the key is a compiled-in anchor (of this line), and allowed when the record narrows the anchors.
// allowed: the record's allowed key ids (8 bytes each), n_allowed 0 meaning every anchor.
inline Verdict checkKey(const Manifest& m, const uint8_t* allowed, size_t n_allowed, const Anchor** out) {
  const Anchor* a = findAnchor(m.key_id);
  if (!a || strcmp(a->line, m.line) != 0) return refuse("untrusted-release-key");
  if (n_allowed) {
    bool listed = false;
    for (size_t i = 0; i < n_allowed && !listed; i++) listed = memcmp(allowed + 8 * i, m.key_id, 8) == 0;
    if (!listed) return refuse("untrusted-release-key");
  }
  if (out) *out = a;
  return pass();
}

// Step 7: the target. Everything the image must match, and a path this device supports.
inline Verdict checkTarget(const Manifest& m, const Device& d) {
  bool rev = false;
  for (int i = 0; i < m.n_hardware_rev && !rev; i++) rev = strcmp(m.hardware_rev[i], d.hardware_rev) == 0;
  if (strcmp(m.line, d.line) != 0 || strcmp(m.hardware, d.hardware) != 0 || !rev || strcmp(m.role, d.role) != 0 ||
      strcmp(m.variant, d.variant) != 0 || m.layout != d.layout || !(d.kinds & (1u << m.kind)))
    return refuse("wrong-target");
  if (m.has_bootloader_min && d.has_bootloader && d.bootloader < m.bootloader_min) return refuse("wrong-target");
  return pass();
}

// Steps 8 to 12, in order. deployment_signed: the offer is Deployment-signed (a downgrade needs it).
inline Verdict checkInstall(const Manifest& m, const Device& d, bool deployment_signed) {
  if (m.security_version < d.security_floor) return refuse("below-security-floor");   // 8
  Version v, min_from;
  // 9. A running version that does not order, or a min_from that does not, cannot show it is at least min_from.
  if (!d.running_known || !parseVersion(m.min_from, &min_from) || compareVersion(d.running, min_from) < 0)
    return refuse("below-min-from");
  // 10. Never below the running version, unless the manifest allows it and a Deployment member signed the offer.
  if (!parseVersion(m.version, &v)) return refuse("downgrade-refused");
  if (compareVersion(v, d.running) < 0 && !(m.downgrade && deployment_signed)) return refuse("downgrade-refused");
  if (m.kind == KIND_APP && m.image_size > d.slot_size) return refuse("too-large");   // 11
  if (d.free_heap < d.heap_needed) return {false, "busy", "low-memory"};                // 12
  return pass();
}

}  // namespace Upd
