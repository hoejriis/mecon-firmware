#pragma once

#include <stdint.h>
#include <string.h>

/**
 * Sliding one-hour airtime window used to enforce the duty cycle.  Dependency-free (no Arduino, no Dispatcher),
 * shared by every path that puts a LoRa packet on the air: mesh::Dispatcher and the KISS modem.
 *
 * Transmitted airtime is kept in DUTY_NUM_BINS bins of DUTY_BIN_MS each: 121 x 30 s covers the hour plus
 * the bin being filled, so everything younger than an hour is always counted (at most one bin of older
 * airtime is counted too, which can only make the node more conservative).  Cost: 121 x 2 = 242 bytes.
 */
#define DUTY_WINDOW_MS   3600000UL
#define DUTY_BIN_MS      30000UL
#define DUTY_NUM_BINS    (DUTY_WINDOW_MS / DUTY_BIN_MS + 1)

namespace mesh {

class DutyWindow {
  uint16_t bins[DUTY_NUM_BINS];   // airtime (ms) of each 30 s bin of the trailing hour, ring buffer
  uint32_t total;                 // sum of bins
  bool     is_seeded;             // the hour has been seeded at boot (see seed())
  uint8_t  cur;                   // index of the bin being filled
  unsigned long cur_start;        // millis() at which that bin started

public:
  DutyWindow() { reset(0); }

  void reset(unsigned long now) {
    memset(bins, 0, sizeof(bins));
    total = 0;
    is_seeded = false;
    cur = 0;
    cur_start = now;
  }

  bool seeded() const { return is_seeded; }
  uint32_t sum() const { return total; }

  // The window lives in RAM and is not persisted, so a reboot or crash loop would otherwise start every hour empty
  // and exceed the limit across boots.  On first use after boot the hour is seeded as if the previous hour had used
  // exactly the limit, spread evenly over the bins; capacity then accrues at the limit rate as those bins age out,
  // and a sliding hour that crosses a reboot cannot exceed the limit.
  void seed(uint32_t limit_ms) {
    if (is_seeded) return;
    is_seeded = true;
    uint32_t each = limit_ms / DUTY_NUM_BINS, rest = limit_ms % DUTY_NUM_BINS;
    total = 0;
    for (uint32_t i = 0; i < DUTY_NUM_BINS; i++) {
      bins[i] = each + (i < rest ? 1 : 0);
      total += bins[i];
    }
  }

  // Rotate the ring forward so that the bin being filled is the one `now` falls in.  seed() must have run.
  void advance(unsigned long now) {
    unsigned long elapsed = now - cur_start;
    if (elapsed < DUTY_BIN_MS) return;
    unsigned long steps = elapsed / DUTY_BIN_MS;
    if (steps >= DUTY_NUM_BINS) {   // idle for longer than the window: nothing left to count
      memset(bins, 0, sizeof(bins));
      total = 0;
      cur = 0;
      cur_start = now;
      return;
    }
    for (unsigned long i = 0; i < steps; i++) {
      cur = (cur + 1) % DUTY_NUM_BINS;
      total -= bins[cur];     // the oldest bin falls out of the window
      bins[cur] = 0;
    }
    cur_start += steps * DUTY_BIN_MS;
  }

  void record(unsigned long now, uint32_t air_ms) {
    advance(now);
    uint32_t room = 0xFFFF - bins[cur];
    if (air_ms > room) air_ms = room;
    bins[cur] += air_ms;
    total += air_ms;
  }

  // How long until enough of the oldest airtime has left the window for `need_ms` to be freed.
  // Bin j (0 = the one being filled) is counted until the last moment in it is an hour old, i.e. until
  // cur_start - j*BIN + BIN + WINDOW.
  uint32_t waitMs(unsigned long now, uint32_t need_ms) const {
    uint32_t freed = 0;
    for (int j = DUTY_NUM_BINS - 1; j >= 0; j--) {
      freed += bins[(cur + DUTY_NUM_BINS - j) % DUTY_NUM_BINS];
      if (freed >= need_ms) {
        long wait = (long)(DUTY_BIN_MS + DUTY_WINDOW_MS) - (long)j * (long)DUTY_BIN_MS - (long)(now - cur_start);
        return (wait < 1 ? 1 : (uint32_t)wait) + 1;
      }
    }
    return DUTY_WINDOW_MS;   // not reachable when the packet alone fits the limit
  }
};

}
