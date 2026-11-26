#pragma once
// The reconnect recovery rule (Issue #651): a device whose broker connects keep failing while its heap is low restarts,
// once, instead of retrying under a heap that cannot hold a TLS handshake.
//
// A TLS handshake needs one large block (about 16 KB or more). Every failed connect builds and frees a TLS context, and
// a long run of them can leave the largest free block under the heap floor (helpers/HeapFloor.h) with plenty of memory
// free in total. From then on each attempt fails for want of a block, and nothing the device does frees one. A restart
// does. The rule is: FAILS_NEEDED connect attempts failed in a row, no success between them, and `heap_low` set when the
// last one failed.
//
// The cap: at most one recovery restart per hour. The hour is measured on the millisecond counter, which starts at zero
// at every boot, so a restart resets it and the cap survives the restart with no stored state: the next recovery can come
// no sooner than an hour of uptime. The same rule means a device that boots with a low heap waits an hour before its
// first. A broker that is simply down, with a healthy heap, never triggers it, and a broker that stays down with a low
// heap costs one restart an hour, not a loop.
//
// Pure logic: no clock of its own, no heap API, no reboot (the caller passes the time and the heap state and acts on the
// answer), so it runs in the native unit tests.
#include <stdint.h>

class MqttRecovery {
public:
  static const uint8_t FAILS_NEEDED = 5;                   // consecutive failed connect attempts
  static const uint32_t MIN_INTERVAL_MS = 3600000UL;       // between restarts, and after boot

  void begin() { _fails = 0; _since = 0; _armed = false; }

  // Call with the millisecond counter often (the main loop): the cap is only met once, and a counter that has wrapped
  // (49.7 days) must not close it again.
  void tick(uint32_t now_ms) {
    if (!_armed && (uint32_t)(now_ms - _since) >= MIN_INTERVAL_MS) _armed = true;
  }

  void onConnected() { _fails = 0; }

  // A connect attempt failed. True when the device should restart now; the caller restarts it. The count starts over
  // and the hour begins again, so a second call before the restart happens answers false.
  bool onFailed(uint32_t now_ms, bool heap_low) {
    tick(now_ms);
    if (_fails < 255) _fails++;
    if (_fails >= FAILS_NEEDED && heap_low && _armed) {
      _fails = 0;
      _since = now_ms;
      _armed = false;
      return true;
    }
    return false;
  }

  uint8_t fails() const { return _fails; }

private:
  uint8_t _fails = 0;
  uint32_t _since = 0;     // the last restart this rule asked for, or boot (0)
  bool _armed = false;     // an hour has passed since `_since`
};
