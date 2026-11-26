#pragma once

// The enforced heap floor: a run-time check that the largest free heap block stays above a per-board floor.
//
// CI cannot measure heap, and the hardware sessions only sample it. A Wi-Fi + TLS session, a BLE pairing or a long
// uptime can leave the largest block small enough that the next allocation (a TLS handshake, a contact bucket) fails.
// This check looks every HEAP_FLOOR_PERIOD_MS (and once after setup) and, when the largest block is below the floor:
//   * records exactly one audit entry per low episode (EV_LOW_MEM; repeats of the event fold into one entry whose
//     count is the number of episodes),
//   * raises `heap_low` in `get mecon.mem`, and
//   * lets the Companion's home status line say LOW MEM.
// An episode ends when the largest block is back at floor + hysteresis, so a block hovering at the floor cannot
// produce one entry per check.
//
// The largest block is the figure, not the total free heap: the total can look healthy while fragmentation leaves no
// block big enough. Where MemStats is not measurable (`known` false) the check never fires.
//
// A TLS session's own input record buffer (the content length plus the record overhead) is allocated from the biggest
// region and stays there for as long as the session is open, so the largest block of a healthy connected board is
// smaller by exactly that much. The MQTT network layer reports it with holdSession() while a session (or its handshake)
// exists and releases it when the session is closed; the check then counts the largest block plus that figure, so a
// healthy connected board is not low, while a block that is short for any other reason still is. The floor itself is
// not lowered, and the figure `get mecon.mem` prints as `largest` stays the measured block.
//
// Pure logic: no clock, no heap API of its own (the caller passes both), so it runs in the native unit tests.

#include <stdint.h>
#include <atomic>
#include "MemStats.h"
#include "AuditLog.h"

#ifndef HEAP_FLOOR_BYTES
  #if defined(ESP32)
    #define HEAP_FLOOR_BYTES 20480   // proposed from the first hardware session: largest block >= 20 KB with a session up
  #else
    #define HEAP_FLOOR_BYTES 0       // not checked: the platform's figure is the total free heap, not a block (docs/CLI.md)
  #endif
#endif
#ifndef HEAP_FLOOR_HYSTERESIS_BYTES
  #define HEAP_FLOOR_HYSTERESIS_BYTES 2048
#endif
#ifndef HEAP_FLOOR_PERIOD_MS
  #define HEAP_FLOOR_PERIOD_MS 60000UL
#endif

#ifndef HEAP_FLOOR_TLS_SESSION_BYTES
  // The ESP32 builds' TLS input buffer: 16,384 B of content (MBEDTLS_SSL_IN_CONTENT_LEN) + 512 B of record overhead.
  // The network layer passes the configured content length when the library defines it (MqttNetEsp32.cpp).
  #define HEAP_FLOOR_TLS_SESSION_BYTES (16384 + 512)
#endif

class HeapFloor {
public:
  // floor_bytes 0 disables the check. hysteresis: how far above the floor the block must be to end an episode.
  void begin(uint32_t floor_bytes, uint32_t hysteresis = HEAP_FLOOR_HYSTERESIS_BYTES, uint32_t period_ms = HEAP_FLOOR_PERIOD_MS) {
    _floor = floor_bytes;
    _hyst = hysteresis;
    _period = period_ms;
    _low = false;
    _primed = false;
    _last = 0;
    _held = 0;
  }

  // A TLS session's input buffer is held (or released again): the network layer calls these as a session or its handshake
  // starts and when it is closed, each with the same figure. Sums over sessions (the MQTT and the update network each
  // have their own); atomic because the handshake runs on the connect worker and the close on the loop task.
  void holdSession(uint32_t bytes) { _held.fetch_add(bytes); }
  void releaseSession(uint32_t bytes) {
    uint32_t cur = _held.load();
    while (!_held.compare_exchange_weak(cur, cur > bytes ? cur - bytes : 0)) {}
  }
  uint32_t sessionHeld() const { return _held.load(); }

  bool enabled() const { return _floor > 0; }
  bool low() const { return _low; }
  uint32_t floorBytes() const { return _floor; }

  // Time for the next look? The first look comes as soon as the check is enabled (after setup).
  bool due(uint32_t now_ms) const { return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }

  // One look at `m`. Returns true when the low state changed. `ts` is the RTC time for the audit entry.
  bool check(uint32_t now_ms, uint32_t ts, const MemStats& m, Posture::AuditRing& audit) {
    _primed = true;
    _last = now_ms;
    if (!enabled() || !m.known) return false;
    const uint32_t largest = m.largest + _held.load();   // what the largest block would be without the open session's buffer
    if (!_low) {
      if (largest < _floor) {
        _low = true;
        audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR);
        return true;
      }
    } else if (largest >= _floor + _hyst) {   // (floor + hyst cannot wrap for any sane floor)
      _low = false;
      return true;
    }
    return false;
  }

  // The caller's one line: measures only when a look is due.
  void loop(uint32_t now_ms, uint32_t ts) {
    if (due(now_ms)) check(now_ms, ts, memStats(), Posture::audit());
  }

private:
  uint32_t _floor = 0, _hyst = HEAP_FLOOR_HYSTERESIS_BYTES, _period = HEAP_FLOOR_PERIOD_MS, _last = 0;
  std::atomic<uint32_t> _held{0};
  bool _low = false, _primed = false;
};

// One per device. Function-local: the examples construct their mesh object before main().
inline HeapFloor& heapFloor() {
  static HeapFloor h;
  return h;
}
