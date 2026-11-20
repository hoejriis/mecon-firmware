#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The battery guard's settings, one composite value: enabled, low, critical, margin and wake. Platform-free, tested
// natively (test/test_battery_guard). This release only reports and answers a dry run: nothing sleeps or shuts down.
//
// The value is always checked as a whole. A request names some or all of the fields; the ones it leaves out come from
// the stored value, and the combination is what is validated, never one field against the old stored others. A refused
// request changes nothing.
namespace BatteryGuard {

struct Cfg {
  uint16_t low_mv = 3550;       // a level below this mean is low
  uint16_t critical_mv = 3400;  // a level below this mean is critical
  uint16_t margin_mv = 100;     // a level is left upward only this far above the threshold it crossed (the hysteresis)
  uint16_t wake_s = 900;        // the check interval a later release will use while asleep; kept now, not used
  uint8_t enabled = 0;          // off by default
};

inline bool equal(const Cfg& a, const Cfg& b) {
  return a.low_mv == b.low_mv && a.critical_mv == b.critical_mv && a.margin_mv == b.margin_mv && a.wake_s == b.wake_s &&
         a.enabled == b.enabled;
}

// The valid ranges, in millivolts and seconds.
struct Range {
  static const uint16_t CRITICAL_MIN = 2500, CRITICAL_MAX = 4000;
  static const uint16_t LOW_MIN = 2600, LOW_MAX = 4100;
  static const uint16_t MIN_GAP = 100;                 // low is at least this far above critical
  static const uint16_t MARGIN_MIN = 50, MARGIN_MAX = 500;   // low + margin stays within the 4600 mV valid reading
  static const uint16_t WAKE_MIN = 300, WAKE_MAX = 3600;
};

// The longest text a value takes or gives: "enabled=off low=3550 critical=3400 margin=100 wake=900" is 54 characters.
static const size_t TEXT_MAX = 64;

// The checks of one whole combination. Null when it is valid, else the name of the first rule it breaks.
inline const char* validate(const Cfg& c) {
  if (c.critical_mv < Range::CRITICAL_MIN || c.critical_mv > Range::CRITICAL_MAX) return "critical-out-of-range";
  if (c.low_mv < Range::LOW_MIN || c.low_mv > Range::LOW_MAX) return "low-out-of-range";
  if (c.low_mv < c.critical_mv + Range::MIN_GAP) return "low-not-above-critical";
  if (c.margin_mv < Range::MARGIN_MIN || c.margin_mv > Range::MARGIN_MAX) return "margin-out-of-range";
  if (c.wake_s < Range::WAKE_MIN || c.wake_s > Range::WAKE_MAX) return "wake-out-of-range";
  return nullptr;
}

namespace detail {
inline bool number(const char* s, size_t n, uint32_t* out) {
  if (n == 0 || n > 5) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (uint32_t)(s[i] - '0');
  }
  *out = v;
  return true;
}
}  // namespace detail

// "enabled=on low=3550 critical=3400 margin=100 wake=900", in any order, any subset, each name once, single spaces.
// Applies the named fields on top of `base` into *out, with no range check (validate() does that, on the whole). Null
// on success, else "bad-format".
inline const char* parse(const char* text, const Cfg& base, Cfg* out) {
  Cfg c = base;
  bool seen[5] = {false, false, false, false, false};
  const char* p = text;
  if (!*p) return "bad-format";
  while (*p) {
    const char* eq = strchr(p, '=');
    if (!eq) return "bad-format";
    const char* end = strchr(eq, ' ');
    const size_t nlen = (size_t)(eq - p);
    const size_t vlen = end ? (size_t)(end - eq - 1) : strlen(eq + 1);
    const char* v = eq + 1;
    int f = -1;
    if (nlen == 7 && memcmp(p, "enabled", 7) == 0) f = 0;
    else if (nlen == 3 && memcmp(p, "low", 3) == 0) f = 1;
    else if (nlen == 8 && memcmp(p, "critical", 8) == 0) f = 2;
    else if (nlen == 6 && memcmp(p, "margin", 6) == 0) f = 3;
    else if (nlen == 4 && memcmp(p, "wake", 4) == 0) f = 4;
    if (f < 0 || seen[f]) return "bad-format";
    seen[f] = true;
    if (f == 0) {
      if (vlen == 2 && memcmp(v, "on", 2) == 0) c.enabled = 1;
      else if (vlen == 3 && memcmp(v, "off", 3) == 0) c.enabled = 0;
      else return "bad-format";
    } else {
      uint32_t n;
      if (!detail::number(v, vlen, &n) || n > 65535) return "bad-format";
      if (f == 1) c.low_mv = (uint16_t)n;
      else if (f == 2) c.critical_mv = (uint16_t)n;
      else if (f == 3) c.margin_mv = (uint16_t)n;
      else c.wake_s = (uint16_t)n;
    }
    if (!end) break;
    p = end + 1;
    if (!*p) return "bad-format";   // a trailing space
  }
  *out = c;
  return nullptr;
}

// The value as the CLI and config.get give it.
inline void format(const Cfg& c, char* out, size_t cap) {
  snprintf(out, cap, "enabled=%s low=%u critical=%u margin=%u wake=%u", c.enabled ? "on" : "off", (unsigned)c.low_mv,
           (unsigned)c.critical_mv, (unsigned)c.margin_mv, (unsigned)c.wake_s);
}

// The syntax of a value, for the settings table (any subset of the fields, each once, numbers only). The ranges are
// checked on the device against the whole combination.
inline bool formatOk(const char* text) {
  Cfg c;
  return parse(text, Cfg(), &c) == nullptr;
}

}  // namespace BatteryGuard
