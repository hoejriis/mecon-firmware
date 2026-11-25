#include "KeptReports.h"
#include <stdlib.h>
#include <string.h>

namespace Iface {

namespace {

const uint32_t META_MAGIC = 0x3150524Bu;   // "KRP1"
const size_t META_LEN = 4 + 4 + 4 + 4 + 2;

uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

}  // namespace

uint16_t KeptReports::crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

uint32_t KeptReports::ownerOf(const char* issuer) {
  uint32_t h = 2166136261u;   // FNV-1a: it tells one member from another, and proves nothing
  for (; issuer && *issuer; issuer++) { h ^= (uint8_t)*issuer; h *= 16777619u; }
  return h;
}

// One record of a segment file at `off`, valid or not.
bool KeptReports::readRecord(int slot, size_t off, size_t size, KeptEntry* e, size_t* end) {
  if (off + 9 > size) return false;
  uint8_t buf[RECORD_OVERHEAD + REPORT_MAX];   // one read, one open of the file
  size_t want = size - off < sizeof(buf) ? size - off : sizeof(buf);
  if (!_s->read(slot, off, buf, want)) return false;
  const size_t len = buf[8];
  if (len < REPORT_HEADER + REPORT_SIG || len > REPORT_MAX || RECORD_OVERHEAD + len > want) return false;
  const uint16_t got = (uint16_t)(buf[9 + len] | (buf[10 + len] << 8));
  if (crc16(buf, 9 + len) != got || buf[9] != REPORT_MARKER) return false;
  e->cursor = rd32(buf);
  e->owner = rd32(buf + 4);
  e->len = (uint8_t)len;
  memcpy(e->bytes, buf + 9, len);
  *end = off + RECORD_OVERHEAD + len;
  return true;
}

bool KeptReports::saveMeta(uint32_t next, uint32_t mark, uint32_t base) {
  uint8_t b[META_LEN];
  for (int i = 0; i < 4; i++) { b[i] = (uint8_t)(META_MAGIC >> (8 * i)); b[4 + i] = (uint8_t)(next >> (8 * i)); b[8 + i] = (uint8_t)(mark >> (8 * i)); b[12 + i] = (uint8_t)(base >> (8 * i)); }
  const uint16_t c = crc16(b, 16);
  b[16] = (uint8_t)c; b[17] = (uint8_t)(c >> 8);
  _replaces++;
  return _s->replace(KeptStore::META, b, sizeof(b));
}

// Rewrites a segment file with its valid records only, after a write that did not complete.
bool KeptReports::repair(int slot) {
  const size_t size = _s->size(slot);
  uint8_t* buf = static_cast<uint8_t*>(malloc(SEG * (RECORD_OVERHEAD + REPORT_MAX)));
  if (!buf) return false;
  size_t off = 0, n = 0;
  KeptEntry e;
  size_t end;
  uint32_t prev = 0;
  while (readRecord(slot, off, size, &e, &end) && (prev == 0 || e.cursor == prev + 1) && n + (end - off) <= SEG * (RECORD_OVERHEAD + REPORT_MAX)) {
    if (!_s->read(slot, off, buf + n, end - off)) break;
    n += end - off;
    off = end;
    prev = e.cursor;
  }
  bool ok;
  if (n == 0) { _s->remove(slot); _removes++; _have[slot] = 0; ok = true; }
  else { _replaces++; ok = _s->replace(slot, buf, n); }
  free(buf);
  return ok;
}

void KeptReports::load() {
  if (_loaded) return;
  _loaded = true;
  _next = 1; _mark = 0; _base = 0; _unkept = 0; _torn = false;
  memset(_have, 0, sizeof(_have));
  if (!_s) return;
  uint32_t meta_next = 1;
  uint8_t m[META_LEN];
  if (_s->size(KeptStore::META) == META_LEN && _s->read(KeptStore::META, 0, m, META_LEN) && rd32(m) == META_MAGIC &&
      crc16(m, 16) == (uint16_t)(m[16] | (m[17] << 8))) {
    meta_next = rd32(m + 4);
    _mark = rd32(m + 8);
    _base = rd32(m + 12);
  }
  uint32_t top = 0;
  int bad[NSEG];
  for (int slot = 0; slot < NSEG; slot++) {
    bad[slot] = 0;
    const size_t size = _s->size(slot);
    if (!size) continue;
    size_t off = 0, end;
    KeptEntry e;
    uint32_t prev = 0, first = 0;
    while (readRecord(slot, off, size, &e, &end) && (prev == 0 || e.cursor == prev + 1)) {
      if (!first) first = e.cursor;
      prev = e.cursor;
      off = end;
    }
    if (!first || (int)(((first - 1) / SEG) % NSEG) != slot) { bad[slot] = 2; continue; }   // nothing valid, or not this slot's
    if (off != size) bad[slot] = 1;
    _have[slot] = (first - 1) / SEG + 1;
    if (prev > top) top = prev;
  }
  _next = top + 1 > meta_next ? top + 1 : meta_next;
  if (_next < 1) _next = 1;
  for (int slot = 0; slot < NSEG; slot++) {
    if (bad[slot] == 2) { _s->remove(slot); _removes++; _have[slot] = 0; }
    else if (bad[slot] == 1 && !repair(slot)) _torn = true, _torn_slot = slot;
  }
  dropOld();
}

// Segments wholly released or wholly out of the window go: nothing can list them.
void KeptReports::dropOld() {
  const uint32_t lo = _mark > floorOf() ? _mark : floorOf();
  for (int slot = 0; slot < NSEG; slot++) {
    if (!_have[slot]) continue;
    if (SEG * _have[slot] <= lo) { _s->remove(slot); _removes++; _have[slot] = 0; }
  }
}

KeptReports*& KeptReports::head() { static KeptReports* h = nullptr; return h; }

void KeptReports::begin(KeptStore* s, int batch) {
  _s = s;
  _loaded = false;
  _batch = batch < 1 ? 1 : batch > MAX_BATCH ? MAX_BATCH : batch;
  if (_batch > 1 && !_pend) _pend = static_cast<uint8_t*>(malloc(MAX_BATCH * (RECORD_OVERHEAD + REPORT_MAX)));
  if (!_pend) _batch = 1;
  bool listed = false;
  for (KeptReports* k = head(); k; k = k->_next_inst) if (k == this) listed = true;
  if (!listed) { _next_inst = head(); head() = this; }
}

KeptReports::~KeptReports() {
  for (KeptReports** p = &head(); *p; p = &(*p)->_next_inst)
    if (*p == this) { *p = _next_inst; break; }
  free(_pend);
}

void KeptReports::flushAll() { for (KeptReports* k = head(); k; k = k->_next_inst) k->flush(); }

void KeptReports::tick(uint32_t now_ms) {
  if (_pend_n && (uint32_t)(now_ms - _pend_ms) >= HOLD_MS) flush();
}

// The pending records, one write for each segment they fall in. A group that does not write is lost with what follows it; the
// cursors they took go back, since nothing has been told of them, and they count in `dropped`.
void KeptReports::flush() {
  if (!_pend_n) return;
  size_t off = 0;
  int done = 0;
  while (off < _pend_len) {
    const uint32_t first = (uint32_t)_pend[off] | ((uint32_t)_pend[off + 1] << 8) | ((uint32_t)_pend[off + 2] << 16) | ((uint32_t)_pend[off + 3] << 24);
    const uint32_t id = (first - 1) / SEG;
    size_t end = off;
    int cnt = 0;
    while (end < _pend_len) {
      const uint32_t c = (uint32_t)_pend[end] | ((uint32_t)_pend[end + 1] << 8) | ((uint32_t)_pend[end + 2] << 16) | ((uint32_t)_pend[end + 3] << 24);
      if ((c - 1) / SEG != id) break;
      end += RECORD_OVERHEAD + _pend[end + 8];
      cnt++;
    }
    if (!writeGroup(id, _pend + off, end - off)) {
      _unkept += (uint32_t)(_pend_n - done);
      _next = first;
      break;
    }
    done += cnt;
    off = end;
  }
  _pend_len = 0;
  _pend_n = 0;
}

bool KeptReports::add(const uint8_t* bytes, size_t n, uint32_t owner, uint32_t now_ms) {
  load();
  if (!_s || n < REPORT_HEADER + REPORT_SIG || n > REPORT_MAX || bytes[0] != REPORT_MARKER) return false;
  uint8_t local[RECORD_OVERHEAD + REPORT_MAX];
  uint8_t* rec = _batch > 1 ? _pend + _pend_len : local;
  const uint32_t cursor = _next;
  for (int i = 0; i < 4; i++) { rec[i] = (uint8_t)(cursor >> (8 * i)); rec[4 + i] = (uint8_t)(owner >> (8 * i)); }
  rec[8] = (uint8_t)n;
  memcpy(rec + 9, bytes, n);
  const uint16_t c = crc16(rec, 9 + n);
  rec[9 + n] = (uint8_t)c; rec[10 + n] = (uint8_t)(c >> 8);
  if (_batch == 1) {
    if (!writeGroup((cursor - 1) / SEG, rec, RECORD_OVERHEAD + n)) { _unkept++; return false; }
    _next++;
    return true;
  }
  if (!_pend_n) _pend_ms = now_ms;
  _pend_len += RECORD_OVERHEAD + n;
  _pend_n++;
  _next++;
  if (_pend_n >= _batch) flush();
  return true;
}

// One append of one or more records that belong to segment `id`.
bool KeptReports::writeGroup(uint32_t id, const uint8_t* rec, size_t len) {
  if (_torn) {
    if (!repair(_torn_slot)) return false;
    _torn = false;
  }
  const int slot = (int)(id % NSEG);
  if (_have[slot] != id + 1) {
    // A new segment. What the slot held is wholly out of the window, since the window is RING and the slot's old segment is
    // NSEG back: the last cursor it held is below the window's floor.
    if (_have[slot]) { _s->remove(slot); _removes++; _have[slot] = 0; }
    if (_s->freeBytes() < RESERVE) return false;
  }
  _appends++;
  if (!_s->append(slot, rec, len)) {
    _torn = true;
    _torn_slot = slot;
    return false;
  }
  if (_have[slot] == 0) _have[slot] = id + 1;
  return true;
}

uint32_t KeptReports::last() { load(); flush(); return _next - 1; }
uint32_t KeptReports::mark() { load(); flush(); return _mark; }

uint32_t KeptReports::held() {
  load();
  flush();
  const uint32_t lo = _mark > floorOf() ? _mark : floorOf();
  return _next - 1 > lo ? _next - 1 - lo : 0;
}

uint32_t KeptReports::dropped() {
  load();
  flush();
  const uint32_t wf = floorOf();
  return (wf > _base ? wf - _base : 0) + _unkept;
}

void KeptReports::each(uint32_t after, Visit f, void* user) {
  load();
  flush();   // a list shows everything the device holds
  if (!_s || _next <= 1) return;
  uint32_t lo = after;
  if (_mark > lo) lo = _mark;
  if (floorOf() > lo) lo = floorOf();
  if (lo >= _next - 1) return;
  for (uint32_t id = lo / SEG; id <= (_next - 2) / SEG; id++) {
    const int slot = (int)(id % NSEG);
    if (_have[slot] != id + 1) continue;
    const size_t size = _s->size(slot);
    size_t off = 0, end;
    KeptEntry e;
    while (readRecord(slot, off, size, &e, &end)) {
      off = end;
      if (e.cursor <= lo) continue;
      if (!f(user, e)) return;
    }
  }
}

bool KeptReports::release(uint32_t upto, uint32_t* released) {
  load();
  flush();
  if (!_s || upto == 0 || upto > _next - 1) return false;
  const uint32_t lo = _mark > floorOf() ? _mark : floorOf();
  const uint32_t mark = upto > _mark ? upto : _mark;
  const uint32_t wf = floorOf();
  const uint32_t base = mark > wf ? mark : wf;
  if (mark == _mark && base == _base) { _unkept = 0; *released = 0; return true; }   // a repeat: nothing to make durable
  if (!saveMeta(_next, mark, base)) return false;
  *released = mark > lo ? mark - lo : 0;
  _mark = mark;
  _base = base;
  _unkept = 0;
  dropOld();
  return true;
}

void KeptReports::clear() {
  load();
  _pend_len = 0;
  _pend_n = 0;
  if (!_s) return;
  for (int slot = 0; slot < NSEG; slot++) {
    if (_s->size(slot)) { _s->remove(slot); _removes++; }
    _have[slot] = 0;
  }
  _mark = _next - 1;
  _base = _mark;
  _unkept = 0;
  _torn = false;
  saveMeta(_next, _mark, _base);
}

}  // namespace Iface
