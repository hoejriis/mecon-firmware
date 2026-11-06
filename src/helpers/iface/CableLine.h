#pragma once
// Whether the USB host is still there, on a board that can tell. On native USB (Heltec V4, the nRF52 boards) the board sees
// the host's connection. Behind a USB-UART bridge (Heltec V3) it sees nothing, so this stays true and only the unlock's
// time limit applies (AUTHORITY.md section 10).
#include <Arduino.h>
#if defined(SIM_FREE_HEAP)
  #include <stdio.h>
  #include <stdlib.h>
#endif

namespace Iface {

inline bool cableLineUp() {
#if defined(SIM_FREE_HEAP)   // the simulated board: a test says when the line is down, with a file holding 0
  const char* path = getenv("SIM_USB_LINE_FILE");
  FILE* f = path ? fopen(path, "r") : nullptr;
  if (!f) return true;
  const int c = fgetc(f);
  fclose(f);
  return c != '0';
#elif defined(NRF52_PLATFORM) || (defined(ESP_PLATFORM) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
  return (bool)Serial;
#else
  return true;
#endif
}

}  // namespace Iface
