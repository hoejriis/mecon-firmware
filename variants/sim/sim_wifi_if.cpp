// Simulated board: the real SerialWifiInterface class (src/helpers/wifi/SerialWifiInterface.h, so its
// transport trust classification is the one the board builds have) with its network plumbing replaced:
// the frames travel over the node's loopback serial port instead of a WiFi TCP server.
//
// The frames only flow while the Wi-Fi controller has joined a (simulated) network: no link, no TCP server.
// In the build that also has the USB interface (sim_companion_wifi_usb) the loopback port belongs to USB, so the
// TCP side is inert there: that build exists to drive the Wi-Fi CLI and the controller over USB.
#include <helpers/wifi/SerialWifiInterface.h>
#include <helpers/ArduinoSerialInterface.h>
#include <WiFi.h>

static ArduinoSerialInterface& inner() { static ArduinoSerialInterface i; return i; }

#ifdef ENABLE_USB_INTERFACE
void SerialWifiInterface::begin(int) { }
void SerialWifiInterface::enable() { _isEnabled = true; }
void SerialWifiInterface::disable() { _isEnabled = false; }
bool SerialWifiInterface::isConnected() const { return false; }
bool SerialWifiInterface::isWriteBusy() const { return false; }
size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) { return 0; }
size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) { return 0; }
#else
void SerialWifiInterface::begin(int) { inner().begin(Serial); }
void SerialWifiInterface::enable() { _isEnabled = true; inner().enable(); }
void SerialWifiInterface::disable() { _isEnabled = false; inner().disable(); }
bool SerialWifiInterface::isConnected() const { return _isEnabled && WiFi.status() == WL_CONNECTED && inner().isConnected(); }
bool SerialWifiInterface::isWriteBusy() const { return inner().isWriteBusy(); }
size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) { return inner().writeFrame(src, len); }
size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) { return WiFi.status() == WL_CONNECTED ? inner().checkRecvFrame(dest) : 0; }
#endif
bool SerialWifiInterface::hasReceivedFrameHeader() { return false; }
void SerialWifiInterface::resetReceivedFrameHeader() {}
