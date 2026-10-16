// Host-native stand-in for the Arduino IPAddress: IPv4 only, what PubSubClient and the MQTT controller use.
#pragma once
#include <Arduino.h>

class IPAddress {
  uint8_t _b[4] = {0, 0, 0, 0};
public:
  IPAddress() { }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _b[0] = a; _b[1] = b; _b[2] = c; _b[3] = d; }
  IPAddress(uint32_t host_order) { _b[0] = host_order >> 24; _b[1] = host_order >> 16; _b[2] = host_order >> 8; _b[3] = host_order; }
  uint8_t operator[](int i) const { return _b[i]; }
  uint8_t& operator[](int i) { return _b[i]; }
  uint32_t toU32() const { return ((uint32_t)_b[0] << 24) | ((uint32_t)_b[1] << 16) | ((uint32_t)_b[2] << 8) | _b[3]; }
};
