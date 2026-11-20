#pragma once
#include <stdint.h>

// The battery level the health stream reports: ok, low, critical, or unknown. It only reports. Nothing sleeps, shuts
// down or changes the radio because of it.
//
// A level comes from the mean of the valid readings of one 60-second window, read every 5 seconds (12 readings), so
// a dip shorter than the window, such as the sag while the radio transmits, cannot move it. A reading outside
// VALID_MIN_MV..VALID_MAX_MV (a board that returns 0 for "cannot measure" included) is not a reading: it is left out of
// the mean, and a window with fewer than MIN_READINGS valid readings makes the level unknown. Unknown is never
// critical. The level goes down at once when the mean falls below a threshold and comes back up only HYST_MV above the
// threshold it crossed.
//
// The state is a sum and a count, not the readings, so it costs a few bytes of RAM. The thresholds are fixed here and
// nowhere else. Platform-free, tested natively (test/test_battery_level). Call only from the loop task.
class BatteryLevel {
public:
  enum Level : uint8_t { OK = 0, LOW_LEVEL = 1, CRITICAL = 2, UNKNOWN = 3 };

  // The one place the numbers live, in millivolts, for a single Li-ion or LiPo cell.
  struct Thresholds {
    static const uint16_t LOW_MV = 3550;        // below this: low
    static const uint16_t CRITICAL_MV = 3400;   // below this: critical
    static const uint16_t HYST_MV = 100;        // a level is left upward only this far above the threshold it crossed
    static const uint16_t VALID_MIN_MV = 2000;  // a reading outside this range is a sensor fault, not a level
    static const uint16_t VALID_MAX_MV = 4600;
  };
  static const uint32_t SAMPLE_MS = 5000;
  static const uint8_t WINDOW_READINGS = 12;    // 12 x 5 s = 60 s
  static const uint8_t MIN_READINGS = 8;        // fewer valid readings in a window: unknown

  // True when a reading is due. The caller reads the board only then, so a stalled loop costs no ADC reads.
  bool due(uint32_t now_ms) const { return (int32_t)(now_ms - _due) >= 0; }

  // Add one reading, in mV (0 or out of range: invalid). Every WINDOW_READINGS calls close a window and re-classify.
  void sample(uint32_t now_ms, uint16_t mv) {
    _due = now_ms + SAMPLE_MS;
    if (mv >= Thresholds::VALID_MIN_MV && mv <= Thresholds::VALID_MAX_MV) { _sum += mv; _n++; }
    if (++_slot < WINDOW_READINGS) return;
    closeWindow();
  }

  Level level() const { return _valid ? _level : UNKNOWN; }
  // Times the reported level changed since boot, the first reading after boot included. Wraps at 65536.
  uint16_t changes() const { return _changes; }
  static const char* name(Level l) {
    switch (l) {
      case OK: return "ok";
      case LOW_LEVEL: return "low";
      case CRITICAL: return "critical";
      default: return "unknown";
    }
  }

  // The window a level comes from; the one name the telemetry carries for how it was derived.
  static const char* method() { return "mv_mean_60s"; }

private:
  void closeWindow() {
    const Level before = level();
    if (_n < MIN_READINGS) {
      _valid = false;   // keeps the last level, so a short fault does not drop the hysteresis
    } else {
      const uint16_t mean = (uint16_t)((_sum + _n / 2) / _n);
      _level = classify(mean);
      _valid = true;
      _seen = true;
    }
    if (level() != before) _changes++;
    _sum = 0; _n = 0; _slot = 0;
  }

  Level classify(uint16_t mean) const {
    if (!_seen) {   // no level yet: the plain thresholds
      return mean < Thresholds::CRITICAL_MV ? CRITICAL : mean < Thresholds::LOW_MV ? LOW_LEVEL : OK;
    }
    const uint16_t up_to_low = Thresholds::CRITICAL_MV + Thresholds::HYST_MV;
    const uint16_t up_to_ok = Thresholds::LOW_MV + Thresholds::HYST_MV;
    switch (_level) {
      case OK:
        return mean < Thresholds::CRITICAL_MV ? CRITICAL : mean < Thresholds::LOW_MV ? LOW_LEVEL : OK;
      case LOW_LEVEL:
        return mean < Thresholds::CRITICAL_MV ? CRITICAL : mean >= up_to_ok ? OK : LOW_LEVEL;
      default:   // CRITICAL
        return mean >= up_to_ok ? OK : mean >= up_to_low ? LOW_LEVEL : CRITICAL;
    }
  }

  uint32_t _due = 0;
  uint16_t _sum = 0;       // at most 12 x 4600, which fits
  uint16_t _changes = 0;
  uint8_t _n = 0, _slot = 0;
  Level _level = OK;       // the last level a window gave
  bool _valid = false;     // whether the latest window gave one
  bool _seen = false;      // whether any window has
};

// The one tracker of this firmware, fed from the role's loop.
inline BatteryLevel& batteryLevel() {
  static BatteryLevel b;
  return b;
}
