#pragma once

// A GNSS receiver's state for the runtime stream's `gnss` object (docs/contract/TELEMETRY.md): whether it is
// powered, whether it has a fix, how many satellites the fix used and how long ago it was. Never a position: that is
// the private `location` stream. A board without a receiver does not fill one, and the object is then absent.

#include <stdint.h>

struct GnssHealth {
  bool powered = false;
  int8_t fix = -1;           // 1 fix, 0 no fix, -1 unknown (the receiver is powered off): written as null
  uint8_t sats = 0;          // satellites used in the last fix
  int32_t fix_age_s = -1;    // seconds since the last valid fix, -1 when there has been none this boot: null
};
