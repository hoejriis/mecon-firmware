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
   * \returns false if this transport carries no authentication of the peer (a TCP server that
   *          accepts any client). Management and secret-bearing commands are refused on such a
   *          transport. For a multiplexed interface: the transport the last frame arrived on.
   */
  virtual bool isTrustedTransport() const { return true; }
  virtual void loop() {};

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
