#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;

  /**
   * \returns true only if this transport authenticates the peer: a cable (physical possession) or
   *          BLE paired with a PIN that is not a well-known default. Management and secret-bearing
   *          commands are refused on any other transport. Fails closed: an interface that does not
   *          declare itself trusted is untrusted. For a multiplexed interface: the transport the
   *          last frame arrived on.
   */
  virtual bool isTrustedTransport() const { return false; }
  virtual void loop() {};

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
