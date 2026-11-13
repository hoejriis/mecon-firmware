#pragma once

// Which part of a loop pass took the time, for the longest pass since boot. A loop pass that holds the port too long
// loses part of a request arriving on a UART (a Heltec V3's 1 KB receive ring at 115,200 baud lasts about 90 ms), and
// keeps the radio waiting; this names what did it.
//
// The loop marks the start and end of each pass, and names its sections (the mesh, the screen, Wi-Fi, MQTT, ...) with a
// TimeScope. When a pass is the longest yet, its duration is kept with its longest section: the name and how long that
// section took. Time outside every section is put down to "unscoped". Each section also keeps its own longest run, so a
// section that is slow inside a pass another section made longer still shows. Passes in the first SETTLE_S seconds after
// boot are left out (the boot's own passes are long by design, and get mecon.boot already reports them).
//
// Names are string literals (or other static storage): only the pointer is kept.
//
// Pure logic: the caller passes the time, so it runs in the native unit tests.

#include <stdint.h>
#include <string.h>

class PassBlame {
public:
  struct Slowest {
    uint32_t pass_ms = 0;           // the whole pass
    const char* by = nullptr;       // its longest section; "unscoped" when no section ran
    uint32_t section_ms = 0;        // how long that section took
    uint32_t at_s = 0;              // seconds since boot
    bool seen = false;
  };

  static const uint32_t SETTLE_S = 60;
  static const int SECTIONS = 16;
  struct Section {
    const char* name = nullptr;
    uint32_t max_ms = 0;
  };

  void begin(uint32_t now_ms) { _start = now_ms; _longest = 0; _by = nullptr; _n = 0; }
  // A section of the current pass ended, after ms.
  void section(const char* what, uint32_t ms) {
    if (!_by || ms > _longest) { _longest = ms; _by = what; }
    if (_n < SECTIONS) { _pass[_n].name = what; _pass[_n].max_ms = ms; _n++; }
  }
  void end(uint32_t now_ms, uint32_t uptime_s) {
    if (uptime_s < SETTLE_S) return;
    for (int i = 0; i < _n; i++) fold(_pass[i].name, _pass[i].max_ms);
    const uint32_t pass = now_ms - _start;
    if (_slowest.seen && pass <= _slowest.pass_ms) return;
    _slowest.pass_ms = pass;
    _slowest.by = _by ? _by : "unscoped";
    _slowest.section_ms = _by ? _longest : pass;
    _slowest.at_s = uptime_s;
    _slowest.seen = true;
  }
  const Slowest& slowest() const { return _slowest; }
  // Each section's longest run since the first counted pass, in the order first seen; count() of them.
  int count() const { return _count; }
  const Section& sectionAt(int i) const { return _max[i]; }

private:
  void fold(const char* name, uint32_t ms) {
    for (int i = 0; i < _count; i++)
      if (_max[i].name == name || (_max[i].name && name && strcmp(_max[i].name, name) == 0)) {
        if (ms > _max[i].max_ms) _max[i].max_ms = ms;
        return;
      }
    if (_count < SECTIONS) { _max[_count].name = name; _max[_count].max_ms = ms; _count++; }
  }
  uint32_t _start = 0, _longest = 0;
  const char* _by = nullptr;
  Slowest _slowest;
  Section _pass[SECTIONS], _max[SECTIONS];
  int _n = 0, _count = 0;
};

// One per device. Function-local, as heapWatch().
inline PassBlame& passBlame() {
  static PassBlame p;
  return p;
}

#if defined(ARDUINO) || defined(SIM_FREE_HEAP)
  #include <Arduino.h>
  // A named section of the loop pass: its time is given to passBlame() when it ends. Cheap (two millis() reads), so it
  // may wrap work that runs on every pass.
  class TimeScope {
  public:
    explicit TimeScope(const char* what) : _what(what), _t0(millis()) {}
    ~TimeScope() { passBlame().section(_what, millis() - _t0); }
    TimeScope(const TimeScope&) = delete;
    TimeScope& operator=(const TimeScope&) = delete;
  private:
    const char* _what;
    uint32_t _t0;
  };
#else
  class TimeScope {
  public:
    explicit TimeScope(const char*) {}
  };
#endif
