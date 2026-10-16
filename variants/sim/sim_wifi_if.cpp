// Simulated board: the real SerialWifiInterface class (src/helpers/wifi/SerialWifiInterface.h, so its
// transport trust classification is the one the board builds have) with its network plumbing replaced:
// the frames travel over the node's loopback serial port instead of a WiFi TCP server.
#include <helpers/wifi/SerialWifiInterface.h>
#include <helpers/ArduinoSerialInterface.h>

static ArduinoSerialInterface& inner() { static ArduinoSerialInterface i; return i; }

void SerialWifiInterface::begin(int) { inner().begin(Serial); }
void SerialWifiInterface::enable() { _isEnabled = true; inner().enable(); }
void SerialWifiInterface::disable() { _isEnabled = false; inner().disable(); }
bool SerialWifiInterface::isConnected() const { return _isEnabled && inner().isConnected(); }
bool SerialWifiInterface::isWriteBusy() const { return inner().isWriteBusy(); }
size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) { return inner().writeFrame(src, len); }
size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) { return inner().checkRecvFrame(dest); }
bool SerialWifiInterface::hasReceivedFrameHeader() { return false; }
void SerialWifiInterface::resetReceivedFrameHeader() {}
