#pragma once

// Security audit log: a small ring buffer of security events, kept in RAM and persisted by AuditStore.h.
//
// Pure logic, no file system and no clock of its own, so it runs in the native unit tests.
//
// What is recorded (never a secret, never a password, never command arguments):
//   event   what happened            posture change, refused command, failed admin login, factory reset
//   caller  who asked                cable (USB/UART), BLE, network (TCP), remote admin (RF), remote CLI (RF), MQTT, or the
//                                    device itself (a heap-floor episode, helpers/HeapFloor.h)
//   what    which command class      or, for a posture change, the new posture
//   count   how many times           repeats of the same (event, caller, what) are folded into one entry
//
// Folding matters: a caller over the air can trigger a refusal at will. If every attempt took a slot, a flood
// would push the posture changes out of the ring. With folding the ring holds one entry per distinct kind, so
// an attacker needs 32 distinct kinds to evict anything, and there are fewer kinds than that.
//
// File image (fixed size, 268 bytes): "AUD1", entry count (1 byte), 3 reserved bytes, 32 entries of 8 bytes
// (timestamp u32 LE, event, caller, what, count), CRC32 of everything before it (u32 LE). A file whose
// magic, count or CRC is wrong is ignored (the log starts empty), never partially trusted.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

namespace Posture {

enum Event : uint8_t { EV_POSTURE_SET = 1, EV_REFUSED = 2, EV_LOGIN_FAILED = 3, EV_FACTORY_RESET = 4, EV_MODE_SET = 5, EV_LOW_MEM = 6, EV_UNLOCK = 7 };
enum Caller : uint8_t { CALLER_CABLE = 1, CALLER_BLE = 2, CALLER_NET = 3, CALLER_REMOTE_ADMIN = 4, CALLER_REMOTE_CLI = 5, CALLER_BUTTON = 6, CALLER_MQTT = 7, CALLER_SYSTEM = 8 };
enum What : uint8_t {
  W_NONE = 0, W_CLI = 1, W_KEY_EXPORT = 2, W_KEY_IMPORT = 3, W_SET_PIN = 4, W_FACTORY_RESET = 5, W_POSTURE = 6,
  W_AUDIT = 7, W_REMOTE_WRITE = 8, W_REMOTE_CLI = 9, W_DEFAULT_PASSWORD = 10, W_BAD_PASSWORD = 11,
  W_BLE_START = 12, W_NET_START = 13, W_CONNMODE = 14, W_MQTT_SESSION = 15, W_MGMT_FLAG = 16, W_HEAP_FLOOR = 17,
  W_APP_FRAME = 18, W_UNLOCK = 19
};

struct AuditEntry {
  uint32_t ts;
  uint8_t event, caller, what, count;
};

class AuditRing {
public:
  // enums, not static const members: they are never odr-used, so no out-of-class definition is needed on any C++ standard
  enum : int { CAPACITY = 32 };
  enum : int { FILE_SIZE = 8 + CAPACITY * 8 + 4 };
  enum : int { FLUSH_INTERVAL_MS = 10 * 60 * 1000 };   // wear limit for ordinary events

  AuditEntry e[CAPACITY];   // e[0] oldest ... e[n-1] newest
  uint8_t n = 0;
  // How many times add() has recorded something since boot (a folded repeat counts: it changes an entry), and the value
  // of that counter when each held entry was last recorded (0 for entries loaded from the file). The logs stream polls
  // these from the main loop, so nothing is pushed from whichever task records.
  uint32_t recorded = 0;
  uint32_t serial[CAPACITY];

  AuditRing() { clear(); }

  void clear() {
    memset(e, 0, sizeof(e));
    memset(serial, 0, sizeof(serial));
    n = 0;
    _dirty = _urgent = false;
    _flushed_once = false;
    _last_flush = 0;
  }

  // Record an event. A repeat of an entry already in the ring is folded into it (count + 1, new timestamp)
  // and moved to the newest position. Posture changes, connectivity-mode changes and factory resets are flushed at once, the rest
  // at most every FLUSH_INTERVAL_MS (the first one after a boot at once).
  void add(uint32_t ts, uint8_t event, uint8_t caller, uint8_t what) {
    int at = find(event, caller, what);
    AuditEntry rec = {ts, event, caller, what, 1};
    if (at >= 0) {
      rec.count = e[at].count == 255 ? 255 : e[at].count + 1;
      for (int i = at; i < n - 1; i++) { e[i] = e[i + 1]; serial[i] = serial[i + 1]; }
      n--;
    }
    if (n == CAPACITY) {
      for (int i = 0; i < CAPACITY - 1; i++) { e[i] = e[i + 1]; serial[i] = serial[i + 1]; }
      n--;
    }
    serial[n] = ++recorded;
    e[n++] = rec;
    _dirty = true;
    if (event == EV_POSTURE_SET || event == EV_FACTORY_RESET || event == EV_MODE_SET) _urgent = true;
  }

  // Like add(), but only when this kind of event is not in the ring yet. For things that can repeat on
  // every boot (a transport that is not started): a crash loop must not turn into a flash write per boot.
  void addOnce(uint32_t ts, uint8_t event, uint8_t caller, uint8_t what) {
    if (find(event, caller, what) < 0) add(ts, event, caller, what);
  }

  bool dirty() const { return _dirty; }

  // True when the log should be written now. now_ms is a free-running millisecond counter.
  bool shouldFlush(uint32_t now_ms) const {
    if (!_dirty) return false;
    return _urgent || !_flushed_once || (uint32_t)(now_ms - _last_flush) >= (uint32_t)FLUSH_INTERVAL_MS;
  }
  // A failed write keeps the log dirty but still starts the interval, so a broken file system is retried
  // every FLUSH_INTERVAL_MS and not on every loop pass.
  void markFlushed(uint32_t now_ms, bool ok = true) {
    if (ok) _dirty = false;
    _urgent = false;
    _flushed_once = true;
    _last_flush = now_ms;
  }
  // The file was erased under the log (file system format): the whole log must be written again.
  void markAllDirty() { _dirty = _urgent = true; }

  // ---- file image
  size_t serialize(uint8_t* out) const {   // out: FILE_SIZE bytes
    memset(out, 0, FILE_SIZE);
    memcpy(out, "AUD1", 4);
    out[4] = n;
    for (int i = 0; i < CAPACITY; i++) {
      uint8_t* p = out + 8 + i * 8;
      p[0] = e[i].ts; p[1] = e[i].ts >> 8; p[2] = e[i].ts >> 16; p[3] = e[i].ts >> 24;
      p[4] = e[i].event; p[5] = e[i].caller; p[6] = e[i].what; p[7] = e[i].count;
    }
    uint32_t crc = crc32(out, FILE_SIZE - 4);
    uint8_t* c = out + FILE_SIZE - 4;
    c[0] = crc; c[1] = crc >> 8; c[2] = crc >> 16; c[3] = crc >> 24;
    return FILE_SIZE;
  }

  bool deserialize(const uint8_t* in, size_t len) {
    if (len != FILE_SIZE || memcmp(in, "AUD1", 4) != 0 || in[4] > CAPACITY) return false;
    const uint8_t* c = in + FILE_SIZE - 4;
    uint32_t want = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
    if (crc32(in, FILE_SIZE - 4) != want) return false;
    for (int i = 0; i < CAPACITY; i++) {
      const uint8_t* p = in + 8 + i * 8;
      e[i].ts = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      e[i].event = p[4]; e[i].caller = p[5]; e[i].what = p[6]; e[i].count = p[7];
    }
    memset(serial, 0, sizeof(serial));
    n = in[4];
    _dirty = _urgent = false;
    return true;
  }

  static uint32_t crc32(const uint8_t* p, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
      crc ^= *p++;
      for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  }

  // ---- text
  static const char* eventName(uint8_t v) {
    switch (v) {
      case EV_POSTURE_SET: return "posture";
      case EV_REFUSED: return "refused";
      case EV_LOGIN_FAILED: return "login-fail";
      case EV_FACTORY_RESET: return "reset";
      case EV_MODE_SET: return "mode";
      case EV_LOW_MEM: return "low-mem";
      case EV_UNLOCK: return "unlock";
    }
    return "?";
  }
  static const char* callerName(uint8_t v) {
    switch (v) {
      case CALLER_CABLE: return "cable";
      case CALLER_BLE: return "ble";
      case CALLER_NET: return "net";
      case CALLER_REMOTE_ADMIN: return "remote-admin";
      case CALLER_REMOTE_CLI: return "remote-cli";
      case CALLER_BUTTON: return "button";
      case CALLER_MQTT: return "mqtt";
      case CALLER_SYSTEM: return "system";
    }
    return "?";
  }
  static const char* whatName(uint8_t event, uint8_t v) {
    if (event == EV_POSTURE_SET) return v == 0 ? "normal" : "hardened";
    if (event == EV_MODE_SET) return v == 0 ? "ble" : "wifi";
    switch (v) {
      case W_NONE: return "-";
      case W_CLI: return "cli";
      case W_KEY_EXPORT: return "key-export";
      case W_KEY_IMPORT: return "key-import";
      case W_SET_PIN: return "set-pin";
      case W_FACTORY_RESET: return "factory-reset";
      case W_POSTURE: return "posture";
      case W_AUDIT: return "audit";
      case W_REMOTE_WRITE: return "remote-write";
      case W_REMOTE_CLI: return "remote-cli";
      case W_DEFAULT_PASSWORD: return "default-password";
      case W_BAD_PASSWORD: return "bad-password";
      case W_BLE_START: return "ble-start";
      case W_NET_START: return "net-start";
      case W_CONNMODE: return "connectivity-mode";
      case W_MQTT_SESSION: return "mqtt-session";
      case W_MGMT_FLAG: return "mgmt-flag";
      case W_HEAP_FLOOR: return "heap-floor";
      case W_APP_FRAME: return "app-frame";
      case W_UNLOCK: return "unlock";
    }
    return "?";
  }

  // "get audit [start]": 2 entries per reply, newest first; start counts from the newest (0).
  // Reply form: "> <total> entries" on the first page, then "<ts> <event> <caller> <what> x<count>" lines,
  // then "... next:<start>" when more follow.
  void formatPage(unsigned start, char* reply, size_t max) const {
    size_t used = 0;
    if (start == 0) used += snprintf(reply, max, "> %u entries", (unsigned)n);
    else reply[0] = 0;
    unsigned i = start;
    for (; i < n && i < start + 2; i++) {
      const AuditEntry& a = e[n - 1 - i];
      if (used >= max) break;
      used += snprintf(reply + used, max - used, "%s%lu %s %s %s x%u", used ? "\n" : "", (unsigned long)a.ts,
                       eventName(a.event), callerName(a.caller), whatName(a.event, a.what), (unsigned)a.count);
    }
    if (i < n && used < max) snprintf(reply + used, max - used, "\n... next:%u", i);
    if (max) reply[max - 1] = 0;
  }

private:
  bool _dirty, _urgent, _flushed_once;
  uint32_t _last_flush;

  int find(uint8_t event, uint8_t caller, uint8_t what) const {
    for (int i = 0; i < n; i++) {
      if (e[i].event == event && e[i].caller == caller && e[i].what == what) return i;
    }
    return -1;
  }
};

}  // namespace Posture
