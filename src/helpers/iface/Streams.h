#pragma once
// Telemetry streams (docs/contract/TELEMETRY.md): the stream table, per-transport subscriptions, sub.start / sub.stop,
// and the writers for the families every role can produce. The role glue (MeconIface, ConsoleIface) owns its
// transports and says which streams its build can produce; this file owns their meaning, so a stream reads the same on
// every role and transport.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "IfaceDispatch.h"
#include "../AuditRing.h"
#include "IfaceJson.h"
#include "../GnssHealth.h"

namespace mesh { class Dispatcher; class Radio; class MainBoard; }
class SensorManager;
class LocationProvider;

namespace Iface {

enum StreamId : uint8_t { SID_HEALTH = 0, SID_RADIO, SID_POWER, SID_RUNTIME, SID_OBSERVATIONS, SID_CONNECTIVITY, SID_ENVIRONMENT, SID_INVENTORY, SID_LOGS, SID_LOCATION, STREAM_COUNT };

struct StreamDef {
  const char* name;
  uint16_t min_s, max_s, default_s;   // period bounds and default, seconds; all 0 for an event stream (no period)
  uint8_t cls;                        // the CLI privilege class a subscriber needs (Cli::Class: 0 read ... 3 posture)
  bool events() const { return default_s == 0; }
};
extern const StreamDef STREAMS[STREAM_COUNT];
int streamIndex(const char* name, size_t len);   // -1 when there is no such stream

inline uint32_t streamBit(StreamId s) { return 1u << s; }

struct Sub {
  bool active = false;
  uint32_t period_ms = 0;
  uint32_t until = 0;        // millis() at which it lapses unless renewed
};
struct SubSet {
  Sub s[STREAM_COUNT];
  void clear() { for (Sub& x : s) x = Sub(); }
};

// sub.start {"stream", "period_s", "lifetime_s"} and sub.stop {"stream"} against one transport's set. supported is the
// bit mask of streams this build produces; any other stream is unknown-stream. *started gets the stream a new
// subscription began on (so the caller can send its first event at once), or -1.
// allow(user, cls) says whether the caller may use a stream of that class; it decides with the caller's real context,
// like the CLI line the data would come from. A caller that may not gets ST_REFUSED.
typedef bool (*StreamAllowFn)(void* user, uint8_t cls);
Status subStart(OpContext& c, SubSet* set, uint32_t supported, uint32_t now, int* started, StreamAllowFn allow,
                void* allow_user);
Status subStop(OpContext& c, SubSet* set, uint32_t supported);

// Device-wide pacing: a stream runs at the shortest period any transport asked for, and each event is built once and
// goes, with the same (epoch, seq), to every transport subscribed.
struct StreamClock {
  uint32_t seq[STREAM_COUNT] = {0};
  uint32_t next[STREAM_COUNT] = {0};
  // Lapses expired subscriptions in each set, and returns the bit mask of streams due now (advancing their clocks).
  uint32_t due(SubSet* const* sets, int nsets, uint32_t now);
  void scheduleNow(int stream, uint32_t now) { if (stream >= 0) next[stream] = now; }
};

// A rate limit for an event stream: at most RATE events a second (a burst of RATE), then dropped and counted; the next
// event that goes out says how many were dropped before it.
template <uint32_t RATE>
struct RateLimiter {
  uint32_t tokens = RATE;
  uint32_t last_ms = 0;
  uint32_t dropped = 0;
  bool take(uint32_t now) {
    const uint32_t refill = (now - last_ms) * RATE / 1000;
    if (refill) {
      tokens = tokens + refill > RATE ? RATE : tokens + refill;
      last_ms = now;
    }
    if (!tokens) { dropped++; return false; }
    tokens--;
    return true;
  }
};
// The observations stream: at most OBS_RATE events a second.
const uint32_t OBS_RATE = 10;
typedef RateLimiter<OBS_RATE> ObsLimiter;

// The logs stream (class posture): each entry the audit ring recorded since the last poll, at most LOGS_RATE a second.
// The main loop polls the ring's `recorded` counter (nothing is pushed from the task that records). An entry the ring
// no longer shows (evicted, or folded into a later repeat) and an entry over the rate are dropped and counted in
// `dropped` of the next event sent. A folded repeat is sent again with its new count.
const uint32_t LOGS_RATE = 5;
struct LogFeed {
  RateLimiter<LOGS_RATE> limit;
  uint32_t seen = 0;   // ring.recorded at the last poll
  // Nobody is subscribed: what is recorded now is history.
  void skip(const Posture::AuditRing& r) { seen = r.recorded; }
  // The entries to send now, oldest first, into out (room for AuditRing::CAPACITY); *dropped is the count to report
  // with the first. Entries not sent stay counted until an event goes out.
  int poll(uint32_t now, const Posture::AuditRing& r, Posture::AuditEntry* out, uint32_t* dropped) {
    const uint32_t fresh = r.recorded - seen;
    if (!fresh) return 0;
    seen = r.recorded;
    uint32_t held = 0;
    int n = 0;
    for (int i = 0; i < r.n; i++) {
      if ((int32_t)(r.serial[i] - (r.recorded - fresh)) <= 0) continue;   // recorded before the last poll
      held++;
      if (limit.take(now)) out[n++] = r.e[i];
    }
    limit.dropped += fresh - held;   // recorded since, but no longer shown
    if (!n) return 0;
    *dropped = limit.dropped;
    limit.dropped = 0;
    return n;
  }
};
// The data object of one logs event: names, never codes, and nothing but what the audit entry holds (it holds no
// secret, command argument or password). ts is null while the entry's clock was not set. Returns the length written,
// or -1 when it does not fit.
inline int formatLogData(char* out, size_t cap, const Posture::AuditEntry& a, uint32_t dropped) {
  char ts[16];
  if (a.ts >= 1704067200) snprintf(ts, sizeof(ts), "%lu", (unsigned long)a.ts);
  else snprintf(ts, sizeof(ts), "null");
  const int n = snprintf(out, cap, "{\"ts\":%s,\"event\":\"%s\",\"caller\":\"%s\",\"what\":\"%s\",\"count\":%u,\"dropped\":%lu}", ts,
                         Posture::AuditRing::eventName(a.event), Posture::AuditRing::callerName(a.caller),
                         Posture::AuditRing::whatName(a.event, a.what), (unsigned)a.count, (unsigned long)dropped);
  return n > 0 && (size_t)n < cap ? n : -1;
}
// Writes one logs event envelope (seq advanced in clock) into ev; its length, or -1.
int writeLog(char* ev, size_t cap, StreamClock& clock, uint32_t epoch, bool ts_known, uint32_t ts,
             const Posture::AuditEntry& a, uint32_t dropped);

// Writes one observations event envelope (seq advanced in clock) into ev; its length, or -1 when it does not fit.
int writeObservation(char* ev, size_t cap, StreamClock& clock, uint32_t epoch, bool ts_known, uint32_t ts, float snr,
                     float rssi, const uint8_t* raw, int len, uint32_t dropped);

// The capability names of the streams in supported ("stream.health", ...), for hello.
void writeStreamCapabilities(JsonWriter& w, uint32_t supported);

// ---- family writers (TELEMETRY.md section 3); each writes members into an open object

// What the role's radio driver counts: the driver classes differ, so the role reads them and passes the numbers.
struct RadioCounts {
  bool known = false;
  uint32_t rx_total = 0, tx_total = 0, rx_errors = 0;
};
void writeRadio(JsonWriter& w, mesh::Dispatcher& d, mesh::Radio& r, const RadioCounts& counts);

// Power facts the board can tell; a board that cannot tell leaves the field null (or absent, for detection hardware it
// does not have).
void writePower(JsonWriter& w, mesh::MainBoard& b);

// The longest pass of the main loop since the last read: ticked once per pass by the glue, read and reset by the
// runtime stream. 0 until two passes have been seen.
struct LoopGap {
  uint32_t last = 0, max = 0;
  void tick(uint32_t now) {
    if (last && now - last > max) max = now - last;
    last = now;
  }
  uint32_t take() { uint32_t m = max; max = 0; return m; }
};

// The environment stream: the attached sensors' readings (decoded from the LPP their manager produces, environment
// permission only, never location) and the MCU temperature where the board reads one. Returns how many readings it
// wrote; countEnvironment answers the same without writing, for the stream's discovery.
int writeEnvironment(JsonWriter& w, SensorManager& s, mesh::MainBoard& b);
int countEnvironment(SensorManager& s, mesh::MainBoard& b);

// One inventory count: {"count", "capacity"} under key.
void writeCount(JsonWriter& w, const char* key, int count, int capacity);

// Reset reason, boot times, and the loop's longest gap since the previous runtime event.
// heap_low: 1 or 0 where the build checks the heap floor (the Companion), -1 to leave the field out.
void writeRuntime(JsonWriter& w, mesh::MainBoard& b, LoopGap& gap, int heap_low = -1);

// The location stream (TELEMETRY.md): a GNSS fix as integers, never floats. lat_e6/lon_e6 in millionths of a degree,
// alt_m in metres; position members are null without a fix; age_s is null when the provider cannot tell or has had no
// fix this boot. Private: class C_PRIVATE, never in logs or any other stream.
struct LocationFix {
  bool fix = false;
  int32_t lat_e6 = 0, lon_e6 = 0, alt_m = 0;
  uint8_t sats = 0;
  int32_t age_s = -1;
};
inline void writeLocation(JsonWriter& w, const LocationFix& f) {
  w.key("fix"); w.boolean(f.fix);
  w.key("lat_e6"); if (f.fix) w.num(f.lat_e6); else w.null();
  w.key("lon_e6"); if (f.fix) w.num(f.lon_e6); else w.null();
  w.key("alt_m"); if (f.fix) w.num(f.alt_m); else w.null();
  w.key("sats"); w.unum(f.sats);
  w.key("age_s"); if (f.age_s >= 0) w.unum((uint32_t)f.age_s); else w.null();
}
// The fix of a location provider (sensors.getLocationProvider()); false when there is none.
bool readLocation(LocationProvider* p, LocationFix& out);

// Inventory entries (TELEMETRY.md, class private): a snapshot of the Companion's contacts and channels in pages, each
// page one event under the transport limit. Never a secret: no channel key, no shared secret, no path bytes.
struct ContactEntry {
  uint8_t key[32];
  const char* name;
  uint8_t type;          // ADV_TYPE_*
  bool fav;
  uint32_t last_advert;  // their clock
  int16_t hops;          // hops of the known path (path length & 63); -1: none, flood
  bool has_pos;
  int32_t lat_e6, lon_e6;
};
inline void writeContactEntry(JsonWriter& w, const ContactEntry& c) {
  static const char H[] = "0123456789abcdef";
  char key[65];
  for (int i = 0; i < 32; i++) { key[2 * i] = H[c.key[i] >> 4]; key[2 * i + 1] = H[c.key[i] & 15]; }
  key[64] = 0;
  static const char* const TYPES[] = {"none", "chat", "repeater", "room", "sensor"};
  w.beginObject();
  w.key("kind"); w.str("contact");
  w.key("key"); w.str(key);
  w.key("name"); w.str(c.name);
  w.key("type"); w.str(c.type < 5 ? TYPES[c.type] : "other");
  w.key("fav"); w.boolean(c.fav);
  w.key("last_advert"); w.unum(c.last_advert);
  w.key("hops"); w.num(c.hops);
  if (c.has_pos) { w.key("lat_e6"); w.num(c.lat_e6); w.key("lon_e6"); w.num(c.lon_e6); }
  w.endObject();
}
inline void writeChannelEntry(JsonWriter& w, int index, const char* name) {
  w.beginObject();
  w.key("kind"); w.str("channel");
  w.key("index"); w.num(index);
  w.key("name"); w.str(name);
  w.endObject();
}

// One page of an inventory snapshot, built in out: {"snapshot":n,"part":p,"contacts":{...},"channels":{...},
// "entries":[...],"last":bool}. add() takes one entry's complete JSON and returns false, leaving the page as it was,
// when the entry would not fit with the page's closing bytes; finish() closes it and returns its length (0 on error).
class InventoryPage {
public:
  InventoryPage(char* out, size_t cap) : _o(out), _cap(cap) {}
  bool begin(uint32_t snapshot, uint32_t part, int contacts, int contacts_cap, int channels, int channels_cap) {
    const int n = snprintf(_o, _cap, "{\"snapshot\":%lu,\"part\":%lu,\"contacts\":{\"count\":%d,\"capacity\":%d},"
                           "\"channels\":{\"count\":%d,\"capacity\":%d},\"entries\":[",
                           (unsigned long)snapshot, (unsigned long)part, contacts, contacts_cap, channels, channels_cap);
    if (n < 0 || (size_t)n >= _cap) return false;
    _n = (size_t)n; _count = 0;
    return true;
  }
  bool add(const char* entry, size_t len) {
    const size_t need = (_count ? 1 : 0) + len + TAIL;
    if (_n + need >= _cap) return false;
    if (_count) _o[_n++] = ',';
    memcpy(_o + _n, entry, len); _n += len; _count++;
    _o[_n] = 0;
    return true;
  }
  size_t finish(bool last) {
    const int n = snprintf(_o + _n, _cap - _n, "],\"last\":%s}", last ? "true" : "false");
    if (n < 0 || _n + (size_t)n >= _cap) return 0;
    _n += (size_t)n;
    return _n;
  }
  int count() const { return _count; }
private:
  static const size_t TAIL = 16;   // "],\"last\":false}" and the NUL
  char* _o;
  size_t _cap, _n = 0;
  int _count = 0;
};
// Whether the last reset was abnormal (a panic, the watchdog, a brownout or a lockup): 1 or 0, -1 when the board cannot
// tell. The runtime stream's abnormal_reset, and the job table's boot rule (OPERATIONS.md section 3.3).
int abnormalReset(mesh::MainBoard& b);

// The power stream's fields from any board type (mesh::MainBoard in the firmware, a fake one in the native test).
// The defaults answer false whether or not the board can tell, so external and charging are reported only by an
// initialised power manager or a board with its own detect pins (hasPowerDetect); otherwise they are null.
template <class Board> void writePowerOf(JsonWriter& w, Board& b) {
  const uint16_t mv = b.getBattMilliVolts();
  w.key("battery_mv"); if (mv) w.unum(mv); else w.null();
  const bool can_tell = b.isPwrMgtInitialised() || b.hasPowerDetect();
  w.key("external"); if (can_tell) w.boolean(b.isExternalPowered()); else w.null();
  w.key("charging"); if (can_tell) w.boolean(b.isChargerActive()); else w.null();
  if (b.hasUsbPowerDetect()) { w.key("usb_power"); w.boolean(b.isUsbPowered()); }
  if (b.hasSolarChargerDetect()) { w.key("solar"); w.boolean(b.isSolarChargerActive()); }
  const uint16_t boot_mv = b.getBootVoltage();
  if (boot_mv) { w.key("boot_mv"); w.unum(boot_mv); }
}

// The runtime stream's `gnss` object. Called only for a board that has a receiver, so it is absent
// otherwise. `fix` is null while the receiver is off; `fix_age_s` is null before the first fix of the boot.
inline void writeGnss(JsonWriter& w, const GnssHealth& g) {
  w.key("gnss"); w.beginObject();
  w.key("powered"); w.boolean(g.powered);
  w.key("fix"); if (g.fix < 0) w.null(); else w.boolean(g.fix != 0);
  w.key("sats"); w.unum(g.sats);
  w.key("fix_age_s"); if (g.fix_age_s < 0) w.null(); else w.unum((uint32_t)g.fix_age_s);
  w.endObject();
}

// The boot event of a runtime subscription: sent once per epoch, right after the first snapshot, so a consumer learns
// what is running without a hello. The members are the ones hello carries, plus the reset reason.
void writeBoot(JsonWriter& w, mesh::MainBoard& b, const char* role, const char* hardware);

}  // namespace Iface
