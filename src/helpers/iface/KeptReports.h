#pragma once
// Compact reports kept for later (docs/contract/TELEMETRY.md, "Reports kept for later"): the last RING reports a keeping
// standing instruction sent, as their exact signed bytes, in flash, until a manager collects them with report.list and
// releases them with report.ack. Pure logic over a small file interface, for the native tests.
//
// Layout. The cursor of a report is a counter that only grows (the first is 1). Cursors are cut into segments of SEG, and
// each segment is a file of its own (slot = segment % NSEG) holding that segment's records one after the other:
//   cursor u32 | owner u32 | length u8 | the report | CRC-16 of the bytes before it
// A report is one append to one file. A file is never rewritten to make room: when a new segment starts, the segment NSEG
// before it (wholly out of the window of the last RING) is removed. So a report costs one append, a segment (SEG reports) one
// file creation and one removal, and nothing is copied. At most NSEG segments, about 11 KB, are held, and a listing shows the
// last RING. A cut power leaves a record with a bad CRC at the end of one file; the valid records before it are kept and the
// file is rewritten with only those (a file of at most SEG records), at the next boot or the next report.
//
// One small meta file, replaced atomically, holds what a release must make durable: the next cursor, the acknowledged mark,
// and the point `dropped` counts from. It is written at a release, never for a report.
#include <stddef.h>
#include <stdint.h>
#include "CompactReport.h"

namespace Iface {

// The files. Slots 0..NSEG-1 are segments, META is the meta file.
class KeptStore {
public:
  static const int NSEG = 5;
  static const int META = NSEG;
  virtual ~KeptStore() {}
  virtual size_t size(int slot) = 0;                                      // 0: no such file
  virtual bool read(int slot, size_t off, uint8_t* buf, size_t n) = 0;
  virtual bool append(int slot, const uint8_t* buf, size_t n) = 0;        // creates the file; one write
  virtual bool replace(int slot, const uint8_t* buf, size_t n) = 0;       // atomic, whole file
  virtual void remove(int slot) = 0;
  virtual uint32_t freeBytes() = 0;                                       // 0xFFFFFFFF: this platform cannot say
};

struct KeptEntry {
  uint32_t cursor;
  uint32_t owner;   // who may release it: a hash of the signing member of the instruction that made it
  uint8_t len;
  uint8_t bytes[REPORT_MAX];
};

// How many reports share a write: 8 on the nRF52 (its file system), 1 elsewhere.
inline int keptBatchFor(bool nrf52) { return nrf52 ? 8 : 1; }

class KeptReports {
public:
  static const uint32_t RING = 64;
  static const uint32_t SEG = 16;
  static const int NSEG = KeptStore::NSEG;
  static const size_t RECORD_OVERHEAD = 4 + 4 + 1 + 2;
  static const uint32_t RESERVE = 6144;   // a new segment is not started with less than this free: the prefs and the contacts need it

  // batch: how many reports wait in RAM for one write. 1 writes each at once (ESP32). The nRF52 InternalFS costs about 5 page
  // erases per save, mostly on one directory page rated for 10,000, so there up to KEPT_BATCH reports go in one write, or after
  // HOLD_MS, or when a list, an ack or a shutdown asks. A power cut loses what still waits.
  ~KeptReports();
  void begin(KeptStore* s, int batch = 1);
  static void flushAll();   // the shutdown path: before a reboot or power-off
  void flush();
  void tick(uint32_t now_ms);   // flushes what has waited HOLD_MS
  uint32_t pending() const { return _pend_n; }
  static const int MAX_BATCH = 8;
  static const uint32_t HOLD_MS = 3600UL * 1000;
  bool available() const { return _s != nullptr; }

  // Keeps one report. False when it could not be written (counted in `dropped`). A report that is not a compact report is refused.
  bool add(const uint8_t* bytes, size_t n, uint32_t owner, uint32_t now_ms = 0);

  uint32_t last();       // the newest cursor, 0 before the first
  uint32_t mark();       // released up to here
  uint32_t dropped();    // pushed out of the ring, or not written, since the last release
  uint32_t held();       // reports a listing can show now

  // The held reports with a cursor above `after`, oldest first, until `f` returns false.
  typedef bool (*Visit)(void* user, const KeptEntry& e);
  void each(uint32_t after, Visit f, void* user);

  // Releases everything up to `upto`. False: not possible (a cursor not yet given, or the meta file did not write).
  bool release(uint32_t upto, uint32_t* released);
  void clear();   // everything goes, and the cursors go on from where they were

  // Flash operations since boot, for the tests and the PR's wear figure.
  uint32_t appends() const { return _appends; }
  uint32_t replaces() const { return _replaces; }
  uint32_t removes() const { return _removes; }

  static uint16_t crc16(const uint8_t* p, size_t n);
  static uint32_t ownerOf(const char* issuer);

private:
  KeptStore* _s = nullptr;
  bool _loaded = false;
  uint32_t _next = 1, _mark = 0, _base = 0, _unkept = 0;
  uint32_t _have[NSEG] = {0};   // segment id + 1 of the file in a slot, 0: none
  bool _torn = false;
  int _batch = 1;
  uint8_t* _pend = nullptr;      // records waiting for their write, as they go on flash
  size_t _pend_len = 0;
  int _pend_n = 0;
  uint32_t _pend_ms = 0;
  KeptReports* _next_inst = nullptr;
  static KeptReports*& head();
  bool writeGroup(uint32_t id, const uint8_t* rec, size_t len);
  int _torn_slot = 0;
  uint32_t _appends = 0, _replaces = 0, _removes = 0;

  void load();
  uint32_t floorOf() const { return _next - 1 > RING ? _next - 1 - RING : 0; }
  bool readRecord(int slot, size_t off, size_t size, KeptEntry* e, size_t* end);
  bool repair(int slot);
  bool saveMeta(uint32_t next, uint32_t mark, uint32_t base);
  void dropOld();
};

}  // namespace Iface
