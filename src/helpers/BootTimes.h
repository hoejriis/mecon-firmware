#pragma once
// Boot timing for `get mecon.boot`: millis() at three points of setup(), recorded on the board itself.
// A serial log is not a reliable instrument for this: opening the native USB port of an ESP32-S3 resets it, and the
// port re-enumerates during boot, so the log of the boot being measured is easily lost.
//   store   the contact store and prefs are loaded (DataStore::begin() returned)
//   mesh    the_mesh.begin() returned
//   setup   the end of setup(), before the first loop()
// A stage a role does not mark reads n/a (0 is a real reading: a simulated board's clock starts at 0).
#include <stdint.h>
#include <stdio.h>

namespace mecon {
namespace boot {

enum Stage : uint8_t { STORE = 0, MESH = 1, SETUP = 2, N_STAGES = 3 };

inline uint32_t* marks() {
  static uint32_t m[N_STAGES] = {0, 0, 0};
  return m;
}
inline uint8_t& marked() {
  static uint8_t bits = 0;
  return bits;
}
inline void mark(Stage s, uint32_t ms) { marks()[s] = ms; marked() |= (uint8_t)(1u << s); }
inline bool isMarked(Stage s) { return (marked() >> s) & 1u; }
inline uint32_t at(Stage s) { return marks()[s]; }

// The reply to `get mecon.boot`.
inline void format(char* reply, size_t max) {
  char v[N_STAGES][12];
  for (uint8_t i = 0; i < N_STAGES; i++) {
    if (isMarked((Stage)i)) snprintf(v[i], sizeof(v[i]), "%lu", (unsigned long)at((Stage)i));
    else snprintf(v[i], sizeof(v[i]), "n/a");
  }
  snprintf(reply, max, "> store_ms=%s mesh_ms=%s setup_ms=%s", v[STORE], v[MESH], v[SETUP]);
}

}  // namespace boot
}  // namespace mecon
