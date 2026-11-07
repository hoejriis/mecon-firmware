#pragma once
// The ESP32-S3's native USB through Arduino's HWCDC. Its transmit interrupt can stop with data waiting, and only the next
// write starts it again, so the end of an answer sat in the device until the host asked something else. A writer that goes
// on filling the 256 B ring meanwhile times out, is taken for an absent host, and has its older bytes discarded.
#if defined(ESP_PLATFORM)
#include <soc/soc_caps.h>
#endif
#if defined(ESP_PLATFORM) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && SOC_USB_SERIAL_JTAG_SUPPORTED
#define MECON_NATIVE_USB_TX 1
#include <Arduino.h>
#include <hal/usb_serial_jtag_ll.h>

// What HWCDC::write does after queueing, with nothing to queue. Called from the loop; takes no lock and never waits.
inline void nativeUsbTxNudge(uint32_t now_ms) {
  static uint32_t last = 0;
  if (now_ms - last < 50) return;
  last = now_ms;
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
}
#else
inline void nativeUsbTxNudge(uint32_t) {}
#endif
