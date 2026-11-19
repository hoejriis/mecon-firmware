#pragma once
// The human-readable ping on the management channel (docs/contract/MANAGEMENT_CHANNEL.md section 7). A channel member
// sends the text "ping" or "ping <device name>" as an ordinary channel message; an enrolled device whose management
// channel is bound, and that heard it on that channel, answers with one short ordinary channel message. Pure logic: the
// roles hand it the text, the clock and a random number, and send what it builds.
//
// The ping grants nothing: whoever holds the channel key may send it, and the reply carries only what an administrator
// already reads from the device's status (name, version, uptime, battery, temperature, radio counts), never a key, an
// identifier, a position or a message. A reply starts with "pong", so no reply is ever read as a ping.
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Iface {

const uint32_t FMC_BROADCAST_GAP_MS = 300000;   // at most one reply to a broadcast ping per device in this time
const uint32_t FMC_BROADCAST_JITTER_MS = 30000; // a broadcast reply waits a random 0 to this long
const uint32_t FMC_TARGET_JITTER_MS = 2000;     // a targeted reply waits a random 0 to this long (devices that share a name)
const uint32_t FMC_TARGET_GAP_MS = 10000;       // at least this long between two targeted replies
const uint32_t FMC_RETRY_MS = 5000;            // a reply with no airtime to spare waits this long, then asks again
const uint8_t FMC_RETRIES = 6;
const size_t FMC_REPLY_MAX = 96;                // the reply text, without its terminator

enum FmcAsk : uint8_t { FMC_NONE = 0, FMC_ALL = 1, FMC_ONE = 2 };

inline char fmcLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// What a channel message asks of a device named `name`: the text is "<sender>: <message>" as the native channel carries it.
// "ping" asks every device, "ping <name>" the device with that name (compared without regard to case, spaces at either end of
// the name ignored); anything else, and a ping for another name, asks nothing.
inline FmcAsk fmcParse(const char* text, const char* name) {
  const char* sep = strstr(text, ": ");
  if (!sep) return FMC_NONE;
  const char* m = sep + 2;
  while (*m == ' ') m++;
  const char* p = "ping";
  for (int i = 0; i < 4; i++) if (fmcLower(m[i]) != p[i]) return FMC_NONE;
  m += 4;
  if (*m == 0) return FMC_ALL;
  if (*m != ' ') return FMC_NONE;
  while (*m == ' ') m++;
  size_t n = strlen(m);
  while (n > 0 && m[n - 1] == ' ') n--;
  if (n == 0) return FMC_ALL;
  const char* nm = name;
  while (*nm == ' ') nm++;
  size_t k = strlen(nm);
  while (k > 0 && nm[k - 1] == ' ') k--;
  if (n != k) return FMC_NONE;
  for (size_t i = 0; i < n; i++) if (fmcLower(m[i]) != fmcLower(nm[i])) return FMC_NONE;
  return FMC_ONE;
}

struct FmcFacts {
  const char* version = "";
  uint32_t uptime_s = 0;
  uint16_t battery_mv = 0;      // 0: not known
  float temp_c = NAN;           // NAN: not known
  bool counts = false;          // rx and tx are known
  uint32_t rx = 0, tx = 0;
  uint8_t err_flags = 0;
};

// Appends printf text to out at n, never past cap - 1.
inline void fmcAdd(char* out, size_t cap, size_t& n, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
inline void fmcAdd(char* out, size_t cap, size_t& n, const char* fmt, ...) {
  if (n + 1 >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  const int w = vsnprintf(out + n, cap - n, fmt, ap);
  va_end(ap);
  if (w > 0) n = ((size_t)w < cap - n) ? n + (size_t)w : cap - 1;
}

// "pong v1.18.0-mecon.0.3.9 up 2d3h bat 4.05V temp 22.5C rx 120 tx 45 ok": only what is known. The length, never past cap - 1.
inline size_t fmcBuild(char* out, size_t cap, const FmcFacts& f) {
  if (cap < 2) return 0;
  size_t n = 0;
  fmcAdd(out, cap, n, "pong");
  if (f.version && f.version[0]) fmcAdd(out, cap, n, " %s", f.version);
  const unsigned long u = f.uptime_s;
  if (u >= 86400) fmcAdd(out, cap, n, " up %lud%luh", u / 86400, (u % 86400) / 3600);
  else if (u >= 3600) fmcAdd(out, cap, n, " up %luh%lum", u / 3600, (u % 3600) / 60);
  else if (u >= 60) fmcAdd(out, cap, n, " up %lum%lus", u / 60, u % 60);
  else fmcAdd(out, cap, n, " up %lus", u);
  if (f.battery_mv) fmcAdd(out, cap, n, " bat %lu.%02luV", (unsigned long)f.battery_mv / 1000, ((unsigned long)f.battery_mv % 1000) / 10);
  if (!isnan(f.temp_c) && f.temp_c > -100.0f && f.temp_c < 200.0f) {
    const long t10 = lroundf(f.temp_c * 10.0f);
    const unsigned long a = (unsigned long)(t10 < 0 ? -t10 : t10);
    fmcAdd(out, cap, n, " temp %s%lu.%luC", t10 < 0 ? "-" : "", a / 10, a % 10);
  }
  if (f.counts) {
    fmcAdd(out, cap, n, " rx %lu tx %lu", (unsigned long)f.rx, (unsigned long)f.tx);
    if (f.err_flags) fmcAdd(out, cap, n, " err %u", (unsigned)f.err_flags);
    else fmcAdd(out, cap, n, " ok");
  }
  out[n] = 0;
  return n;
}

// The state of the answer: one pending reply, the times of the last replies, and the last pings heard.
class FmcPing {
public:
  // A channel message heard on the bound channel by an enrolled device: schedules a reply when it asks for one. True when it did.
  bool onText(const char* text, const char* name, uint32_t now, uint32_t rnd, uint32_t stamp) {
    const FmcAsk ask = fmcParse(text, name);
    if (ask == FMC_NONE) return false;
    const uint32_t id = hash(text, stamp);
    for (uint8_t i = 0; i < SEEN; i++) if (_seen_ok[i] && _seen[i] == id) return false;   // the same ping again
    _seen[_next] = id; _seen_ok[_next] = true; _next = (uint8_t)((_next + 1) % SEEN);
    if (_pending) return false;   // one reply is waiting: the queue is one deep
    if (ask == FMC_ALL) {
      if (_bcast_ok && (uint32_t)(now - _bcast_ms) < FMC_BROADCAST_GAP_MS) return false;
      _due = now + rnd % (FMC_BROADCAST_JITTER_MS + 1);
    } else {
      if (_target_ok && (uint32_t)(now - _target_ms) < FMC_TARGET_GAP_MS) return false;
      _due = now + rnd % (FMC_TARGET_JITTER_MS + 1);
    }
    _pending = true; _pending_all = ask == FMC_ALL; _retries = 0;
    return true;
  }
  // True once when the pending reply is due; the caller then builds and sends it, or calls dropped() when it could not.
  bool due(uint32_t now) {
    if (!_pending || (int32_t)(now - _due) < 0) return false;
    _pending = false;
    if (_pending_all) { _bcast_ok = true; _bcast_ms = now; } else { _target_ok = true; _target_ms = now; }   // the gap runs from the send
    return true;
  }
  // The reply the caller took could not go out for want of airtime: it waits FMC_RETRY_MS and tries again, up to FMC_RETRIES times,
  // then it is given up (true: it waits). A reply that is given up does not count against the gap.
  bool defer(uint32_t now) {
    if (_retries < FMC_RETRIES) { _retries++; _due = now + FMC_RETRY_MS; _pending = true; return true; }
    dropped();
    return false;
  }
  void dropped() { _retries = 0; if (_pending_all) _bcast_ok = false; else _target_ok = false; }
  // The role stopped being eligible (unbound, not enrolled): nothing waits.
  void cancel() { _pending = false; }
  bool pending() const { return _pending; }

private:
  static const uint8_t SEEN = 4;
  static uint32_t hash(const char* s, uint32_t stamp) {
    uint32_t h = 2166136261u ^ stamp;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
  }
  bool _pending = false, _pending_all = false;
  uint32_t _due = 0;
  uint8_t _retries = 0;
  bool _bcast_ok = false, _target_ok = false;
  uint32_t _bcast_ms = 0, _target_ms = 0;
  uint32_t _seen[SEEN] = {0};
  bool _seen_ok[SEEN] = {false};
  uint8_t _next = 0;
};

}  // namespace Iface
