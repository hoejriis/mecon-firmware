#pragma once

// How this firmware names itself (docs/VERSIONING_AND_RELEASES.md). One source: the build script bin/mecon_version.py
// passes `git describe --tags --match 'v[0-9]*' --dirty` as -D MECON_VERSION; everything below is derived from it.
//
//   describe                  full (ver, get mecon.version)        short (device-info frame, display)
//   v0.1.2                    v1.18.0-mecon.0.1.2                  v1.18.0-mecon.0.1.2
//   v0.1.2-5-gabc1234         v1.18.0-mecon.0.1.2+5.gabc1234       v1.18.0-mecon.0.1.2
//   v0.1.2-5-gabc1234-dirty   v1.18.0-mecon.0.1.2+5.gabc1234.dirty v1.18.0-mecon.0.1.2
//   v0.1.2-dirty              v1.18.0-mecon.0.1.2+dirty            v1.18.0-mecon.0.1.2
//   unknown (no git, no tag)  v1.18.0-mecon.unknown                v1.18.0-mecon.unk
//
// The upstream base stays first so an app that reads the leading "v1.18.0" still does. The short form is what the
// Companion device-info frame has room for (a 20-byte field: 19 characters and the terminator); it is cut at 19.
// FIRMWARE_VER_CODE (the protocol level) is a different number and is never touched here.

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef MECON_UPSTREAM_BASE
#define MECON_UPSTREAM_BASE "v1.18.0"
#endif

#ifndef MECON_VERSION
#define MECON_VERSION "unknown"
#endif

#define MECON_VERSION_SHORT_MAX 19

namespace mecon {

namespace detail {
inline bool isHex(const char* s, size_t n) {
  if (n == 0) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
}  // namespace detail

// Builds both forms from a describe string. `full` and `shortv` are always terminated; a size of 0 skips that one.
inline void formatVersion(const char* describe, const char* upstream, char* full, size_t fullN, char* shortv, size_t shortN) {
  char tag[40];
  char extra[40];
  extra[0] = 0;
  tag[0] = 0;
  size_t n = describe ? strlen(describe) : 0;
  bool ok = n >= 2 && describe[0] == 'v' && describe[1] >= '0' && describe[1] <= '9';
  if (!ok) {
    strcpy(tag, "unknown");
  } else {
    bool dirty = false;
    if (n > 6 && strcmp(describe + n - 6, "-dirty") == 0) { dirty = true; n -= 6; }
    // v<tag>-<N>-g<hash>: the last "-g", then the "-<digits>" before it
    size_t g = n;
    for (size_t i = n; i >= 3; i--) {
      if (describe[i - 2] == '-' && describe[i - 1] == 'g') { g = i - 2; break; }
    }
    bool dev = false;
    size_t tagEnd = n;
    if (g < n && detail::isHex(describe + g + 2, n - g - 2)) {
      size_t d = g;
      while (d > 1 && describe[d - 1] >= '0' && describe[d - 1] <= '9') d--;
      if (d < g && d > 1 && describe[d - 1] == '-') {
        dev = true;
        tagEnd = d - 1;
        snprintf(extra, sizeof(extra), "+%.*s.%.*s", (int)(g - d), describe + d, (int)(n - g - 1), describe + g + 1);
      }
    }
    size_t tl = tagEnd - 1;
    if (tl > sizeof(tag) - 1) tl = sizeof(tag) - 1;
    memcpy(tag, describe + 1, tl);
    tag[tl] = 0;
    if (dirty) strncat(extra, dev ? ".dirty" : "+dirty", sizeof(extra) - strlen(extra) - 1);
  }
  const char* base = upstream ? upstream : MECON_UPSTREAM_BASE;
  if (full && fullN) snprintf(full, fullN, "%s-mecon.%s%s", base, tag, extra);
  if (shortv && shortN) {
    snprintf(shortv, shortN, "%s-mecon.%s", base, strcmp(tag, "unknown") == 0 ? "unk" : tag);   // "unknown" would not fit
    if (shortN > MECON_VERSION_SHORT_MAX + 1) shortv[MECON_VERSION_SHORT_MAX] = 0;
  }
}

// The running build's strings, formatted once.
inline const char* versionFull() {
  static char buf[96];
  static bool done = false;
  if (!done) { formatVersion(MECON_VERSION, MECON_UPSTREAM_BASE, buf, sizeof(buf), NULL, 0); done = true; }
  return buf;
}
inline const char* versionShort() {
  static char buf[MECON_VERSION_SHORT_MAX + 1];
  static bool done = false;
  if (!done) { formatVersion(MECON_VERSION, MECON_UPSTREAM_BASE, NULL, 0, buf, sizeof(buf)); done = true; }
  return buf;
}

}  // namespace mecon
