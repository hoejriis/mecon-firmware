#pragma once
// Room for a whole frame on a cable whose write waits for the host. The nRF52 core's USB serial write loops for as long as the
// host holds the port open without reading, and the device's loop with it: radio, mesh and cable all stop (31 s seen). A frame
// is written whole once there is room for it, or not at all once `wait_ms` has passed; a partial frame would desynchronise the
// host's framing. Header-only so a native test can drive it with a port that never drains.
#include <stddef.h>
#include <stdint.h>

static const uint32_t SERIAL_TX_WAIT_MS = 20;   // a host that reads drains 256 B in well under a millisecond

template <class Port, class Clock, class Yield>
bool serialRoomFor(Port& port, size_t need, Clock now_ms, Yield yield_fn, uint32_t wait_ms = SERIAL_TX_WAIT_MS) {
  const uint32_t t0 = now_ms();
  while (port.availableForWrite() < (int)need) {
    if ((uint32_t)(now_ms() - t0) >= wait_ms) return false;
    yield_fn();
  }
  return true;
}
