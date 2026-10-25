#pragma once
// Telemetry streams (docs/contract/TELEMETRY.md): the stream table, per-transport subscriptions, sub.start / sub.stop,
// and the writers for the families every role can produce. The role glue (MeconIface, ConsoleIface) owns its
// transports and says which streams its build can produce; this file owns their meaning, so a stream reads the same on
// every role and transport.
#include <stdint.h>
#include "IfaceDispatch.h"
#include "IfaceJson.h"

namespace mesh { class Dispatcher; class Radio; class MainBoard; }

namespace Iface {

enum StreamId : uint8_t { SID_HEALTH = 0, SID_RADIO, SID_POWER, SID_RUNTIME, STREAM_COUNT };

struct StreamDef {
  const char* name;
  uint16_t min_s, max_s, default_s;   // period bounds and default, seconds
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

// Reset reason and boot times.
void writeRuntime(JsonWriter& w, mesh::MainBoard& b);

}  // namespace Iface
