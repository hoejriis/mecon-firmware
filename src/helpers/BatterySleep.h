#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BatteryLevel.h"
#include "BusyGate.h"

// The battery guard's protective sleep: opt-in (battery.guard enabled=on), on the boards that have a timed deep sleep
// (the Heltec V3 and V4, voltage only, and the simulated board). Platform-free, tested natively (test/test_battery_sleep);
// the board's part is BatterySleepRun.h and its platform file.
//
// Entry, from the loop only (Controller::tick):
//   - the guard is on, the level is critical (BatteryLevel.h) and has been for CONFIRM_WINDOWS consecutive 60 s windows,
//     no external power is sensed, and nothing holds the device (BusyGate.h: a save, an open job, an update transfer, an
//     image on trial): the device sleeps for the interval;
//   - emergency: a window whose mean is more than EMERGENCY_BELOW_MV under critical sleeps at once, held only by a save
//     in progress;
//   - never at an unknown level, with the guard off, on a board without the sleep, or when the timer cannot be armed
//     (the device stays up, `timer-failed`, and tries again at the next window).
// Recovery, at the top of setup() on each timer wake (boot()), before the display, the radio, the file system or any
// network starts: a burst of readings; below the recovery voltage the device sleeps again at once; at or above it, two
// more bursts BURST_GAP_MS apart must agree before the boot goes on. One rebound reading never resumes it. After a
// resume the device is on probation for PROBATION_MS: a relapse into sleep in that time is a failed recovery, which
// doubles the interval (up to the maximum) and raises the recovery voltage by RELAPSE_BUMP_MV; a whole probation resets
// both. After FAIL_CAP failed recoveries in a row the interval stays at its maximum.
//
// The state is a small block in RTC memory (State): it survives deep sleep and a software reset, not a power-on. It
// carries a copy of the settings, because a wake decides before the file system is mounted. A magic and a CRC guard it;
// a damaged block is cleared and the device boots and measures afresh, never "assumed healthy" from stale state. Nothing
// here writes flash, so a wake that goes back to sleep writes nothing. No duration uses the wall clock: the sleep is
// the timer's, probation and the lease count the boot's own milliseconds.
//
// The recovery voltage is derived, not a setting: the larger of low + margin (where the level returns to ok) and
// critical + RECOVER_ABOVE_CRITICAL_MV, plus the relapse bump. Every figure here is a placeholder to be checked on the
// hardware; no current or sleep-time figure is measured.
namespace BatterySleep {

static const uint32_t MAGIC = 0x50534247;            // "GBSP"
static const uint8_t VERSION = 1;

static const uint8_t CONFIRM_WINDOWS = 3;             // 3 consecutive critical windows (3 minutes) before a sleep
static const uint16_t EMERGENCY_BELOW_MV = 150;       // a window this far under critical sleeps without the confirmation
static const uint16_t RECOVER_ABOVE_CRITICAL_MV = 250;
static const uint8_t BURSTS = 3;                      // bursts at or above the recovery voltage before a resume
static const uint32_t BURST_GAP_MS = 5000;
static const uint8_t BURST_READINGS = 16;             // one burst: 16 readings 10 ms apart, the 4 lowest and highest
static const uint8_t BURST_MIN_VALID = 12;            // dropped; fewer than 12 valid readings is no measurement
static const uint32_t BURST_READING_GAP_MS = 10;
static const uint32_t PROBATION_MS = 600000;          // 10 minutes
static const uint32_t LEASE_MS = 600000;              // a button wake: 10 minutes awake whatever the level
static const uint16_t RELAPSE_BUMP_MV = 50;
static const uint16_t BUMP_MAX_MV = 200;
static const uint16_t BUMPED_CAP_MV = 4000;           // a bump never takes the recovery voltage above this
static const uint8_t FAIL_CAP = 20;
static const uint32_t SETTLE_MS = 2000;               // the last check before a sleep, so queued answers go out
static const uint16_t INTERVAL_MAX_S = BatteryGuard::Range::WAKE_MAX;

enum Phase : uint8_t { PH_NONE = 0, PH_ASLEEP = 1, PH_PROBATION = 2 };
// Why the device last went to sleep.
enum Entry : uint8_t { E_NONE = 0, E_CRITICAL, E_EMERGENCY, E_BROWNOUT };
// Why it last came back, or did not go.
enum Recovery : uint8_t { V_NONE = 0, V_VOLTAGE, V_BUTTON, V_SENSOR_INVALID, V_TIMER_FAILED, V_RESET, V_STATE_LOST };
// What started this boot.
enum Wake : uint8_t { W_OTHER = 0, W_TIMER, W_BUTTON, W_BROWNOUT };

inline const char* entryName(uint8_t e) {
  switch (e) {
    case E_CRITICAL: return "critical";
    case E_EMERGENCY: return "emergency";
    case E_BROWNOUT: return "brownout";
    default: return nullptr;
  }
}
inline const char* recoveryName(uint8_t v) {
  switch (v) {
    case V_VOLTAGE: return "voltage";
    case V_BUTTON: return "button";
    case V_SENSOR_INVALID: return "sensor-invalid";
    case V_TIMER_FAILED: return "timer-failed";
    case V_RESET: return "reset";
    case V_STATE_LOST: return "state-lost";
    default: return nullptr;
  }
}

// The block in RTC memory. Only kept while the guard is on: turning it off clears it.
struct State {
  uint32_t magic;
  uint8_t version, enabled, phase, entry;
  uint8_t recovery, failed, pad0, pad1;
  uint16_t low_mv, critical_mv, margin_mv, wake_s;   // the settings, for a wake that decides before the prefs load
  uint16_t interval_s, bump_mv, sleeps, adc_x1000;   // the next sleep, the relapse bump, sleeps begun, the ADC factor
  uint32_t crc;
};

inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}
inline uint32_t crcOf(const State& s) { return crc32(reinterpret_cast<const uint8_t*>(&s), offsetof(State, crc)); }
inline bool valid(const State& s) { return s.magic == MAGIC && s.version == VERSION && s.crc == crcOf(s); }
inline void seal(State& s) { s.magic = MAGIC; s.version = VERSION; s.crc = crcOf(s); }
inline void clear(State& s) { memset(&s, 0, sizeof(s)); }

inline uint16_t recoverMv(uint16_t low, uint16_t critical, uint16_t margin, uint16_t bump) {
  const uint32_t a = (uint32_t)low + margin, b = (uint32_t)critical + RECOVER_ABOVE_CRITICAL_MV;
  const uint32_t base = a > b ? a : b;
  const uint32_t cap = base > BUMPED_CAP_MV ? base : BUMPED_CAP_MV;
  const uint32_t r = base + (bump > BUMP_MAX_MV ? BUMP_MAX_MV : bump);
  return (uint16_t)(r > cap ? cap : r);
}
inline uint16_t recoverMv(const State& s) { return recoverMv(s.low_mv, s.critical_mv, s.margin_mv, s.bump_mv); }

// What a boot or a sleep needs of the board, so the native test can drive it.
struct Io {
  uint16_t (*read)(void* user);                  // one battery reading, mV (0: none)
  void (*wait)(void* user, uint32_t ms);
  bool (*sleep)(void* user, uint32_t secs);      // arms the timer and sleeps; false only when arming failed
  void* user;
};

// One burst: the trimmed mean of the valid readings, or 0 when too few were valid.
inline uint16_t burst(const Io& io) {
  uint16_t v[BURST_READINGS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < BURST_READINGS; i++) {
    const uint16_t mv = io.read(io.user);
    if (mv >= BatteryLevel::Thresholds::VALID_MIN_MV && mv <= BatteryLevel::Thresholds::VALID_MAX_MV) v[n++] = mv;
    if (i + 1 < BURST_READINGS) io.wait(io.user, BURST_READING_GAP_MS);
  }
  if (n < BURST_MIN_VALID) return 0;
  for (uint8_t i = 1; i < n; i++) {   // insertion sort, 16 at most
    const uint16_t x = v[i];
    uint8_t j = i;
    while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
    v[j] = x;
  }
  const uint8_t trim = n / 4;
  uint32_t sum = 0;
  for (uint8_t i = trim; i < n - trim; i++) sum += v[i];
  const uint8_t k = n - 2 * trim;
  return (uint16_t)((sum + k / 2) / k);
}

// Mark the block asleep for a new entry. A sleep that starts within probation is a failed recovery.
inline void markAsleep(State& s, uint8_t why) {
  if (s.phase == PH_PROBATION) {
    if (s.failed < FAIL_CAP) s.failed++;
    const uint32_t iv = (uint32_t)(s.interval_s ? s.interval_s : s.wake_s) * 2;
    s.interval_s = (uint16_t)(iv > INTERVAL_MAX_S ? INTERVAL_MAX_S : iv);
    const uint32_t b = (uint32_t)s.bump_mv + RELAPSE_BUMP_MV;
    s.bump_mv = (uint16_t)(b > BUMP_MAX_MV ? BUMP_MAX_MV : b);
  }
  if (!s.interval_s) s.interval_s = s.wake_s;
  if (s.failed >= FAIL_CAP) s.interval_s = INTERVAL_MAX_S;
  s.phase = PH_ASLEEP;
  s.entry = why;
  s.sleeps++;
  seal(s);
}

// Sleep with the block as it now is. Returns true when the device slept (on hardware this never returns). When the
// timer cannot be armed, `before` is put back, the reason is timer-failed, and the device stays up.
inline bool sleepOrStay(State& s, const State& before, const Io& io) {
  if (io.sleep(io.user, s.interval_s)) return true;
  s = before;
  if (s.phase == PH_ASLEEP) s.phase = PH_NONE;
  s.recovery = V_TIMER_FAILED;
  seal(s);
  return false;
}

enum Outcome : uint8_t { O_CONTINUE = 0, O_SLEPT = 1 };
struct Boot {
  Outcome outcome;
  uint8_t note;   // V_BUTTON: a maintenance lease for this boot; V_STATE_LOST: the block was damaged and cleared
};

// The boot guard: right after the board starts, before anything else does. `trial_pending`: the running image is on
// trial (UPDATES.md section 7), where a reset would roll the update back.
inline Boot boot(State& s, Wake w, bool trial_pending, const Io& io) {
  if (!valid(s)) {
    const bool damaged = s.magic == MAGIC;   // a block that was there; anything else is a power-on's random memory
    clear(s);
    return {O_CONTINUE, (uint8_t)(damaged ? V_STATE_LOST : V_NONE)};
  }
  if (!s.enabled) return {O_CONTINUE, V_NONE};
  if (s.phase == PH_ASLEEP) {
    if (w == W_BUTTON) {   // someone pressed the button: awake for the lease, whatever the level
      s.phase = PH_NONE;
      s.recovery = V_BUTTON;
      seal(s);
      return {O_CONTINUE, V_BUTTON};
    }
    if (w != W_TIMER) {    // reset by hand, or a crash: a normal boot, the loop decides again
      s.phase = PH_NONE;
      s.recovery = V_RESET;
      seal(s);
      return {O_CONTINUE, V_NONE};
    }
    const uint16_t want = recoverMv(s);
    for (uint8_t i = 0; i < BURSTS; i++) {
      if (i) io.wait(io.user, BURST_GAP_MS);
      const uint16_t m = burst(io);
      if (!m) {            // cannot measure: never asleep on an unknown level
        s.phase = PH_NONE;
        s.recovery = V_SENSOR_INVALID;
        seal(s);
        return {O_CONTINUE, V_NONE};
      }
      if (m < want) {      // still low: back to sleep, nothing else started and nothing written
        const State before = s;
        s.sleeps++;
        seal(s);
        return {sleepOrStay(s, before, io) ? O_SLEPT : O_CONTINUE, V_NONE};
      }
    }
    s.phase = PH_PROBATION;
    s.recovery = V_VOLTAGE;
    seal(s);
    return {O_CONTINUE, V_NONE};
  }
  if (w == W_BROWNOUT) {   // the supply collapsed under load: the bar is the recovery voltage, not critical
    const uint16_t m = burst(io);
    if (!m || m >= recoverMv(s)) return {O_CONTINUE, V_NONE};
    const bool emergency = (uint32_t)m + EMERGENCY_BELOW_MV < s.critical_mv;
    if (trial_pending && !emergency) return {O_CONTINUE, V_NONE};
    const State before = s;
    markAsleep(s, E_BROWNOUT);
    return {sleepOrStay(s, before, io) ? O_SLEPT : O_CONTINUE, V_NONE};
  }
  return {O_CONTINUE, V_NONE};
}

// The loop's part: when to sleep, and what to report.
class Controller {
public:
  typedef const char* (*BusyFn)(void* user, bool emergency);   // BusyGate::reason over the role's own state

  // One loop pass, with the settings the level tracker holds. Keeps the block's copy of the settings current. Returns
  // the seconds to sleep when the caller must sleep now (the block is already marked asleep); the caller then calls
  // sleepFailed() if the timer cannot be armed. `note`: what the boot guard found (Boot::note), read on the first pass.
  uint32_t tick(uint32_t now, State& s, const BatteryLevel& bl, bool external, uint16_t adc_x1000, BusyFn busy,
                void* user, uint8_t note = V_NONE) {
    if (!_begun) {
      _begun = true;
      _begin = now;
      _lease = note == V_BUTTON;
    }
    _s = &s;
    sync(s, bl.settings(), adc_x1000);
    if (bl.windows() != _seen) {
      _seen = bl.windows();
      _crit = bl.level() == BatteryLevel::CRITICAL ? (uint8_t)(_crit < 255 ? _crit + 1 : 255) : 0;
    }
    _why = nullptr;
    const bool go = decide(now, s, bl, external, busy, user);
    if (!go) { _settle_at = 0; return 0; }
    // Every condition holds: once more after SETTLE_MS, so the answers and events already queued (the reply to the
    // job.confirm that ended the last open job, say) go out first.
    if (!_settle_at) { _settle_at = now + SETTLE_MS; if (!_settle_at) _settle_at = 1; }
    if ((int32_t)(now - _settle_at) < 0) { _eff = CONFIRMING; return 0; }
    _settle_at = 0;
    _before = s;
    markAsleep(s, _emergency ? E_EMERGENCY : E_CRITICAL);
    return s.interval_s;
  }

  // The timer could not be armed: nothing slept. The block goes back as it was, and the next window tries again.
  void sleepFailed(State& s) {
    s = _before;
    s.recovery = V_TIMER_FAILED;
    seal(s);
    _timer_failed = true;
    _failed_at = _seen;
    _eff = BLOCKED;
    _why = "timer-failed";
  }

  // "off", "armed", "confirming", "probation" or "blocked:<reason>" (lease, level-unknown, external-power,
  // timer-failed, or BusyGate's flash-write, update-trial, update-transfer, job-open).
  void effective(char* out, size_t cap) const {
    switch (_eff) {
      case OFF: snprintf(out, cap, "off"); break;
      case ARMED: snprintf(out, cap, "armed"); break;
      case CONFIRMING: snprintf(out, cap, "confirming"); break;
      case PROBATION: snprintf(out, cap, "probation"); break;
      default: snprintf(out, cap, "blocked:%s", _why ? _why : "unknown"); break;
    }
  }
  // The block the last pass saw, while the guard is on and it is valid; null otherwise.
  const State* state() const { return _s && valid(*_s) && _s->enabled ? _s : nullptr; }
  uint8_t criticalWindows() const { return _crit; }

private:
  enum Eff : uint8_t { OFF = 0, ARMED, CONFIRMING, PROBATION, BLOCKED };

  // Whether every condition for a sleep holds now; sets what effective() reports otherwise.
  bool decide(uint32_t now, State& s, const BatteryLevel& bl, bool external, BusyFn busy, void* user) {
    const BatteryGuard::Cfg& c = bl.settings();
    if (!c.enabled) { _eff = OFF; _crit = 0; return false; }
    if (s.phase == PH_PROBATION && now - _begin >= PROBATION_MS) {   // a whole probation without a relapse
      s.phase = PH_NONE;
      s.failed = 0;
      s.bump_mv = 0;
      s.interval_s = s.wake_s;
      seal(s);
    }
    if (_lease && now - _begin >= LEASE_MS) _lease = false;
    _eff = BLOCKED;
    if (_lease) { _why = "lease"; return false; }
    const BatteryLevel::Level lv = bl.level();
    if (lv == BatteryLevel::UNKNOWN) { _why = "level-unknown"; return false; }
    if (external) { _why = "external-power"; return false; }
    if (lv != BatteryLevel::CRITICAL) { _eff = s.phase == PH_PROBATION ? PROBATION : ARMED; return false; }
    const bool emergency = bl.valid() && (uint32_t)bl.lastMean() + EMERGENCY_BELOW_MV < c.critical_mv;
    if (_crit < CONFIRM_WINDOWS && !emergency) { _eff = CONFIRMING; return false; }
    if (_timer_failed && _failed_at == _seen) { _why = "timer-failed"; return false; }   // once per window
    const char* r = busy ? busy(user, emergency) : nullptr;
    if (r) { _why = r; return false; }
    _emergency = emergency;
    return true;
  }


  static void sync(State& s, const BatteryGuard::Cfg& c, uint16_t adc_x1000) {
    if (!c.enabled) {
      if (s.magic || s.crc) clear(s);
      return;
    }
    if (!valid(s)) clear(s);
    if (valid(s) && s.enabled && s.low_mv == c.low_mv && s.critical_mv == c.critical_mv &&
        s.margin_mv == c.margin_mv && s.wake_s == c.wake_s && s.adc_x1000 == adc_x1000)
      return;
    const bool wake_changed = s.wake_s != c.wake_s;
    s.enabled = 1;
    s.low_mv = c.low_mv;
    s.critical_mv = c.critical_mv;
    s.margin_mv = c.margin_mv;
    s.wake_s = c.wake_s;
    s.adc_x1000 = adc_x1000;
    if (!s.interval_s || (wake_changed && !s.failed)) s.interval_s = c.wake_s;
    seal(s);
  }

  State* _s = nullptr;
  State _before = {};
  uint32_t _begin = 0;
  const char* _why = nullptr;
  uint16_t _seen = 0;
  uint16_t _failed_at = 0;
  uint8_t _crit = 0;
  uint32_t _settle_at = 0;
  uint8_t _eff = OFF;
  bool _begun = false, _lease = false, _timer_failed = false, _emergency = false;
};

// The one controller of this firmware.
inline Controller& controller() {
  static Controller c;
  return c;
}

}  // namespace BatterySleep
