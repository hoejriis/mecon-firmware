#include "ConsoleIface.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helpers/MeconVersion.h"
#include "helpers/MemStats.h"
#include "helpers/Posture.h"
#include "helpers/cli/CliPolicy.h"
#include "helpers/cli/CliRegistry.h"

namespace Iface {

namespace {

ConsoleIface* self(OpContext& c) { return static_cast<ConsoleIface*>(c.user); }

void hexOf(char* out, const uint8_t* b, size_t n) {
  static const char H[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = H[b[i] >> 4];
    out[2 * i + 1] = H[b[i] & 15];
  }
  out[2 * n] = 0;
}

const char* const CAPABILITIES[] = {"hello", "status.get", "config.schema", "config.get", "config.apply",
                                    "sub.start", "sub.stop"};

Status opHello(OpContext& c) {
  ConsoleIface* m = self(c);
  JsonWriter& w = *c.out;
  char key[65];
  hexOf(key, m->host().node_key, 32);
  w.key("interface"); w.str("1.0");
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("node_key"); w.str(key);
  w.key("role"); w.str(m->host().role);
  w.key("hardware"); w.str(m->host().hardware);
  w.key("firmware"); w.str(mecon::versionFull());
  w.key("upstream"); w.str(MECON_UPSTREAM_BASE);
  w.key("epoch"); w.unum(m->epoch());
  w.key("capabilities"); w.beginArray();
  for (const char* cap : CAPABILITIES) w.str(cap);
  writeStreamCapabilities(w, ConsoleIface::STREAMS_PRODUCED);
  w.endArray();
  return ST_OK;
}

// Shared by status.get and the health stream. A server has no contact list, so "contacts" is absent.
void writeHealth(JsonWriter& w, ConsoleIface* m) {
  w.key("uptime_s"); w.unum(millis() / 1000);
  const MemStats ms = memStats();
  w.key("heap");
  if (!ms.known) w.null();
  else {
    w.beginObject();
    w.key("free"); w.unum(ms.free);
    w.key("largest"); w.unum(ms.largest);
    w.key("min_free"); if (ms.min_free) w.unum(ms.min_free); else w.null();
    w.key("loop_stack_free"); if (ms.loop_stack_free) w.unum(ms.loop_stack_free); else w.null();
    if (ms.psram) { w.key("psram_free"); w.unum(ms.psram); }
    w.endObject();
  }
  const uint16_t mv = m->host().battery_mv(m->host().user);
  w.key("battery_mv"); if (mv) w.unum(mv); else w.null();
}

Status opStatus(OpContext& c) {
  ConsoleIface* m = self(c);
  JsonWriter& w = *c.out;
  w.key("device_id"); w.str(m->deviceIdHex());
  w.key("epoch"); w.unum(m->epoch());
  writeHealth(w, m);
  w.key("posture"); w.str(Posture::isHardened(m->host().posture(m->host().user)) ? "hardened" : "normal");
  return ST_OK;
}

Status opSubStart(OpContext& c) {
  int started;
  const uint32_t now = millis();
  Status st = subStart(c, &self(c)->subs(), ConsoleIface::STREAMS_PRODUCED, now, &started);
  self(c)->scheduleStream(started, now);
  return st;
}

Status opSubStop(OpContext& c) { return subStop(c, &self(c)->subs(), ConsoleIface::STREAMS_PRODUCED); }

Status withConfig(OpContext& c, Status (*op)(OpContext&)) {
  ConfigHost host = self(c)->configHost();
  OpContext cc = c;
  cc.user = &host;
  Status st = op(cc);
  c.error = cc.error;
  return st;
}
Status opSchema(OpContext& c) { return withConfig(c, opConfigSchema); }
Status opGet(OpContext& c) { return withConfig(c, opConfigGet); }
Status opApply(OpContext& c) { return withConfig(c, opConfigApply); }

const OpEntry OPS[] = {
  {"hello", opHello, PRIV_READ, false},
  {"status.get", opStatus, PRIV_READ, false},
  {"config.schema", opSchema, PRIV_READ, false},
  {"config.get", opGet, PRIV_READ, false},
  {"config.apply", opApply, PRIV_WRITE, true},
  {"sub.start", opSubStart, PRIV_READ, false},
  {"sub.stop", opSubStop, PRIV_READ, false},
};

Cli::Context consoleContext(const ConsoleIface::Host& h) {
  return Cli::Context(Cli::T_USB, Cli::A_CABLE, h.posture(h.user));
}

}  // namespace

void ConsoleIface::begin(const Host& host, const uint8_t device_id[16], uint32_t epoch) {
  _host = host;
  hexOf(_id_hex, device_id, 16);
  _epoch = epoch;
}

ConfigHost ConsoleIface::configHost() { return ConfigHost{cliRun, cliAllow, this, SERVER_KEYS, N_SERVER_KEYS}; }

bool ConsoleIface::cliRun(void* user, const char* line, char* reply, size_t cap) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  return s->_host.run(s->_host.user, line, reply, cap);
}

bool ConsoleIface::cliAllow(void* user, const char* line) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const Cli::Resolved r = Cli::resolve(line, Cli::ROLE_REPEATER);
  return Cli::decide(consoleContext(s->_host), r.cls, r.flags).allow;
}

bool ConsoleIface::authorize(void* user, const OpEntry& op, const Request&) {
  ConsoleIface* s = static_cast<ConsoleIface*>(user);
  const uint8_t cls = op.priv == PRIV_READ ? Cli::C_READ : op.priv == PRIV_WRITE ? Cli::C_WRITE : Cli::C_POSTURE;
  return Cli::decide(consoleContext(s->_host), cls).allow;
}

void ConsoleIface::startLine() {
  _collecting = true;
  _overflow = false;
  _len = 0;
  if (!_line) _line = static_cast<char*>(malloc(MAX_LINE + 1));
  if (!_line) _overflow = true;   // nothing to collect into: answered with busy at the end of the line
}

void ConsoleIface::feed(char c, Print& out) {
  if (!_collecting) return;
  if (c == '\r' || c == '\n') {
    answer(out);
    return;
  }
  if (_overflow) return;
  if (_len >= MAX_LINE) { _overflow = true; return; }
  _line[_len++] = c;
}

void ConsoleIface::printLine(Print& out, const char* msg, size_t len) {
  out.print(PREFIX);
  out.write(reinterpret_cast<const uint8_t*>(msg), len);
  out.print("\r\n");
}

void ConsoleIface::answer(Print& out) {
  _collecting = false;
  out.print("\r\n");   // ends the console's echo of the prefix, as the stock console ends an echoed line
  static const size_t OUT_MAX = 1600;
  char* res = static_cast<char*>(malloc(OUT_MAX));
  int n;
  if (!res) {
    char small[96];
    n = writeResult(small, sizeof(small), "", ST_BUSY, nullptr, "no-memory");
    if (n > 0) printLine(out, small, (size_t)n);
  } else {
    if (_overflow) {
      n = writeResult(res, OUT_MAX, "", ST_BUSY, nullptr, _line ? "line-too-long" : "no-memory");
    } else {
      if (!_cache) _cache = new (std::nothrow) ResultCache();
      if (!_cache) {
        n = writeResult(res, OUT_MAX, "", ST_BUSY, nullptr, "no-memory");
      } else {
        Dispatcher d(OPS, sizeof(OPS) / sizeof(OPS[0]), authorize, this, this);
        n = d.handle(_line, _len, res, OUT_MAX, _cache);
      }
    }
    if (n > 0) printLine(out, res, (size_t)n);
    free(res);
  }
  if (_line) {
    memset(_line, 0, _len);   // a write may have carried a secret
    free(_line);
    _line = nullptr;
  }
  _len = 0;
}

bool ConsoleIface::writeStream(int s, JsonWriter& w) {
  w.beginObject();
  switch (s) {
    case SID_HEALTH: writeHealth(w, this); break;
    case SID_RADIO: {
      RadioCounts counts;
      if (_host.radio_counts) _host.radio_counts(_host.user, &counts);
      writeRadio(w, *_host.dispatcher, *_host.radio, counts);
      break;
    }
    case SID_POWER: writePower(w, *_host.board); break;
    case SID_RUNTIME: writeRuntime(w, *_host.board); break;
    default: return false;
  }
  w.endObject();
  return w.ok();
}

void ConsoleIface::loop(uint32_t now, Print& out) {
  if (_collecting) return;   // an event never lands in the middle of a request being typed
  SubSet* sets[1] = { &_subs };
  const uint32_t due = _clock.due(sets, 1, now);
  for (int s = 0; s < STREAM_COUNT; s++) {
    if (!(due & (1u << s))) continue;
    char data[512];
    JsonWriter w(data, sizeof(data));
    if (!writeStream(s, w)) continue;
    char ev[640];
    const int n = writeEvent(ev, sizeof(ev), STREAMS[s].name, ++_clock.seq[s], _epoch, false, 0, data);
    if (n > 0) printLine(out, ev, (size_t)n);
  }
}

}  // namespace Iface
