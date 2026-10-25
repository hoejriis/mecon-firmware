#pragma once
// Cayenne LPP, read back into named readings for the environment stream (docs/contract/TELEMETRY.md). The sensor
// managers already produce LPP for the mesh's own telemetry; this turns those bytes into {channel, quantity, value,
// unit} so a telemetry consumer needs no LPP decoder. Pure: no dependency on the LPP library.
#include <stddef.h>
#include <stdint.h>
#include "IfaceJson.h"

namespace Iface {

// Appends one object per reading to an open JSON array. A GPS entry is skipped (location is its own, private stream).
// Returns the number of readings written, or -1 when the bytes are not well-formed LPP (an unknown type or a short
// entry), in which case nothing after the bad entry is written.
int decodeLpp(const uint8_t* buf, size_t len, JsonWriter& w);

}  // namespace Iface
