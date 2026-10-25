#pragma once
// Received packets for the observations stream (docs/contract/TELEMETRY.md): each role's logRxRaw() hands every packet
// here as the radio heard it, and the device interface glue, when present, registers itself as the sink. No
// dependencies, so every role's mesh code can include it. With no sink, or no subscriber, a call costs one test.
#include <stdint.h>

namespace Iface {

typedef void (*ObserveFn)(void* user, float snr, float rssi, const uint8_t* raw, int len);

struct ObserveSink {
  ObserveFn fn = nullptr;
  void* user = nullptr;
};

inline ObserveSink& observeSink() {
  static ObserveSink sink;
  return sink;
}

inline void observe(float snr, float rssi, const uint8_t* raw, int len) {
  ObserveSink& s = observeSink();
  if (s.fn) s.fn(s.user, snr, rssi, raw, len);
}

}  // namespace Iface
