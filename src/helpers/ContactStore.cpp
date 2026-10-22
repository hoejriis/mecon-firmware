#include "ContactStore.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef ADV_TYPE_NONE
  #define ADV_TYPE_NONE 0
#endif
#define REC CONTACT_RECORD_SIZE
#define BUCKET_BYTES (CONTACT_RECORD_BUCKET * CONTACT_RECORD_SIZE)

#ifdef CONTACT_STORE_NATIVE_TEST
// The native unit-test target does not link Identity.cpp (it needs the signature library); the store only needs the
// default constructor, because it holds ContactInfo values.
namespace mesh { Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); } }
#endif

#ifdef CONTACTS_STOCK_ARRAY
  // The stock fallback: the table is a static array of MAX_CONTACTS records, no heap, no filesystem.
  #ifndef MAX_CONTACTS
    #define MAX_CONTACTS 32
  #endif
  static ContactStore::Slot s_slots[MAX_CONTACTS];
  static ContactStore::Idx  s_idx[MAX_CONTACTS];
#endif

// ---------------------------------------------------------------------------------------------- record image

uint32_t ContactStore::crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Bytes 0..151 are the stock /contacts3 record, byte for byte; then the last message time and a CRC32 of all that.
void ContactStore::encode(const ContactInfo& c, uint32_t last_msg, uint8_t out[REC]) {
  memset(out, 0, REC);
  memcpy(out + 0, c.id.pub_key, 32);
  memcpy(out + 32, c.name, 32);
  out[64] = c.type;
  out[65] = c.flags;
  out[66] = 0;                                   // stock's unused byte
  memcpy(out + 67, &c.sync_since, 4);
  out[71] = c.out_path_len;
  memcpy(out + 72, &c.last_advert_timestamp, 4);
  memcpy(out + 76, c.out_path, 64);
  memcpy(out + 140, &c.lastmod, 4);
  memcpy(out + 144, &c.gps_lat, 4);
  memcpy(out + 148, &c.gps_lon, 4);
  memcpy(out + 152, &last_msg, 4);
  uint32_t crc = crc32(out, 156);
  memcpy(out + 156, &crc, 4);
}

bool ContactStore::decode(const uint8_t in[REC], ContactInfo& c, uint32_t& last_msg) {
  bool zero = true;
  for (int i = 0; i < REC; i++) if (in[i]) { zero = false; break; }
  if (zero) return false;
  uint32_t want;
  memcpy(&want, in + 156, 4);
  if (crc32(in, 156) != want) return false;
  memset(&c, 0, sizeof(c));
  c.id = mesh::Identity(in);
  memcpy(c.name, in + 32, 32);
  c.type = in[64];
  c.flags = in[65];
  memcpy(&c.sync_since, in + 67, 4);
  c.out_path_len = in[71];
  memcpy(&c.last_advert_timestamp, in + 72, 4);
  memcpy(c.out_path, in + 76, 64);
  memcpy(&c.lastmod, in + 140, 4);
  memcpy(&c.gps_lat, in + 144, 4);
  memcpy(&c.gps_lon, in + 148, 4);
  memcpy(&last_msg, in + 152, 4);
  c.shared_secret_valid = false;
  return true;
}

int ContactStore::meaningClass(const Idx& e, uint32_t now) {
  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;
  if (e.last_msg || (e.flags & F_PATH)) return 1;
  return 0;
}

// ---------------------------------------------------------------------------------------------- life cycle

ContactStore::ContactStore() {
  _ready = _persistent = false;
  _be = NULL; _listener = NULL;
  _idx = NULL; _slots = NULL; _scratch = NULL; _bdirty = NULL; _bpresent = NULL; _bpresent_bits = 0;
  _scratch_bucket = -1;
  _cap = _nslots = _count = _pinned = _pin_limit = _first_free = _nbuckets = 0;
  _tick = 0;
  _evictions = _refused_full = _refused_fav = _damaged = _write_errors = _loads = _hits = 0;
  _refusal = REFUSED_NONE;
  _static_storage = false;
  _importing = false; _import_bucket = -1;
}

ContactStore::~ContactStore() { release(); }

void ContactStore::release() {
  if (!_static_storage) {
    free(_idx); free(_slots);
  }
  free(_scratch); free(_bdirty); free(_bpresent);
  _idx = NULL; _slots = NULL; _scratch = NULL; _bdirty = NULL; _bpresent = NULL; _bpresent_bits = 0;
  _static_storage = false;
}

void ContactStore::end() {
  release();
  _ready = false;
  _cap = _nslots = _count = _pinned = _pin_limit = _first_free = _nbuckets = 0;
  _scratch_bucket = -1;
  _importing = false; _import_bucket = -1;
}

bool ContactStore::begin(ContactBackend* backend, ContactStoreListener* listener, const ContactStoreConfig& cfg) {
  end();
  _cfg = cfg;
  _be = backend;
  _listener = listener;
  _persistent = backend != NULL;
  _tick = 0;
  _evictions = _refused_full = _refused_fav = _damaged = _write_errors = _loads = _hits = 0;
  _refusal = REFUSED_NONE;
  const int K = CONTACT_RECORD_BUCKET;
  int max_bucket = -1;

  if (!_persistent) {
    _cap = cfg.store_ceiling;
    _nslots = _cap;                                     // every record is resident
    _nbuckets = 0;
  } else {
    int limit = (cfg.store_ceiling + K - 1) / K;
    int found = 0;
    // One listing of the file system finds every bucket (and recovers the ones with a swap file). Probing each
    // possible bucket costs a lookup apiece, and a flat SPIFFS scans its whole file table for each: ~87 s at boot.
    _bpresent = (uint8_t*)calloc((limit + 7) / 8 + 1, 1);
    if (_bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {
      _bpresent_bits = limit;
      for (int b = 0; b < limit; b++) {
        if (_bpresent[b >> 3] & (1u << (b & 7))) { max_bucket = b; found++; }
      }
    } else {
      free(_bpresent); _bpresent = NULL;
      for (int b = 0; b < limit; b++) {
        if (_be->bucketExists((uint16_t)b)) { max_bucket = b; found++; }
      }
    }
    uint64_t avail = _be->freeBytes();
    if (avail != 0xFFFFFFFFull) avail += (uint64_t)found * BUCKET_BYTES;   // our own files are free space too
    uint64_t cap_fs = 0xFFFFFFull;
    if (avail != 0xFFFFFFFFull) {
      avail = avail > cfg.fs_reserve ? avail - cfg.fs_reserve : 0;
      cap_fs = avail / (cfg.fs_bytes_per_record ? cfg.fs_bytes_per_record : 1);
    }
    uint32_t budget = 0;
    if (cfg.free_heap > cfg.heap_margin) budget = (cfg.free_heap - cfg.heap_margin) / 100 * cfg.heap_percent;
    uint32_t cap_heap = budget / 2 / sizeof(Idx);
    uint32_t cap = cfg.store_ceiling;
    if (cap > cap_fs) cap = (uint32_t)cap_fs;
    uint32_t heap_cap = cap_heap > cfg.store_floor ? cap_heap : cfg.store_floor;
    if (cap > heap_cap) cap = heap_cap;
    if (cap < (uint32_t)(max_bucket + 1) * K) cap = (uint32_t)(max_bucket + 1) * K;   // never smaller than what is stored
    cap = (cap + K - 1) / K * K;
    if (cap < (uint32_t)K) cap = K;
    if (cap > 65000) cap = 65000 / K * K;
    _cap = (int)cap;
    uint32_t idx_bytes = (uint32_t)_cap * sizeof(Idx);
    uint32_t cache_budget = budget > idx_bytes ? budget - idx_bytes : 0;
    uint32_t slots = cache_budget / sizeof(Slot);
    if (cfg.free_heap == 0) slots = cfg.cache_floor;
    if (slots < cfg.cache_floor) slots = cfg.cache_floor;
    if (slots > cfg.cache_ceiling) slots = cfg.cache_ceiling;
    if (slots > (uint32_t)_cap) slots = (uint32_t)_cap;
    _nslots = (int)slots;
    _nbuckets = _cap / K;
  }

#ifdef CONTACTS_STOCK_ARRAY
  if (!_persistent) {
    if (_cap > MAX_CONTACTS) _cap = MAX_CONTACTS;
    _nslots = _cap;
    _idx = s_idx; _slots = s_slots;
    memset(s_idx, 0, sizeof(s_idx)); memset(s_slots, 0, sizeof(s_slots));
    _static_storage = true;
  }
#endif
  if (!_static_storage) {
    for (;;) {
      _idx = (Idx*)calloc(_cap > 0 ? _cap : 1, sizeof(Idx));
      _slots = (Slot*)calloc(_nslots > 0 ? _nslots : 1, sizeof(Slot));
      bool ok = _idx && _slots;
      if (_persistent) {
        _scratch = (uint8_t*)malloc(BUCKET_BYTES);
        _bdirty = (uint8_t*)calloc((_nbuckets + 7) / 8 + 1, 1);
        ok = ok && _scratch && _bdirty;
      }
      if (ok) break;
      release();
      if (_nslots <= 4) { end(); return false; }       // not even a minimal cache fits
      _nslots = _nslots / 2 < 4 ? 4 : _nslots / 2;
      if (!_persistent) _cap = _nslots;                // RAM-only: capacity follows what fits
    }
  }
  for (int i = 0; i < _nslots; i++) _slots[i].rec = -1;
  for (int i = 0; i < _cap; i++) _idx[i].slot = NOSLOT;
  _pin_limit = _nslots > CONTACT_WORKING_SET ? _nslots - CONTACT_WORKING_SET : 0;
  _pinned = 0;
  _count = 0;
  _first_free = 0;
  _scratch_bucket = -1;
  _ready = true;

  if (_persistent && max_bucket >= 0) loadFromBackend();
  // _bpresent stays: writeB()/removeB() keep it exact, and flushBucket() uses it to skip reading a bucket that does not exist
  if (_persistent) {                                   // favourites are resident from the start
    for (int r = 0; r < _cap && _pinned < _pin_limit; r++) {
      if ((_idx[r].flags & F_USED) && (_idx[r].flags & F_FAV)) get(r);
    }
  }
  return true;
}

bool ContactStore::loadFromBackend() {
  for (int b = 0; b < _nbuckets; b++) {
    bool have = _bpresent ? (b < _bpresent_bits && (_bpresent[b >> 3] & (1u << (b & 7)))) : _be->bucketExists((uint16_t)b);
    if (!have) continue;
    if (!_be->readBucket((uint16_t)b, _scratch, BUCKET_BYTES)) { _damaged += CONTACT_RECORD_BUCKET; continue; }
    for (int i = 0; i < CONTACT_RECORD_BUCKET; i++) {
      const uint8_t* r = _scratch + i * REC;
      bool zero = true;
      for (int k = 0; k < REC; k++) if (r[k]) { zero = false; break; }
      if (zero) continue;
      ContactInfo c;
      uint32_t lm;
      if (!decode(r, c, lm)) { _damaged++; continue; }
      Idx& e = _idx[b * CONTACT_RECORD_BUCKET + i];
      setIdxFromContact(e, c, lm);
      e.slot = NOSLOT;
      _count++;
    }
  }
  return true;
}

void ContactStore::setIdxFromContact(Idx& e, const ContactInfo& c, uint32_t last_msg) {
  memcpy(e.key, c.id.pub_key, 4);
  e.lastmod = c.lastmod;
  e.last_msg = last_msg;
  e.flags = (uint8_t)(F_USED | ((c.flags & 0x01) ? F_FAV : 0) | (c.out_path_len != OUT_PATH_UNKNOWN ? F_PATH : 0));
}

// ---------------------------------------------------------------------------------------------- cache slots

void ContactStore::bind(int slot, int rec) {
  Slot& s = _slots[slot];
  s.rec = (int16_t)rec;
  s.pinned = 0;
  s.prov = 0;
  s.last_use = ++_tick;
  _idx[rec].slot = (uint16_t)slot;
}

void ContactStore::unbind(int slot) {
  Slot& s = _slots[slot];
  if (s.rec >= 0) _idx[s.rec].slot = NOSLOT;
  if (s.pinned) _pinned--;
  s.rec = -1;
  s.pinned = 0;
  s.prov = 0;
}

void ContactStore::touch(int slot) {
  Slot& s = _slots[slot];
  s.last_use = ++_tick;
  bool fav = (s.ci.flags & 0x01) != 0;
  if (fav && !s.pinned && _pinned < _pin_limit) { s.pinned = 1; _pinned++; }
  else if (!fav && s.pinned) { s.pinned = 0; _pinned--; }
}

// A favourite flag can be set through a returned pointer at any time: pin or unpin the slots to match before a
// slot is chosen for replacement.
void ContactStore::refreshPins() {
  for (int i = 0; i < _nslots; i++) {
    Slot& s = _slots[i];
    if (s.rec < 0) continue;
    bool fav = (s.ci.flags & 0x01) != 0;
    if (s.pinned && !fav) { s.pinned = 0; _pinned--; }
  }
  for (int i = 0; i < _nslots && _pinned < _pin_limit; i++) {
    Slot& s = _slots[i];
    if (s.rec >= 0 && !s.pinned && (s.ci.flags & 0x01)) { s.pinned = 1; _pinned++; }
  }
}

void ContactStore::syncIndex(int slot) {
  Slot& s = _slots[slot];
  if (s.rec < 0) return;
  Idx& e = _idx[s.rec];
  memcpy(e.key, s.ci.id.pub_key, 4);
  e.lastmod = s.ci.lastmod;
  e.flags = (uint8_t)((e.flags & (F_USED | F_PROV)) | ((s.ci.flags & 0x01) ? F_FAV : 0) |
                      (s.ci.out_path_len != OUT_PATH_UNKNOWN ? F_PATH : 0));
}

void ContactStore::syncAll() {
  for (int i = 0; i < _nslots; i++) if (_slots[i].rec >= 0) syncIndex(i);
}

void ContactStore::imageOf(const Slot& s, uint8_t* out) {
  encode(s.ci, _idx[s.rec].last_msg, out);
}

bool ContactStore::slotDirty(const Slot& s) {
  if (s.rec < 0 || !_persistent) return false;
  if (s.prov) return true;
  uint8_t img[REC];
  imageOf(s, img);
  uint32_t crc;
  memcpy(&crc, img + 156, 4);
  return crc != s.saved_crc;
}

int ContactStore::acquireSlot() {
  for (int i = 0; i < _nslots; i++) if (_slots[i].rec < 0) return i;
  if (!_persistent) return -1;                           // RAM-only: nothing is ever dropped
  refreshPins();
  int victim = -1;
  uint32_t best = 0xFFFFFFFFu;
  for (int i = 0; i < _nslots; i++) {
    if (_slots[i].pinned) continue;
    if (victim < 0 || _slots[i].last_use < best) { victim = i; best = _slots[i].last_use; }
  }
  if (victim < 0) return -1;
  syncIndex(victim);
  if (slotDirty(_slots[victim])) {
    if (!flushBucket(bucketOf(_slots[victim].rec))) return -1;   // cannot let a changed record go
  }
  unbind(victim);
  return victim;
}

// ---------------------------------------------------------------------------------------------- the filesystem side

bool ContactStore::loadScratch(int b) {
  if (_scratch_bucket == b) return true;
  _scratch_bucket = -1;
  if (!_be->readBucket((uint16_t)b, _scratch, BUCKET_BYTES)) return false;
  _scratch_bucket = b;
  return true;
}

bool ContactStore::readRecord(int rec, ContactInfo& out, uint32_t& last_msg) {
  if (!_persistent) return false;
  int b = bucketOf(rec);
  if (!loadScratch(b)) return false;
  return decode(_scratch + (rec % CONTACT_RECORD_BUCKET) * REC, out, last_msg);
}

// Rewrites one bucket: the resident records are taken from RAM, the others stay as they are in the file, free
// records are zeroed. An empty bucket is removed. The write itself is the backend's atomic swap.
bool ContactStore::writeB(int b) {
  if (!_be->writeBucket((uint16_t)b, _scratch, BUCKET_BYTES, knownAbsent(b))) return false;
  if (_bpresent && b < _bpresent_bits) _bpresent[b >> 3] |= (uint8_t)(1u << (b & 7));
  return true;
}

bool ContactStore::removeB(int b) {
  if (!_be->removeBucket((uint16_t)b)) return false;
  if (_bpresent && b < _bpresent_bits) _bpresent[b >> 3] &= (uint8_t)~(1u << (b & 7));
  return true;
}

bool ContactStore::flushBucket(int b) {
  const int K = CONTACT_RECORD_BUCKET;
  bool have = !knownAbsent(b) && _be->readBucket((uint16_t)b, _scratch, BUCKET_BYTES);
  _scratch_bucket = -1;
  if (!have) memset(_scratch, 0, BUCKET_BYTES);
  bool any = false;
  for (int i = 0; i < K; i++) {
    int rec = b * K + i;
    uint8_t* r = _scratch + i * REC;
    Idx& e = _idx[rec];
    if (!(e.flags & F_USED)) {
      memset(r, 0, REC);
    } else if (e.slot != NOSLOT) {
      syncIndex(e.slot);
      if (_slots[e.slot].ci.type == ADV_TYPE_NONE) memset(r, 0, REC);   // allocated, not filled in yet: never a record
      else imageOf(_slots[e.slot], r);
    }
    for (int k = 0; k < REC && !any; k++) if (r[k]) any = true;
  }
  bool ok;
  if (any) ok = writeB(b);
  else ok = have ? removeB(b) : true;
  if (!ok) { _write_errors++; return false; }
  if (any) _scratch_bucket = b;
  for (int i = 0; i < K; i++) {
    Idx& e = _idx[b * K + i];
    if ((e.flags & F_USED) && e.slot != NOSLOT && _slots[e.slot].ci.type != ADV_TYPE_NONE) {
      Slot& s = _slots[e.slot];
      memcpy(&s.saved_crc, _scratch + i * REC + 156, 4);
      s.prov = 0;
      e.flags &= (uint8_t)~F_PROV;
    }
  }
  if (_bdirty) _bdirty[b >> 3] &= (uint8_t)~(1u << (b & 7));
  return true;
}

bool ContactStore::flush() {
  if (!_ready || !_persistent) return true;
  syncAll();
  for (int i = 0; i < _nslots; i++) {
    if (_slots[i].rec >= 0 && slotDirty(_slots[i])) markBucket(bucketOf(_slots[i].rec));
  }
  bool ok = true;
  for (int b = 0; b < _nbuckets; b++) {
    if (_bdirty[b >> 3] & (1u << (b & 7))) {
      if (!flushBucket(b)) ok = false;
    }
  }
  return ok;
}

// ---------------------------------------------------------------------------------------------- lookups

ContactInfo* ContactStore::get(int rec) {
  if (!_ready || rec < 0 || rec >= _cap) return NULL;
  Idx& e = _idx[rec];
  if (!(e.flags & F_USED)) return NULL;
  if (e.slot != NOSLOT) { _hits++; touch(e.slot); return &_slots[e.slot].ci; }
  int s = acquireSlot();
  if (s < 0) return NULL;
  ContactInfo c;
  uint32_t lm = 0;
  if (!readRecord(rec, c, lm)) {                          // the file lost or damaged it since boot
    e.flags = 0; _count--; _damaged++;
    if (rec < _first_free) _first_free = rec;
    return NULL;
  }
  _loads++;
  _slots[s].ci = c;
  bind(s, rec);
  uint8_t img[REC];
  encode(c, e.last_msg, img);
  memcpy(&_slots[s].saved_crc, img + 156, 4);
  touch(s);
  return &_slots[s].ci;
}

ContactInfo* ContactStore::find(const uint8_t* pub_key, int prefix_len, int* rec_out) {
  if (!_ready || prefix_len <= 0) return NULL;
  int n = prefix_len < 4 ? prefix_len : 4;
  for (int r = 0; r < _cap; r++) {
    Idx& e = _idx[r];
    if (!(e.flags & F_USED)) continue;
    if ((e.flags & F_PROV) && e.slot != NOSLOT) syncIndex(e.slot);
    if (memcmp(e.key, pub_key, n) != 0) continue;
    ContactInfo* c = get(r);
    if (c && memcmp(c->id.pub_key, pub_key, prefix_len) == 0) {
      if (rec_out) *rec_out = r;
      return c;
    }
  }
  return NULL;
}

ContactInfo* ContactStore::findByName(const char* name_prefix) {
  if (!_ready) return NULL;
  int len = strlen(name_prefix);
  for (int r = 0; r < _cap; r++) {
    Idx& e = _idx[r];
    if (!(e.flags & F_USED)) continue;
    if (e.slot != NOSLOT) {
      if (memcmp(_slots[e.slot].ci.name, name_prefix, len) == 0) return get(r);
    } else {
      ContactInfo c;
      uint32_t lm;
      if (readRecord(r, c, lm) && memcmp(c.name, name_prefix, len) == 0) return get(r);
    }
  }
  return NULL;
}

int ContactStore::findByHash(const uint8_t* hash, int* out, int max) {
  int n = 0;
  if (!_ready) return 0;
  for (int r = 0; r < _cap && n < max; r++) {
    Idx& e = _idx[r];
    if (!(e.flags & F_USED)) continue;
    if ((e.flags & F_PROV) && e.slot != NOSLOT) syncIndex(e.slot);
    if (memcmp(e.key, hash, PATH_HASH_SIZE) == 0) out[n++] = r;
  }
  return n;
}

int ContactStore::recOfPtr(const ContactInfo* p) const {
  if (!_ready || !_slots || !p) return -1;
  const uint8_t* base = (const uint8_t*)_slots;
  const uint8_t* q = (const uint8_t*)p;
  if (q < base || q >= base + (size_t)_nslots * sizeof(Slot)) return -1;
  size_t i = (size_t)(q - base) / sizeof(Slot);
  return _slots[i].rec;
}

bool ContactStore::next(int& cursor, ContactInfo& out) {
  if (!_ready) return false;
  while (cursor < _cap) {
    int r = cursor++;
    Idx& e = _idx[r];
    if (!(e.flags & F_USED)) continue;
    if (e.slot != NOSLOT) { out = _slots[e.slot].ci; return true; }
    uint32_t lm;
    if (readRecord(r, out, lm)) return true;
  }
  return false;
}

uint32_t ContactStore::newestLastmod() {
  if (!_ready) return 0;
  syncAll();
  uint32_t latest = 0;
  for (int r = 0; r < _cap; r++) if ((_idx[r].flags & F_USED) && _idx[r].lastmod > latest) latest = _idx[r].lastmod;
  return latest;
}

int ContactStore::favourites() {
  if (!_ready) return 0;
  syncAll();
  int n = 0;
  for (int r = 0; r < _cap; r++) if ((_idx[r].flags & F_USED) && (_idx[r].flags & F_FAV)) n++;
  return n;
}

void ContactStore::invalidateSecrets() {
  for (int i = 0; i < _nslots; i++) if (_slots && _slots[i].rec >= 0) _slots[i].ci.shared_secret_valid = false;
}

// ---------------------------------------------------------------------------------------------- changes

int ContactStore::freeRecord() {
  for (int r = _first_free; r < _cap; r++) {
    if (!(_idx[r].flags & F_USED)) { _first_free = r; return r; }
  }
  _first_free = _cap;
  return -1;
}

// The record to give up when the store is full: a favourite never, otherwise the lowest score.
int ContactStore::pickVictim(uint32_t now) {
  syncAll();
  int best = -1, best_cls = 0;
  uint32_t best_t = 0;
  for (int r = 0; r < _cap; r++) {
    const Idx& e = _idx[r];
    if (!(e.flags & F_USED) || (e.flags & F_FAV)) continue;
    int cls;
    uint32_t t;
    if (_cfg.stock_policy) { cls = 0; t = e.lastmod; }
    else { cls = meaningClass(e, now); t = e.last_msg > e.lastmod ? e.last_msg : e.lastmod; }
    if (best < 0 || cls < best_cls || (cls == best_cls && t < best_t)) { best = r; best_cls = cls; best_t = t; }
  }
  return best;
}

ContactInfo* ContactStore::allocate(bool allow_evict, uint32_t now, int* rec_out) {
  if (!_ready) return NULL;
  _refusal = REFUSED_NONE;
  int rec = freeRecord();
  int s = -1;
  if (rec < 0) {
    if (!allow_evict) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }
    rec = pickVictim(now);
    if (rec < 0) { _refused_fav++; _refusal = REFUSED_ALL_FAVOURITES; return NULL; }
    ContactInfo* v = get(rec);
    if (v && _listener) {
      uint8_t key[PUB_KEY_SIZE];
      memcpy(key, v->id.pub_key, PUB_KEY_SIZE);
      _listener->onContactEvicted(key);                    // the stock overwrite event, before the record goes
    }
    _evictions++;
    if (_idx[rec].slot != NOSLOT) { s = _idx[rec].slot; unbind(s); }
    memset(&_idx[rec], 0, sizeof(Idx));
    _idx[rec].slot = NOSLOT;
    _count--;
    markBucket(bucketOf(rec));
  }
  if (s < 0) {
    s = acquireSlot();
    if (s < 0) return NULL;
  }
  memset(&_slots[s].ci, 0, sizeof(ContactInfo));
  bind(s, rec);
  _slots[s].prov = 1;
  _slots[s].saved_crc = 0;
  Idx& e = _idx[rec];
  memset(&e, 0, sizeof(Idx));
  e.slot = (uint16_t)s;
  e.flags = F_USED | F_PROV;
  _count++;
  markBucket(bucketOf(rec));
  if (rec_out) *rec_out = rec;
  return &_slots[s].ci;
}

bool ContactStore::remove(int rec) {
  if (!_ready || rec < 0 || rec >= _cap || !(_idx[rec].flags & F_USED)) return false;
  if (_idx[rec].slot != NOSLOT) unbind(_idx[rec].slot);
  memset(&_idx[rec], 0, sizeof(Idx));
  _idx[rec].slot = NOSLOT;
  _count--;
  if (rec < _first_free) _first_free = rec;
  markBucket(bucketOf(rec));
  return true;
}

void ContactStore::noteMessage(int rec, uint32_t now) {
  if (!_ready || rec < 0 || rec >= _cap || !(_idx[rec].flags & F_USED)) return;
  _idx[rec].last_msg = now;
}

// The rescue console's "rebuild": works from the persistent store, not from the cache. Every bucket is read and every
// record re-verified (CRC). Every valid record is kept, cached or not. A record whose CRC fails is dropped from the
// rewritten bucket and counted, and the bucket as it was is kept aside (backend keepAside) before it is touched; if it
// cannot be kept aside the bucket is left as it is. Each bucket is rewritten through the backend's atomic swap, so a
// power cut leaves the previous bucket or the new one. The index is rebuilt from what the buckets hold.
int ContactStore::rebuild(int* damaged_out, int* isolated_out) {
  int damaged = 0, isolated = 0;
  if (!_ready || !_persistent) {
    if (damaged_out) *damaged_out = 0;
    if (isolated_out) *isolated_out = 0;
    return _count;
  }
  syncAll();                                            // changes made through pointers are in the image below
  const int K = CONTACT_RECORD_BUCKET;
  for (int b = 0; b < _nbuckets; b++) {
    bool have = _be->bucketExists((uint16_t)b);
    bool readable = have && _be->readBucket((uint16_t)b, _scratch, BUCKET_BYTES);
    _scratch_bucket = -1;
    if (!readable) memset(_scratch, 0, BUCKET_BYTES);
    bool resident[CONTACT_RECORD_BUCKET];
    bool any_bad = false, any_res = false;
    for (int i = 0; i < K; i++) {
      Idx& e = _idx[b * K + i];
      resident[i] = (e.flags & F_USED) && e.slot != NOSLOT;
      if (resident[i]) { any_res = true; continue; }
      const uint8_t* r = _scratch + i * REC;
      ContactInfo c; uint32_t lm;
      bool zero = true;
      for (int k = 0; k < REC && zero; k++) if (r[k]) zero = false;
      if (!zero && !decode(r, c, lm)) any_bad = true;
    }
    if (have && !readable) any_bad = true;              // a file that cannot be read is damaged as a whole
    if (!have && !any_res) {                            // no file: nothing to verify, but the index may claim records
      for (int i = 0; i < K; i++) {
        Idx& e = _idx[b * K + i];
        if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; damaged++; }
      }
      continue;
    }
    if (any_bad && readable) {                          // the bucket as found is kept before anything is rewritten
      if (!_be->keepAside((uint16_t)b, _scratch, BUCKET_BYTES)) {
        _write_errors++;
        for (int i = 0; i < K; i++) {                    // left as it is: the damaged records are still counted
          const uint8_t* r = _scratch + i * REC;
          ContactInfo c; uint32_t lm;
          bool zero = true;
          for (int k = 0; k < REC && zero; k++) if (r[k]) zero = false;
          if (!resident[i] && !zero && !decode(r, c, lm)) damaged++;
        }
        continue;
      }
      isolated++;
    }
    bool any = false;
    for (int i = 0; i < K; i++) {
      int rec = b * K + i;
      uint8_t* r = _scratch + i * REC;
      Idx& e = _idx[rec];
      if (resident[i]) {
        if (_slots[e.slot].ci.type == ADV_TYPE_NONE) memset(r, 0, REC);
        else imageOf(_slots[e.slot], r);
      } else {
        ContactInfo c; uint32_t lm = 0;
        bool zero = true;
        for (int k = 0; k < REC && zero; k++) if (r[k]) zero = false;
        if (!zero && decode(r, c, lm)) {
          if (!(e.flags & F_USED)) _count++;
          setIdxFromContact(e, c, lm);
          e.slot = NOSLOT;
        } else {
          if (!zero || (e.flags & F_USED)) damaged++;
          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }
          memset(r, 0, REC);
        }
      }
      for (int k = 0; k < REC && !any; k++) if (r[k]) any = true;
    }
    bool ok;
    if (any) ok = writeB(b);
    else ok = have ? removeB(b) : true;
    if (!ok) { _write_errors++; continue; }
    if (any) _scratch_bucket = b;
    for (int i = 0; i < K; i++) {
      Idx& e = _idx[b * K + i];
      if ((e.flags & F_USED) && e.slot != NOSLOT && _slots[e.slot].ci.type != ADV_TYPE_NONE) {
        Slot& s = _slots[e.slot];
        memcpy(&s.saved_crc, _scratch + i * REC + 156, 4);
        s.prov = 0;
        e.flags &= (uint8_t)~F_PROV;
      }
    }
    if (_bdirty) _bdirty[b >> 3] &= (uint8_t)~(1u << (b & 7));
  }
  _first_free = 0;
  while (_first_free < _cap && (_idx[_first_free].flags & F_USED)) _first_free++;
  if (damaged_out) *damaged_out = damaged;
  if (isolated_out) *isolated_out = isolated;
  return _count;
}

// ---------------------------------------------------------------------------------------------- bulk import

void ContactStore::importBegin() {
  _importing = true;
  _import_bucket = -1;
}

bool ContactStore::importFlushBucket() {
  if (_import_bucket < 0) return true;
  int b = _import_bucket;
  _import_bucket = -1;
  _scratch_bucket = -1;
  if (!writeB(b)) { _write_errors++; return false; }
  _scratch_bucket = b;
  return true;
}

bool ContactStore::importAdd(const ContactInfo& c, uint32_t last_msg, uint32_t now) {
  if (!_ready) return false;
  if (!_persistent) {
    ContactInfo* d = allocate(true, now);
    if (!d) return false;
    *d = c; d->shared_secret_valid = false;
    return true;
  }
  int rec = freeRecord();
  if (rec < 0) {                                          // full: from here on the ordinary path decides who stays
    if (!importFlushBucket()) return false;
    int r2 = NONE;
    ContactInfo* d = allocate(true, now, &r2);
    if (!d) return false;
    *d = c; d->shared_secret_valid = false;
    _idx[r2].last_msg = last_msg;
    return true;
  }
  int b = bucketOf(rec);
  if (b != _import_bucket) {
    if (!importFlushBucket()) return false;
    memset(_scratch, 0, BUCKET_BYTES);
    _import_bucket = b;
    _scratch_bucket = -1;
  }
  encode(c, last_msg, _scratch + (rec % CONTACT_RECORD_BUCKET) * REC);
  setIdxFromContact(_idx[rec], c, last_msg);
  _idx[rec].slot = NOSLOT;
  _count++;
  return true;
}

bool ContactStore::importEnd() {
  bool ok = importFlushBucket();
  _importing = false;
  if (_persistent) {
    for (int r = 0; r < _cap && _pinned < _pin_limit; r++) {
      if ((_idx[r].flags & F_USED) && (_idx[r].flags & F_FAV)) get(r);
    }
  }
  return ok;
}

bool ContactStore::clearAll() {
  if (!_ready) return false;
  bool ok = true;
  if (_persistent) {
    for (int b = 0; b < _nbuckets; b++) {
      if (_be->bucketExists((uint16_t)b) && !removeB(b)) ok = false;
    }
    memset(_bdirty, 0, (_nbuckets + 7) / 8 + 1);
  }
  for (int i = 0; i < _nslots; i++) if (_slots[i].rec >= 0) unbind(i);
  for (int r = 0; r < _cap; r++) { memset(&_idx[r], 0, sizeof(Idx)); _idx[r].slot = NOSLOT; }
  _count = 0; _first_free = 0; _scratch_bucket = -1; _import_bucket = -1;
  return ok;
}

// ---------------------------------------------------------------------------------------------- reporting

ContactStoreStats ContactStore::stats() {
  ContactStoreStats s;
  s.used = (uint16_t)_count;
  s.capacity = (uint16_t)_cap;
  s.cache_slots = (uint16_t)_nslots;
  int used = 0;
  for (int i = 0; i < _nslots; i++) if (_slots && _slots[i].rec >= 0) used++;
  s.cache_used = (uint16_t)used;
  s.favourites = (uint16_t)favourites();
  s.pinned = (uint16_t)_pinned;
  s.evictions = _evictions; s.refused_full = _refused_full; s.refused_all_favourites = _refused_fav;
  s.damaged = _damaged; s.write_errors = _write_errors; s.loads = _loads; s.hits = _hits;
  s.persistent = _persistent;
  s.ram_bytes = _static_storage ? 0 : (uint32_t)_cap * sizeof(Idx) + (uint32_t)_nslots * sizeof(Slot) +
                (_persistent ? BUCKET_BYTES + (_nbuckets + 7) / 8 + 1 : 0);
  return s;
}

int ContactStore::format(char* dst, size_t n) {
  ContactStoreStats s = stats();
  return snprintf(dst, n, "> %s store %u/%u cache %u/%u pinned %u fav %u evicted %lu refused %lu+%lu loads %lu damaged %lu werr %lu ram %lu",
                  s.persistent ? "adaptive" : "ram-only", s.used, s.capacity, s.cache_used, s.cache_slots, s.pinned, s.favourites,
                  (unsigned long)s.evictions, (unsigned long)s.refused_full, (unsigned long)s.refused_all_favourites,
                  (unsigned long)s.loads, (unsigned long)s.damaged, (unsigned long)s.write_errors, (unsigned long)s.ram_bytes);
}
