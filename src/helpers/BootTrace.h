#pragma once
// An opt-in diagnostic trace on the USB UART, for builds whose Companion link is not that UART: single-line plain-text
// records, each prefixed "trace: ". Compiled only with -D MECON_BOOT_TRACE; otherwise every call below vanishes.
//
// Records carry counts, lengths, states and times, never payload bytes, keys, PINs or message text. Each is built in a
// stack buffer and written in one call.
//
// A build whose Companion protocol runs over Serial must not carry it: text would corrupt that frame stream. The
// USB-only Companion (ENABLE_USB_INTERFACE without the connectivity mode) is refused here. The standalone Companion
// also offers its USB interface on Serial, so there the trace goes quiet for good once a frame arrives from a host
// on that port (BootTrace mute, called by ArduinoSerialInterface); a host that has not spoken yet sees plain lines
// with no '>' in them, which the frame reader skips.

#ifdef MECON_BOOT_TRACE

#if defined(ENABLE_USB_INTERFACE) && !defined(ENABLE_CONNMODE)
  #error "MECON_BOOT_TRACE would write text into the USB serial Companion frame stream: not for this build"
#endif

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace mecon {
namespace trace {

inline bool& muted() { static bool m = false; return m; }

// A host speaks the frame protocol on Serial: from here on nothing but frames may go there.
inline void mute() { muted() = true; }

inline void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void line(const char* fmt, ...) {
  if (muted()) return;
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  if (!Serial) return;   // a native USB port with no host: a write would wait out its timeout for nothing
#endif
  char buf[160];
  int n = snprintf(buf, sizeof(buf), "trace: ");
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  n += m;
  if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
  buf[n++] = '\n';
  Serial.write((const uint8_t*)buf, n);
}

}  // namespace trace
}  // namespace mecon

#define MECON_TRACE(...) ::mecon::trace::line(__VA_ARGS__)
#define MECON_TRACE_MUTE() ::mecon::trace::mute()

#else

#define MECON_TRACE(...) ((void)0)
#define MECON_TRACE_MUTE() ((void)0)

#endif
