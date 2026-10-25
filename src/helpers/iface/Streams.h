#pragma once
// Telemetry streams (docs/contract/TELEMETRY.md): the stream table, per-transport subscriptions, sub.start / sub.stop,
// and the writers for the families every role can produce. The role glue (MeconIface, ConsoleIface) owns its
// transports and says which streams its build can produce; this file owns their meaning, so a stream reads the same on
// every role and transport.
#include <stdint.h>
#include "IfaceDispatch.h"
#include "IfaceJson.h"

namespace mesh { class Dispatcher; class Radio; class MainBoard; }
class SensorManager;

namespace Iface {

enum StreamId : uint8_t { SID_HEALTH = 0, SID_RADIO, SID_POWER, SID_RUNTIME, SID_OBSERVATIONS, SID_CONNECTIVITY, SID_ENVIRONMENT, SID_INVENTORY, STREAM_COUNT };

struct StreamDef {
  const char* name;
  uint16_t min_s, max_s, default_s;   // period bounds and default, seconds; all 0 for an event stream (no period)
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
Status subStart(OpContext& c, SubSet* set, uint32_t supported, uint32_t now, int* started);
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

// The observations stream: at most OBS_RATE events a second (a burst of OBS_RATE), then dropped and counted; the
// next event that goes out says how many were dropped before it.
const uint32_t OBS_RATE = 10;
struct ObsLimiter {
  uint32_t tokens = OBS_RATE;
  uint32_t last_ms = 0;
  uint32_t dropped = 0;
  bool take(uint32_t now) {
    const uint32_t refill = (now - last_ms) * OBS_RATE / 1000;
    if (refill) {
      tokens = tokens + refill > OBS_RATE ? OBS_RATE : tokens + refill;
      last_ms = now;
    }
    if (!tokens) { dropped++; return false; }
    tokens--;
    return true;
  }
};
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
void writeRuntime(JsonWriter& w, mesh::MainBoard& b, LoopGap& gap);

}  // namespace Iface
