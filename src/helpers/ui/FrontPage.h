#pragma once
// The operational front page shared by the Companion and the Repeater (docs/UI.md): below the stock name, battery and page
// dots, five rows of measured state. Pure formatting, so it runs in the native unit tests; each role fills State from what
// it knows and draws the rows with its own display driver. Nothing is shown that was not measured.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace FrontPage {

static const int ROWS = 5;
static const int COLS = 21;   // 128 px in the 6 px default font

enum Mode : uint8_t { MODE_WIFI, MODE_BLE, MODE_USB };

struct State {
  bool hardened = false;
  const char* role = "";          // "Companion", "Repeater"
  bool rx_seen = false;           // a LoRa packet was received since boot
  uint32_t rx_age_s = 0;          // how long ago, when rx_seen
  Mode mode = MODE_USB;           // the operating link: Wi-Fi, BLE, or neither (USB only)
  bool wifi_connected = false;
  const char* ssid = nullptr;     // the joined network, while wifi_connected
  bool ble_connected = false;     // a central holds the BLE link
  bool usb_connected = false;     // a host is talking on the cable
  bool mqtt_connected = false;    // this device's own broker session; never a Reader's
  const char* broker = nullptr;   // its host, while mqtt_connected
};

typedef char Row[COLS + 1];

// Copies text into a row. Longer text keeps its first COLS - 2 bytes and ends "..", never splitting a UTF-8 character.
inline void clip(Row out, const char* text) {
  const size_t n = strlen(text);
  if (n <= (size_t)COLS) { memcpy(out, text, n + 1); return; }
  size_t keep = COLS - 2;
  while (keep > 0 && ((uint8_t)text[keep] & 0xC0) == 0x80) keep--;   // text[keep] would start the cut-off part
  memcpy(out, text, keep);
  memcpy(out + keep, "..", 3);
}

// "10 sec ago", "2 min ago", "5 h ago", "3 days ago".
inline void age(uint32_t s, char* out, size_t cap) {
  if (s < 60) snprintf(out, cap, "%lu sec ago", (unsigned long)s);
  else if (s < 3600) snprintf(out, cap, "%lu min ago", (unsigned long)(s / 60));
  else if (s < 48UL * 3600) snprintf(out, cap, "%lu h ago", (unsigned long)(s / 3600));
  else snprintf(out, cap, "%lu days ago", (unsigned long)(s / 86400));
}

inline void rows(const State& st, Row out[ROWS]) {
  char tmp[48];
  snprintf(tmp, sizeof(tmp), "%s %s", st.hardened ? "Hardened" : "Normal", st.role);
  clip(out[0], tmp);

  char a[24];
  if (st.rx_seen) age(st.rx_age_s, a, sizeof(a));
  else strcpy(a, "never");
  snprintf(tmp, sizeof(tmp), "Last LoRa: %s", a);
  clip(out[1], tmp);

  clip(out[2], st.mode == MODE_WIFI ? "Mode: WiFi" : st.mode == MODE_BLE ? "Mode: BLE" : "Mode: USB");

  if (st.mode == MODE_WIFI) {
    clip(out[3], st.wifi_connected && st.ssid && st.ssid[0] ? st.ssid : "WiFi not connected");
  } else if (st.mode == MODE_BLE) {
    clip(out[3], st.ble_connected ? "Connected via BLE" : st.usb_connected ? "Connected via USB" : "BLE not connected");
  } else {
    clip(out[3], st.usb_connected ? "Connected via USB" : "USB not connected");
  }

  clip(out[4], st.mqtt_connected && st.broker && st.broker[0] ? st.broker : "MQTT not connected");
}

}  // namespace FrontPage
