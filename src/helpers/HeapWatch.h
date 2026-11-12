#pragma once

// What the heap was doing when it reached its lowest: the largest free block's lowest value and the lowest free heap
// (min_free), each with what was running at the time and when. The heap floor (HeapFloor.h) says that the largest block
// is low; this says what put it there.
//
// A look may walk the heap, so a scope wraps work that happens (a request, a pass that sends an event), never a loop
// pass that does nothing.
//
// Work that may allocate runs inside a HeapScope naming it ("request", an operation's name, "stream.inventory",
// "mqtt.connect", ...). When the scope ends, the heap is looked at: a new low of the largest block, or of min_free, is put
// down to that name. Outside any scope a periodic look puts it down to "idle" (the Wi-Fi and TCP stacks, other tasks).
// The largest block after a scope shows what the scope left allocated (a lasting split); min_free, the platform's own
// low-water, also catches a brief peak inside the scope.
//
// Work on another task (the MQTT connect runs on a worker) cannot hold the name, which belongs to the loop: while it runs,
// setBackground() names it for the looks taken outside every scope, so a handshake's low is not put down to "idle".
//
// Names are string literals (or other static storage): only the pointer is kept.
//
// Pure logic: the caller passes the heap figures and the time, so it runs in the native unit tests.

#include <stdint.h>
#include "MemStats.h"

class HeapWatch {
public:
  struct Low {
    uint32_t bytes = 0;
    const char* by = nullptr;   // what was running; "idle" outside every scope
    uint32_t at_s = 0;          // seconds since boot
    bool seen = false;
  };

  const char* current() const { return _tag ? _tag : _bg ? _bg : "idle"; }
  // What runs on another task while nothing in the loop is named; nullptr when it ends.
  void setBackground(const char* what) { _bg = what; }
  // Enter a named piece of work; returns the name it replaces, for leave().
  const char* enter(const char* what) {
    const char* prev = _tag;
    _tag = what;
    return prev;
  }
  void leave(const char* prev) { _tag = prev; }

  // One look at the heap, put down to whatever is running now.
  void observe(const MemStats& m, uint32_t uptime_s) {
    if (!m.known) return;
    if (m.largest && (!_largest.seen || m.largest < _largest.bytes)) set(_largest, m.largest, uptime_s);
    if (m.min_free && (!_min_free.seen || m.min_free < _min_free.bytes)) set(_min_free, m.min_free, uptime_s);
  }

  const Low& largest() const { return _largest; }
  const Low& minFree() const { return _min_free; }

private:
  void set(Low& l, uint32_t bytes, uint32_t at_s) {
    l.bytes = bytes;
    l.by = current();
    l.at_s = at_s;
    l.seen = true;
  }

  const char* _tag = nullptr;
  const char* _bg = nullptr;
  Low _largest, _min_free;
};

// One per device. Function-local, as heapFloor(): the examples construct their mesh object before main().
inline HeapWatch& heapWatch() {
  static HeapWatch h;
  return h;
}

#if defined(ARDUINO) || defined(SIM_FREE_HEAP)
  #include <Arduino.h>
  // Outside every scope, a look once a second; called from the loop.
  inline void heapWatchTick() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (last && now - last < 1000) return;
    last = now ? now : 1;
    heapWatch().observe(memStats(), now / 1000);
  }
  // Work that may allocate, named: the heap is looked at when it ends.
  class HeapScope {
  public:
    explicit HeapScope(const char* what) : _prev(heapWatch().enter(what)) {}
    ~HeapScope() {
      heapWatch().observe(memStats(), millis() / 1000);
      heapWatch().leave(_prev);
    }
    HeapScope(const HeapScope&) = delete;
    HeapScope& operator=(const HeapScope&) = delete;
  private:
    const char* _prev;
  };
#else
  // The native unit tests: nothing to look at.
  class HeapScope {
  public:
    explicit HeapScope(const char*) {}
  };
  inline void heapWatchTick() {}
#endif
