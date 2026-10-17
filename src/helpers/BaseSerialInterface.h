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
   *          command being handled arrived on (see bindReply()), untrusted when none is bound.
   */
  virtual bool isTrustedTransport() const { return false; }
  /**
   * \returns true only for a wired USB / UART link. A stricter class than isTrustedTransport(): the
   *          Hardened posture lets only a cable factory-reset the device. Fails closed.
   */
  virtual bool isCableTransport() const { return false; }
  /**
   * \returns true for a BLE link, whatever its PIN. Only says which transport a frame arrived on (the audit
   *          records a refusal against it); trust is isTrustedTransport()'s alone. Fails closed to "not BLE".
   */
  virtual bool isBleTransport() const { return false; }
  virtual void loop() {};

  /**
   * A multiplexed interface only: the interface the frame returned by the latest checkRecvFrame() arrived on,
   * nullptr for a single interface (which has nobody else to answer).
   */
  virtual const BaseSerialInterface* rxSource() const { return nullptr; }
  /**
   * A multiplexed interface only: bind the replies (writeFrame) and the transport trust queries to `src`, the
   * interface a command arrived on, until bindReply(nullptr). Unbound, a frame goes to every enabled interface
   * (a push) and the transport is untrusted. A single interface ignores it.
   */
  virtual void bindReply(const BaseSerialInterface* src) { (void)src; }

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
