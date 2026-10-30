#pragma once
// The release manifest (docs/contract/UPDATES.md section 4): one JSON object, signed as text by a release key with
// Ed25519 over "MECON-RELEASE-v1" 0x00 <text>. The bytes verified are the bytes received; nothing is canonicalised.
// At version 1 a manifest with a key not listed here, or a key given twice, is refused (bad-manifest).
#include <stddef.h>
#include <stdint.h>
#include "ReleaseAnchors.h"

namespace Upd {

const size_t MANIFEST_MAX = 600;   // bytes of manifest text
const size_t SIG_LEN = 64;

enum Kind : uint8_t { KIND_APP = 1, KIND_BOOTLOADER_DFU = 2 };

const int MAX_HW_REVS = 4;

struct Manifest {
  uint8_t key_id[8];
  char line[17];
  char version[40];          // as hello's firmware, e.g. "v1.18.0-mecon.0.2.0"
  char min_from[40];         // the lowest running version that may install it
  char build[41];            // the commit it was built from
  char upstream[24];         // informational
  char interface_[8];        // informational
  char hardware[33];
  char hardware_rev[MAX_HW_REVS][17];
  uint8_t n_hardware_rev;
  char role[17];
  char variant[49];
  uint32_t layout;
  bool has_bootloader_min;
  uint32_t bootloader_min;
  uint32_t data_format;
  uint32_t security_version;
  uint32_t image_size;
  uint8_t image_sha256[32];
  Kind kind;
  bool downgrade;
  uint32_t issued_at;        // for the record only
};

// Parses the manifest text strictly. false: bad-manifest.
bool parseManifest(const char* text, size_t len, Manifest* out);

// The anchor with this key_id among the build's anchors (production, then the test anchors where compiled in), or
// nullptr: untrusted-release-key.
const Anchor* findAnchor(const uint8_t key_id[8]);

// Ed25519 over "MECON-RELEASE-v1" 0x00 text, strict (DepVerify.h). false: bad-release-signature.
bool verifyManifest(const char* text, size_t len, const uint8_t sig[SIG_LEN], const uint8_t key[32]);

// Version order within a line. A version string is ordered by its MECON part, the "x.y.z" after "-mecon." (or the whole
// string when it has none): numeric components, then a release above a pre-release of the same numbers ("0.2.0" >
// "0.2.0-rc.1"). A development suffix ("+5.gabc", "+dirty") is ignored, so a build after a tag compares as that tag.
// false when the string has no x.y.z: such a version is never ordered, and preflight treats it as unknown.
struct Version {
  uint32_t n[3];
  bool pre;          // a "-..." pre-release before any "+"
  char pre_tag[24];  // compared as text when both are pre-releases of the same numbers
};
bool parseVersion(const char* s, Version* out);
int compareVersion(const Version& a, const Version& b);   // <0, 0, >0

}  // namespace Upd
