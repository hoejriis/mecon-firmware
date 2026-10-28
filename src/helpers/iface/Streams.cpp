#include "Streams.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Dispatcher.h>
#include <MeshCore.h>
#include "helpers/BootTimes.h"
#include "helpers/MeconVersion.h"
#include "helpers/SensorManager.h"
#include "Lpp.h"
#include "../cli/CliPolicy.h"
#include <math.h>

namespace Iface {

// TELEMETRY.md section 2.
const StreamDef STREAMS[STREAM_COUNT] = {
  {"health", 5, 3600, 60, Cli::C_READ},
  {"radio", 5, 3600, 60, Cli::C_READ},
  {"power", 30, 3600, 300, Cli::C_READ},
  {"runtime", 60, 3600, 300, Cli::C_READ},
  {"observations", 0, 0, 0, Cli::C_READ},
  {"connectivity", 10, 3600, 60, Cli::C_READ},
  {"environment", 30, 3600, 300, Cli::C_READ},
  {"inventory", 300, 3600, 3600, Cli::C_READ},
  {"logs", 0, 0, 0, Cli::C_POSTURE},   // the audit ring: the same class as `get audit`
  {"location", 30, 3600, 300, Cli::C_PRIVATE},   // private: the broker, the device's own links, a signed request
};

int streamIndex(const char* name, size_t len) {
  for (int i = 0; i < STREAM_COUNT; i++)
    if (strlen(STREAMS[i].name) == len && memcmp(STREAMS[i].name, name, len) == 0) return i;
  return -1;
}

static const int64_t LIFE_MIN_S = 10, LIFE_MAX_S = 600, LIFE_DEFAULT_S = 300;

// The stream named in the request's args; -1 with c.error set when it is missing or not produced here.
static int streamArg(OpContext& c, uint32_t supported) {
  char name[16];
  const int t = c.req->args >= 0 ? jsonFind(*c.doc, c.req->args, "stream") : -1;
  if (!jsonGetString(*c.doc, t, name, sizeof(name), CS_NAME, 1)) return -1;
  const int s = streamIndex(name, strlen(name));
  if (s < 0 || !(supported & (1u << s))) { c.error = "unknown-stream"; return -1; }
  return s;
}

Status subStart(OpContext& c, SubSet* set, uint32_t supported, uint32_t now, int* started, StreamAllowFn allow,
                void* allow_user) {
  *started = -1;
  const int s = streamArg(c, supported);
  if (s < 0) return ST_INVALID;
  const StreamDef& def = STREAMS[s];
  if (!allow || !allow(allow_user, def.cls)) return ST_REFUSED;   // before anything else of the request is looked at
  int64_t period = def.default_s, life = LIFE_DEFAULT_S;
  const int pt = jsonFind(*c.doc, c.req->args, "period_s"), lt = jsonFind(*c.doc, c.req->args, "lifetime_s");
  if (pt >= 0 && def.events()) { c.error = "no-period"; return ST_INVALID; }   // an event stream has no period
  if (pt >= 0 && !jsonGetInt(*c.doc, pt, def.min_s, def.max_s, &period)) return ST_INVALID;
  if (lt >= 0 && !jsonGetInt(*c.doc, lt, LIFE_MIN_S, LIFE_MAX_S, &life)) return ST_INVALID;
  if (!set) { c.error = "no-transport"; return ST_FAILED; }
  Sub& sub = set->s[s];
  if (!sub.active && !def.events()) *started = s;   // a new subscriber gets the current state at once
  sub.active = true;
  sub.period_ms = (uint32_t)period * 1000;
  sub.until = now + (uint32_t)life * 1000;
  JsonWriter& w = *c.out;
  w.key("stream"); w.str(def.name);
  w.key("period_s"); if (def.events()) w.null(); else w.num(period);
  w.key("lifetime_s"); w.num(life);
  return ST_OK;
}

Status subStop(OpContext& c, SubSet* set, uint32_t supported) {
  const int s = streamArg(c, supported);
  if (s < 0) return ST_INVALID;
  if (set) set->s[s].active = false;
  c.out->key("stream"); c.out->str(STREAMS[s].name);
  c.out->key("active"); c.out->boolean(false);
  return ST_OK;
}

uint32_t StreamClock::due(SubSet* const* sets, int nsets, uint32_t now) {
  uint32_t mask = 0;
  for (int s = 0; s < STREAM_COUNT; s++) {
    uint32_t period = 0;
    for (int i = 0; i < nsets; i++) {
      if (!sets[i]) continue;
      Sub& sub = sets[i]->s[s];
      if (sub.active && (int32_t)(now - sub.until) >= 0) sub.active = false;
      if (sub.active && (!period || sub.period_ms < period)) period = sub.period_ms;
    }
    if (!period || (int32_t)(now - next[s]) < 0) continue;
    next[s] = now + period;
    mask |= 1u << s;
  }
  return mask;
}

int writeObservation(char* ev, size_t cap, StreamClock& clock, uint32_t epoch, bool ts_known, uint32_t ts, float snr,
                     float rssi, const uint8_t* raw, int len, uint32_t dropped) {
  if (len < 0 || len > 255) return -1;
  // Built on the heap: this runs in the radio's receive path, where the loop task's stack is short.
  const size_t data_cap = 2 * 255 + 128;
  char* data = static_cast<char*>(malloc(data_cap));
  if (!data) return -1;
  static const char H[] = "0123456789abcdef";
  int n = snprintf(data, data_cap, "{\"raw\":\"");
  for (int i = 0; i < len; i++) { data[n++] = H[raw[i] >> 4]; data[n++] = H[raw[i] & 15]; }
  n += snprintf(data + n, data_cap - n, "\",\"len\":%d,\"rssi_dbm\":%d,\"snr_db\":%.2f,\"dropped\":%lu}", len, (int)rssi,
                (double)snr, (unsigned long)dropped);
  int out = -1;
  if (n > 0 && (size_t)n < data_cap)
    out = writeEvent(ev, cap, STREAMS[SID_OBSERVATIONS].name, ++clock.seq[SID_OBSERVATIONS], epoch, ts_known, ts, data);
  free(data);
  return out;
}

int writeLog(char* ev, size_t cap, StreamClock& clock, uint32_t epoch, bool ts_known, uint32_t ts,
             const Posture::AuditEntry& a, uint32_t dropped) {
  char data[160];
  if (formatLogData(data, sizeof(data), a, dropped) < 0) return -1;
  return writeEvent(ev, cap, STREAMS[SID_LOGS].name, ++clock.seq[SID_LOGS], epoch, ts_known, ts, data);
}

void writeStreamCapabilities(JsonWriter& w, uint32_t supported) {
  char cap[24];
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(supported & (1u << s))) continue;
    snprintf(cap, sizeof(cap), "stream.%s", STREAMS[s].name);
    w.str(cap);
  }
}

// ---- radio

void writeRadio(JsonWriter& w, mesh::Dispatcher& d, mesh::Radio& r, const RadioCounts& counts) {
  w.key("rx"); w.beginObject();
  w.key("flood"); w.unum(d.getNumRecvFlood());
  w.key("direct"); w.unum(d.getNumRecvDirect());
  if (counts.known) { w.key("total"); w.unum(counts.rx_total); w.key("errors"); w.unum(counts.rx_errors); }
  w.endObject();
  w.key("tx"); w.beginObject();
  w.key("flood"); w.unum(d.getNumSentFlood());
  w.key("direct"); w.unum(d.getNumSentDirect());
  if (counts.known) { w.key("total"); w.unum(counts.tx_total); }
  w.endObject();
  const uint16_t err = d.getErrFlags();
  w.key("queue_full"); w.boolean((err & ERR_EVENT_FULL) != 0);
  w.key("cad_timeout"); w.boolean((err & ERR_EVENT_CAD_TIMEOUT) != 0);
  w.key("startrx_timeout"); w.boolean((err & ERR_EVENT_STARTRX_TIMEOUT) != 0);
  // A noise floor of 0 dBm is not a reading: the driver reports 0 until it has calibrated.
  const int nf = r.getNoiseFloor();
  w.key("noise_floor_dbm"); if (nf != 0) w.num(nf); else w.null();
  w.key("last"); w.beginObject();
  const bool heard = d.getNumRecvFlood() + d.getNumRecvDirect() > 0 || (counts.known && counts.rx_total > 0);
  w.key("rssi_dbm"); if (heard) w.num((int64_t)r.getLastRSSI()); else w.null();
  w.key("snr_db");
  if (heard) {
    char snr[16];
    snprintf(snr, sizeof(snr), "%.2f", (double)r.getLastSNR());
    w.raw(snr);
  } else {
    w.null();
  }
  w.endObject();
  w.key("airtime"); w.beginObject();
  w.key("tx_ms"); w.unum(d.getTotalAirTime());
  w.key("rx_ms"); w.unum(d.getReceiveAirTime());
  w.endObject();
  w.key("duty"); w.beginObject();
  w.key("limit_bp"); w.unum(d.getDutyLimitBp());
  w.key("limit_ms"); w.unum(d.getDutyLimitMs());
  w.key("used_ms"); w.unum(d.getTrailingAirtimeMs());
  w.endObject();
}

// ---- power

void writePower(JsonWriter& w, mesh::MainBoard& b) {
  const uint16_t mv = b.getBattMilliVolts();
  w.key("battery_mv"); if (mv) w.unum(mv); else w.null();
  // The defaults answer false whether or not the board can tell; only an initialised power manager can.
  const bool can_tell = b.isPwrMgtInitialised();
  w.key("external"); if (can_tell) w.boolean(b.isExternalPowered()); else w.null();
  w.key("charging"); if (can_tell) w.boolean(b.isChargerActive()); else w.null();
  if (b.hasUsbPowerDetect()) { w.key("usb_power"); w.boolean(b.isUsbPowered()); }
  if (b.hasSolarChargerDetect()) { w.key("solar"); w.boolean(b.isSolarChargerActive()); }
  const uint16_t boot_mv = b.getBootVoltage();
  if (boot_mv) { w.key("boot_mv"); w.unum(boot_mv); }
}

// ---- environment

static const size_t LPP_CAP = 160;

int writeEnvironment(JsonWriter& w, SensorManager& s, mesh::MainBoard& b) {
  CayenneLPP lpp(LPP_CAP);
  s.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  w.key("readings"); w.beginArray();
  int n = decodeLpp(lpp.getBuffer(), lpp.getSize(), w);
  w.endArray();
  if (n < 0) n = 0;   // what decoded before a malformed entry stays; the rest is not guessed at
  const float t = b.getMCUTemperature();
  if (!isnan(t)) {
    char num[16];
    snprintf(num, sizeof(num), "%.1f", (double)t);
    w.key("mcu_temp_c"); w.raw(num);
    n++;
  }
  return n;
}

int countEnvironment(SensorManager& s, mesh::MainBoard& b) {
  char scratch[512];
  JsonWriter w(scratch, sizeof(scratch));
  w.beginObject();
  return writeEnvironment(w, s, b);
}

// ---- location

bool readLocation(LocationProvider* p, LocationFix& out) {
  if (!p) return false;
  out = LocationFix();
  out.fix = p->isValid();
  const long sats = p->satellitesCount();
  out.sats = (uint8_t)(sats < 0 ? 0 : sats > 255 ? 255 : sats);
  if (out.fix) {
    out.lat_e6 = (int32_t)p->getLatitude();   // millionths of a degree, as MicroNMEA keeps them
    out.lon_e6 = (int32_t)p->getLongitude();
    out.alt_m = (int32_t)(p->getAltitude() / 1000);   // MicroNMEA: thousandths of a metre
  }
  uint32_t ms = 0;
  if (p->fixAgeMs(ms)) out.age_s = (int32_t)(ms / 1000);
  return true;
}

// ---- inventory

void writeCount(JsonWriter& w, const char* key, int count, int capacity) {
  w.key(key); w.beginObject();
  w.key("count"); w.num(count);
  w.key("capacity"); w.num(capacity);
  w.endObject();
}

// ---- runtime

static bool containsNoCase(const char* s, const char* word) {
  const size_t n = strlen(word);
  for (; *s; s++) {
    size_t i = 0;
    while (i < n && s[i] && tolower((unsigned char)s[i]) == word[i]) i++;
    if (i == n) return true;
  }
  return false;
}

void writeRuntime(JsonWriter& w, mesh::MainBoard& b, LoopGap& gap, int heap_low) {
  const char* why = b.getResetReasonString(b.getResetReason());
  const bool known = why && strcmp(why, "Not available") != 0;
  w.key("reset_reason"); if (known) w.str(why); else w.null();
  w.key("abnormal_reset");
  if (!known) w.null();
  else w.boolean(containsNoCase(why, "panic") || containsNoCase(why, "watchdog") || containsNoCase(why, "brownout") ||
                 containsNoCase(why, "lockup"));
  w.key("boot_ms"); w.beginObject();
  static const char* const STAGE[] = {"store", "mesh", "setup"};
  for (int s = 0; s < mecon::boot::N_STAGES; s++) {
    w.key(STAGE[s]);
    const mecon::boot::Stage st = (mecon::boot::Stage)s;
    if (mecon::boot::isMarked(st)) w.unum(mecon::boot::at(st)); else w.null();
  }
  w.endObject();
  w.key("loop_gap_ms"); w.beginObject();
  w.key("max"); w.unum(gap.take());
  w.endObject();
  if (heap_low >= 0) { w.key("heap_low"); w.boolean(heap_low != 0); }
}

void writeBoot(JsonWriter& w, mesh::MainBoard& b, const char* role, const char* hardware) {
  w.key("boot"); w.boolean(true);
  w.key("firmware"); w.str(mecon::versionFull());
  w.key("upstream"); w.str(MECON_UPSTREAM_BASE);
  w.key("hardware"); w.str(hardware);
  w.key("role"); w.str(role);
  const char* why = b.getResetReasonString(b.getResetReason());
  w.key("reset_reason");
  if (why && strcmp(why, "Not available") != 0) w.str(why); else w.null();
}

}  // namespace Iface
