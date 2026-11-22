#pragma once
#include <stdint.h>
#include "BatteryGuardCfg.h"

// The battery level the health stream reports: ok, low, critical, or unknown. On its own it only reports; the opt-in
// protective sleep (BatterySleep.h, on the boards that have it) acts on a confirmed critical level.
//
// A level comes from the mean of the valid readings of one 60-second window, read every 5 seconds (12 readings), so
// a dip shorter than the window, such as the sag while the radio transmits, cannot move it. A reading outside
// VALID_MIN_MV..VALID_MAX_MV (a board that returns 0 for "cannot measure" included) is not a reading: it is left out of
// the mean, and a window with fewer than MIN_READINGS valid readings makes the level unknown. Unknown is never
// critical. The level goes down at once when the mean falls below a threshold and comes back up only the margin above
// the threshold it crossed.
//
// The state is a sum and a count, not the readings, so it costs a few bytes of RAM. The thresholds are the battery
// guard's settings (BatteryGuardCfg.h: low, critical and the margin), 3550, 3400 and 100 mV until they are changed.
// Platform-free, tested natively (test/test_battery_level, test_battery_guard). Call only from the loop task.
class BatteryLevel {
public:
  enum Level : uint8_t { OK = 0, LOW_LEVEL = 1, CRITICAL = 2, UNKNOWN = 3 };

  // The range of a valid reading, in millivolts: a reading outside it is a sensor fault, not a level.
  struct Thresholds {
    static const uint16_t VALID_MIN_MV = 2000;
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
  // Windows closed since boot (wraps at 65536), whether the latest one gave a level, and its mean: what the protective
  // sleep (BatterySleep.h) counts its confirmations and its emergency margin from.
  uint16_t windows() const { return _windows; }
  bool valid() const { return _valid; }
  uint16_t lastMean() const { return _mean; }

  // The guard's settings, the one copy: the prefs files read and write these fields, and the classifier uses them.
  BatteryGuard::Cfg& settings() { return _cfg; }
  const BatteryGuard::Cfg& settings() const { return _cfg; }
  // Classify the last valid window again, after the settings changed, so the level does not wait for the next window.
  void reclassify() {
    if (!_seen) return;
    const Level before = level();
    _level = classify(_mean, _cfg, true, _level);
    if (level() != before) ++_changes;
  }
  // The level the next window would give with other settings, from the last valid window and the level held now:
  // UNKNOWN when there is no valid level. For a dry run and for refusing a setting that would put the guard at critical.
  Level levelUnder(const BatteryGuard::Cfg& c) const { return _valid ? classify(_mean, c, true, _level) : UNKNOWN; }
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
      _level = classify(mean, _cfg, _seen, _level);
      _mean = mean;
      _valid = true;
      _seen = true;
    }
    if (level() != before) _changes++;
    _windows++;
    _sum = 0; _n = 0; _slot = 0;
  }

  static Level classify(uint16_t mean, const BatteryGuard::Cfg& c, bool seen, Level was) {
    if (!seen) {   // no level yet: the plain thresholds
      return mean < c.critical_mv ? CRITICAL : mean < c.low_mv ? LOW_LEVEL : OK;
    }
    const uint16_t up_to_low = c.critical_mv + c.margin_mv;
    const uint16_t up_to_ok = c.low_mv + c.margin_mv;
    switch (was) {
      case OK:
        return mean < c.critical_mv ? CRITICAL : mean < c.low_mv ? LOW_LEVEL : OK;
      case LOW_LEVEL:
        return mean < c.critical_mv ? CRITICAL : mean >= up_to_ok ? OK : LOW_LEVEL;
      default:   // CRITICAL
        return mean >= up_to_ok ? OK : mean >= up_to_low ? LOW_LEVEL : CRITICAL;
    }
  }

  BatteryGuard::Cfg _cfg;  // the settings, which the classifier reads
  uint16_t _mean = 0;      // the mean of the last valid window, for a dry run and a re-classification
  uint32_t _due = 0;
  uint16_t _sum = 0;       // at most 12 x 4600, which fits
  uint16_t _changes = 0;
  uint16_t _windows = 0;
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
