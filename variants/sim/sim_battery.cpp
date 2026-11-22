// The protective sleep's platform part on the simulated board (src/helpers/BatterySleepRun.h). Nothing here is firmware
// logic. Beside the node's data directory:
//   <dir>.rtcmem     the RTC memory block: kept across every restart of the process, as RTC memory is across a deep sleep
//                    and a software reset; a test removes it for a power-on
//   <dir>.rtcoff     the bench test offset's RTC word
//   <dir>.wake       what starts the next boot, one word, read once and removed: "timer" (a protective sleep writes it),
//                    "button" or "brownout"; no file: any other reset
//   <dir>.slept      one line per protective sleep: the seconds it asked for
//   <dir>.timerfail  while it exists, arming the wake timer fails
// A protective sleep ends the process with status 77; the test plays the timer and starts the node again.
#include "target.h"
#include "sim_config.h"
#include <helpers/BatterySleepRun.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(BATTERY_SLEEP_BOARD) && defined(ENABLE_MECON_IFACE)
#include <helpers/updates/UpdSlot.h>

namespace {
std::string path(const char* ext) {
  std::string d = g_sim.dir;
  while (d.size() > 1 && d.back() == '/') d.pop_back();
  return d + ext;
}
void writeAtomically(const char* ext, const void* p, size_t n) {
  const std::string to = path(ext), tmp = to + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return;
  fwrite(p, 1, n, f);
  fclose(f);
  rename(tmp.c_str(), to.c_str());
}
}  // namespace

namespace BatterySleep {

State& rtc() {
  static State s;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    memset(&s, 0, sizeof(s));
    if (FILE* f = fopen(path(".rtcmem").c_str(), "rb")) {
      if (fread(&s, 1, sizeof(s), f) != sizeof(s)) memset(&s, 0, sizeof(s));
      fclose(f);
    }
  }
  return s;
}

void rtcCommit() { writeAtomically(".rtcmem", &rtc(), sizeof(State)); }

Wake wakeCause() {
  static int cause = -1;
  if (cause < 0) {
    cause = W_OTHER;
    char w[16] = {0};
    if (FILE* f = fopen(path(".wake").c_str(), "r")) {
      if (fscanf(f, "%15s", w) != 1) w[0] = 0;
      fclose(f);
      remove(path(".wake").c_str());
    }
    if (!strcmp(w, "timer")) cause = W_TIMER;
    else if (!strcmp(w, "button")) cause = W_BUTTON;
    else if (!strcmp(w, "brownout")) cause = W_BROWNOUT;
  }
  return (Wake)cause;
}

bool trialPending() { return Upd::runningState() == Upd::RS_PENDING; }
uint16_t readMv() { return offsetReading(board.getBattMilliVolts()); }
void restoreAdc(uint16_t) {}
void early() {}

bool deepSleep(uint32_t secs, bool) {
  if (!secs || access(path(".timerfail").c_str(), F_OK) == 0) return false;
  if (FILE* f = fopen(path(".slept").c_str(), "a")) {
    fprintf(f, "%u\n", (unsigned)secs);
    fclose(f);
  }
  const char t[] = "timer\n";
  writeAtomically(".wake", t, sizeof(t) - 1);
  fflush(nullptr);
  exit(77);
}

#ifdef MECON_BATTERY_TEST_OFFSET
int16_t& testOffset() {
  static int16_t off = 0;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    if (FILE* f = fopen(path(".rtcoff").c_str(), "r")) {
      int v = 0;
      if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 2000) off = (int16_t)v;
      fclose(f);
    }
  }
  return off;
}
void testOffsetCommit() {
  char b[16];
  const int n = snprintf(b, sizeof(b), "%d\n", (int)testOffset());
  writeAtomically(".rtcoff", b, (size_t)n);
}
#endif

}  // namespace BatterySleep
#endif
