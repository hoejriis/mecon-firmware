// Host-native Arduino core shim for the simulated board. Only what the upstream
// examples and helpers actually use. Time comes from the swappable sim clock.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <cmath>
#include <algorithm>

#define PROGMEM
#define F(x) x
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1

using std::isnan;
// Arduino's min/max accept mixed argument types.
template <typename A, typename B> inline auto min(A a, B b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename A, typename B> inline auto max(A a, B b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template <typename T, typename L, typename H> inline T constrain(T x, L lo, H hi) { return x < lo ? (T)lo : (x > hi ? (T)hi : x); }

// ---- clock (see sim_clock.h) ----
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
inline void yield() {}

// ---- random ----
long random(long maxv);
long random(long minv, long maxv);
void randomSeed(unsigned long seed);

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }

class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) {
    size_t t = 0;
    for (size_t i = 0; i < n; i++) t += write(buf[i]);
    return t;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* s, size_t n) { return write((const uint8_t*)s, n); }
  virtual int availableForWrite() { return 64; }
  virtual void flush() {}

  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return printNum((unsigned long)v, base); }
  size_t print(int v, int base = DEC) { return printSigned((long)v, base); }
  size_t print(unsigned int v, int base = DEC) { return printNum((unsigned long)v, base); }
  size_t print(long v, int base = DEC) { return printSigned(v, base); }
  size_t print(unsigned long v, int base = DEC) { return printNum(v, base); }
  size_t print(long long v, int base = DEC) { return printSigned((long)v, base); }
  size_t print(unsigned long long v, int base = DEC) { return printNum((unsigned long)v, base); }
  size_t print(double v, int digits = 2) {
    char b[48]; int n = snprintf(b, sizeof b, "%.*f", digits, v); return write((const uint8_t*)b, n);
  }
  size_t println() { return write((const uint8_t*)"\r\n", 2); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T, typename U> size_t println(T v, U u) { size_t n = print(v, u); return n + println(); }

  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if (n >= (int)sizeof b) n = sizeof b - 1;
    return write((const uint8_t*)b, n);
  }
private:
  size_t printNum(unsigned long v, int base) {
    char b[40]; char* p = b + sizeof b; *--p = 0;
    if (base < 2) base = 10;
    do { int d = v % base; *--p = d < 10 ? '0' + d : 'A' + d - 10; v /= base; } while (v);
    return write(p);
  }
  size_t printSigned(long v, int base) {
    if (base == DEC && v < 0) { size_t n = write((uint8_t)'-'); return n + printNum((unsigned long)(-v), base); }
    return printNum((unsigned long)v, base);
  }
};

class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t readBytes(char* buffer, size_t length) {
    size_t i = 0;
    while (i < length && available()) buffer[i++] = (char)read();
    return i;
  }
  virtual size_t readBytes(uint8_t* buffer, size_t length) { return readBytes((char*)buffer, length); }
};

// Serial port of the simulated board; defined in sim_serial.cpp.
class SimSerial : public Stream {
public:
  void begin(unsigned long) {}
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  operator bool() const { return true; }
};
extern SimSerial Serial;

// Interrupt/critical-section no-ops used by some helpers.
#define noInterrupts()
#define interrupts()

inline char* ltoa(long v, char* buf, int base) {
  if (base == 10) { sprintf(buf, "%ld", v); return buf; }
  sprintf(buf, base == 16 ? "%lx" : "%lo", (unsigned long)v); return buf;
}
inline char* utoa(unsigned v, char* buf, int base) { return ltoa((long)v, buf, base); }
