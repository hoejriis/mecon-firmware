#pragma once
#include "BatteryLevel.h"

// The battery guard's CLI lines and its dry run. Nothing here sleeps, shuts down or changes the radio: this release
// stores the settings, classifies with them and answers what the guard would do. Platform-free, tested natively
// (test/test_battery_guard).
//
//   get battery.guard                  > enabled=off low=3550 critical=3400 margin=100 wake=900
//   set battery.guard <fields>         OK, or Err - <reason>; any of the fields, each once, checked as one whole
//   get battery.guard.dry [<fields>]   > accepted=yes level=ok would_sleep=no reason=guard-off
//                                        (or accepted=no refusal=<reason> ...); writes nothing
namespace BatteryGuard {

// Whether this build has the guard: the four boards of the support matrix (the Heltec V3 and V4 voltage-only, the Wio
// Tracker L1 and the T1000-E) and the simulated board define BATTERY_GUARD_BOARD. Any other board leaves the settings, the
// CLI lines and the key out (its flash is tight) and the health stream reports `supported` false.
inline bool supported() {
#ifdef BATTERY_GUARD_BOARD
  return true;
#else
  return false;
#endif
}

// What the guard would do now with these settings, and why. `external` is true when the board senses external power.
// would_sleep is only ever hypothetical: no release so far sleeps.
struct Verdict {
  bool would_sleep;
  BatteryLevel::Level level;
  const char* reason;
};

inline Verdict decide(const Cfg& c, const BatteryLevel& bl, bool external) {
  Verdict v;
  v.level = bl.levelUnder(c);
  v.would_sleep = false;
  if (!c.enabled) v.reason = "guard-off";
  else if (v.level == BatteryLevel::UNKNOWN) v.reason = "level-unknown";
  else if (external) v.reason = "external-power";
  else if (v.level == BatteryLevel::CRITICAL) { v.would_sleep = true; v.reason = "level-critical"; }
  else v.reason = v.level == BatteryLevel::LOW_LEVEL ? "level-low" : "level-ok";
  return v;
}

// The whole of a request: parse over the stored settings, validate the combination, and refuse what would put the guard at
// critical at once. Null when accepted (*out is the combination), else the
// reason. With an empty `text` the stored settings are re-checked unchanged.
inline const char* check(const char* text, const BatteryLevel& bl, bool external, Cfg* out) {
  Cfg c = bl.settings();
  if (*text) {
    const char* e = parse(text, bl.settings(), &c);
    if (e) return e;
  }
  const char* e = validate(c);
  if (e) return e;
  if (decide(c, bl, external).would_sleep) return "would-sleep-now";
  *out = c;
  return nullptr;
}

inline const char* levelName(BatteryLevel::Level l) { return BatteryLevel::name(l); }

// One CLI line. Returns false when `command` is not a battery.guard line. *changed is set when the settings changed
// (the caller saves its prefs).
inline bool handleCommand(const char* command, char* reply, size_t cap, BatteryLevel& bl, bool external,
                          bool* changed) {
  static const char GET[] = "get battery.guard", SET[] = "set battery.guard ", DRY[] = "get battery.guard.dry";
  if (strcmp(command, GET) == 0) {
    char v[TEXT_MAX];
    format(bl.settings(), v, sizeof(v));
    snprintf(reply, cap, "> %s", v);
    return true;
  }
  if (memcmp(command, SET, sizeof(SET) - 1) == 0) {
    Cfg c;
    const char* arg = command + sizeof(SET) - 1;
    const char* e = *arg ? check(arg, bl, external, &c) : "bad-format";
    if (e) {
      snprintf(reply, cap, "Err - %s", e);
    } else {
      if (!equal(c, bl.settings())) {
        bl.settings() = c;
        bl.reclassify();
        *changed = true;
      }
      snprintf(reply, cap, "OK");
    }
    return true;
  }
  if (strcmp(command, DRY) == 0 || memcmp(command, DRY, sizeof(DRY) - 1) == 0) {
    const char* arg = command + sizeof(DRY) - 1;
    if (*arg && *arg != ' ') return false;
    if (*arg == ' ') arg++;
    Cfg c;
    Cfg proposed = bl.settings();
    if (*arg) {
      const char* e = parse(arg, bl.settings(), &proposed);
      if (e) { snprintf(reply, cap, "Err - %s", e); return true; }
    }
    const char* refusal = check(arg, bl, external, &c);
    // A refused proposal is answered against what is stored; an accepted one against itself.
    const Verdict v = decide(refusal && strcmp(refusal, "would-sleep-now") != 0 ? bl.settings() : proposed, bl, external);
    if (refusal) {
      snprintf(reply, cap, "> accepted=no refusal=%s level=%s would_sleep=%s reason=%s", refusal, levelName(v.level),
               v.would_sleep ? "yes" : "no", v.reason);
    } else {
      snprintf(reply, cap, "> accepted=yes level=%s would_sleep=%s reason=%s", levelName(v.level),
               v.would_sleep ? "yes" : "no", v.reason);
    }
    return true;
  }
  return false;
}

}  // namespace BatteryGuard
