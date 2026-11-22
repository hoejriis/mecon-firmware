#pragma once
// The protective sleep on a board: the boot guard, the loop's part and the bench offset (BatterySleep.h has the policy).
// Built where BATTERY_SLEEP_BOARD and the device interface are: the Heltec V3 and V4 Companion and Repeater, and the
// simulated board. The platform part is src/helpers/BatterySleepEsp32.cpp and variants/sim/sim_battery.cpp.
#include <Arduino.h>
#include <MeshCore.h>
#include "BatterySleep.h"

#if defined(BATTERY_SLEEP_BOARD) && defined(ENABLE_MECON_IFACE)
#define BATTERY_SLEEP 1
#endif

namespace BatterySleep {

// The platform's part.
State& rtc();                       // the block in RTC memory (the simulated board: a file beside its data directory)
void rtcCommit();                   // the simulated board writes the file; on the ESP32 the memory is the store
Wake wakeCause();                   // what started this boot
bool trialPending();                // the running image is on trial (installed, not yet marked valid)
uint16_t readMv();                  // one battery reading through the board, the bench offset taken off
void restoreAdc(uint16_t x1000);    // the ADC factor the block carries, before the prefs load it (0: the default)
// Sleep for secs. Arms the timer first: false when it cannot, and then nothing has been turned off and the device
// keeps running. Otherwise it never returns. peripherals_up: the display, the radio and the GPS have started.
bool deepSleep(uint32_t secs, bool peripherals_up);
void early();                       // first thing in setup(): let go of the pads a protective sleep held

// The bench test offset (MECON_BATTERY_TEST_OFFSET, never in a release build): mV taken off each reading the guard
// sees, kept in RTC memory so it survives the sleep. The cable's `battery test offset <mV>` sets it.
#ifdef MECON_BATTERY_TEST_OFFSET
int16_t& testOffset();
void testOffsetCommit();
#endif

inline uint16_t offsetReading(uint16_t mv) {
#ifdef MECON_BATTERY_TEST_OFFSET
  const int16_t off = testOffset();
  if (mv && off > 0) return mv > (uint16_t)off ? (uint16_t)(mv - off) : 1;   // 1: below the valid range, so unknown
#endif
  return mv;
}

// Why the device may not sleep now (BusyGate.h), from the role's job service and update service.
template <class Jobs, class Updates> const char* busyOf(Jobs& jobs, const Updates& updates, bool emergency) {
  BusyGate::Inputs in;
  in.flash_write = BusyGate::flashWrites() != 0;
  auto* t = jobs.peek();
  in.job_open = t && t->openJob();
  in.update_transfer = updates.transferBusy();
  in.update_trial = trialPending();
  return BusyGate::reason(in, emergency);
}

// What the boot guard found, for the loop's first pass.
inline uint8_t& bootNote() {
  static uint8_t n = V_NONE;
  return n;
}

#ifdef BATTERY_SLEEP
// Whether RTC memory holds a guard block, valid or damaged: false with the guard off and after a power-on.
inline bool holdsBlock() { return rtc().magic == MAGIC; }

// The boot guard: right after board.begin(), before the display, the radio, the file system and any network. On a
// timer wake below the recovery voltage it does not return. With the guard off (no block in RTC memory) it reads
// nothing and returns at once.
inline void bootGuard() {
  State& s = rtc();
  if (s.magic != MAGIC) return;   // a power-on, or the guard off: nothing to decide, nothing read
  restoreAdc(s.adc_x1000);
  Io io;
  io.read = [](void*) -> uint16_t { return readMv(); };
  io.wait = [](void*, uint32_t ms) { delay(ms); };
  io.sleep = [](void*, uint32_t secs) -> bool { rtcCommit(); return deepSleep(secs, false); };
  io.user = nullptr;
  const Boot b = boot(s, wakeCause(), trialPending(), io);
  rtcCommit();
  bootNote() = b.note;
}

// The loop's part, after the level tracker's reading. busy: BusyGate::reason over the role's jobs and update service.
// before: called once the guard decided to sleep and before the timer is armed (the Companion saves dirty contacts).
inline void loopTick(uint32_t now, const BatteryLevel& bl, mesh::MainBoard* b, Controller::BusyFn busy, void* user,
                     void (*before)(void*)) {
  State& s = rtc();
  const State was = s;
  const bool external = b && b->hasUsbPowerDetect() && b->isUsbPowered();
  const float adc = b ? b->getAdcMultiplier() : 0.0f;
  const uint16_t adc_x1000 = adc > 0.0f && adc < 60.0f ? (uint16_t)(adc * 1000.0f + 0.5f) : 0;
  const uint32_t secs = controller().tick(now, s, bl, external, adc_x1000, busy, user, bootNote());
  if (secs) {
    rtcCommit();
    if (before) before(user);
    if (!deepSleep(secs, true)) controller().sleepFailed(s);
  }
  if (memcmp(&was, &s, sizeof(s)) != 0) rtcCommit();
}
#endif

#ifdef MECON_BATTERY_TEST_OFFSET
// "battery test offset" (reads it) and "battery test offset <0-2000>" (sets it). The role calls this for its USB
// cable's lines only. False when the line is not this one.
inline bool testLine(const char* cmd, char* reply, size_t cap) {
  static const char P[] = "battery test offset";
  const size_t n = sizeof(P) - 1;
  if (memcmp(cmd, P, n) != 0 || (cmd[n] != 0 && cmd[n] != ' ')) return false;
  if (!cmd[n]) {
    snprintf(reply, cap, "> %d", (int)testOffset());
    return true;
  }
  const char* v = cmd + n + 1;
  uint32_t x = 0;
  size_t k = 0;
  for (; v[k] >= '0' && v[k] <= '9' && k < 5; k++) x = x * 10 + (uint32_t)(v[k] - '0');
  if (!k || v[k] || x > 2000) {
    snprintf(reply, cap, "Err - battery test offset 0 to 2000 (mV)");
    return true;
  }
  testOffset() = (int16_t)x;
  testOffsetCommit();
  snprintf(reply, cap, "OK - battery test offset %u mV (bench build: the guard reads the battery that much lower)",
           (unsigned)x);
  return true;
}
#endif

}  // namespace BatterySleep
