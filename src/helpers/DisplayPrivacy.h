#pragma once

// What a Companion's own screen may show in each standalone posture (docs/UI.md, "Screen privacy").
//
//                                       Normal                  Hardened
//   Message text                        shown                   hidden ("message received")
//   Sender, channel, contact and        shown                   hidden
//     advert names
//   BLE pairing PIN                     while BLE advertises    while BLE advertises, and only when the
//                                                               posture lets BLE start (a chosen PIN)
//   Posture indicator                   none                    shown
//   Own identity, role, battery,        shown                   shown
//     RF activity, counters, own GPS
//
// Every UI variant (ui-new, ui-orig, ui-tiny) asks these functions when it draws, not when a message arrives:
// the posture can change while the device runs, and a preview captured in Normal must not be drawn once the
// device is Hardened. Any posture byte other than Normal counts as Hardened (Posture::isHardened).

#include <stdint.h>
#include "Posture.h"

namespace DisplayPrivacy {

inline bool showsMessageText(uint8_t posture) { return !Posture::isHardened(posture); }
inline bool showsNames(uint8_t posture) { return !Posture::isHardened(posture); }

// The PIN is worth showing only while a central could use it: BLE is up, nobody is connected, and the posture
// let BLE start with this PIN. A PIN of 0 is "build default", which no screen shows.
inline bool showsPairingPin(uint8_t posture, uint32_t pin, bool ble_enabled, bool ble_connected) {
  return pin != 0 && ble_enabled && !ble_connected && Posture::bleMayStart(posture, pin);
}

// A short prefix for the title bar (before the node name), or nullptr when none is drawn. Short so it fits
// every panel, from a 72 px OLED to a TFT.
inline const char* postureBadge(uint8_t posture) { return Posture::isHardened(posture) ? "[H] " : nullptr; }

// Drawn in place of a hidden message's text and of a hidden name.
static const char* const HIDDEN_TEXT = "message received";
static const char* const HIDDEN_NAME = "-";

// Remembers the posture a UI last drew with, so the UI can redraw at once (and give an e-ink panel a full
// refresh, which is what clears its retained image) when the posture changes.
struct Watch {
  uint8_t last = 0;
  bool primed = false;
  // True on the first call and whenever the posture changed since the last one.
  bool changed(uint8_t posture) {
    bool c = !primed || posture != last;
    last = posture;
    primed = true;
    return c;
  }
};

}  // namespace DisplayPrivacy
