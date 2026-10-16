#pragma once

// Adaptive contact memory: a persistent contact store plus a RAM cache of resident contacts (docs/CONTACTS.md).
//
// Stock MeshCore keeps every contact in one static array. This class keeps them on the board's filesystem, in
// fixed-size records grouped into small "bucket" files, and holds only some of them in RAM:
//
//   * Persistent store: records of CONTACT_RECORD_SIZE bytes, CONTACT_RECORD_BUCKET records per bucket file. A save
//     rewrites one bucket through the backend's atomic swap, so a power cut keeps the previous bucket (and with it
//     the previous version of every record in it). The capacity comes from the free filesystem space at boot and is
//     reported. A RAM index (16 bytes per record) lets a lookup by key prefix or by the one-byte path hash be
//     answered without touching the filesystem.
//   * RAM cache: a fixed number of slots, sized at boot from the measured free heap between a floor and a ceiling.
//     Favourites are kept resident; the rest are loaded on demand and replaced least recently used first.
//   * Store eviction happens only when the store is full and the caller allows overwriting: the lowest "meaningful"
//     score goes (a direct message either way in the last 30 days, then a known path, then the last time it was
//     heard). Favourites are never evicted from either tier.
//
// The class knows nothing about files: persistence goes through a ContactBackend, so the logic runs on the native
// unit-test target with an in-memory backend. Without a backend the store is RAM-only: every slot is resident and
// nothing is saved (the stock-array fallback, and examples that keep their own contact file).

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ContactInfo.h"

#ifndef CONTACT_RECORD_BUCKET
  #define CONTACT_RECORD_BUCKET 8      // records per bucket file; 8 x 160 = 1280 bytes
#endif
#define CONTACT_RECORD_SIZE   160      // 152-byte stock record + last message time + CRC32
#define CONTACT_RECENT_SECS   (30UL * 24 * 3600)   // a message newer than this makes a contact "recently messaged"
#define CONTACT_WORKING_SET   8        // cache slots that are never pinned for favourites

// Persistence port. A bucket is CONTACT_RECORD_BUCKET * CONTACT_RECORD_SIZE bytes.
class ContactBackend {
public:
  virtual ~ContactBackend() { }
  virtual bool bucketExists(uint16_t b) = 0;                              // also completes or drops an interrupted swap
  virtual bool readBucket(uint16_t b, uint8_t* buf, uint32_t len) = 0;    // false when absent or short
  virtual bool writeBucket(uint16_t b, const uint8_t* buf, uint32_t len) = 0;   // atomic: old or new content, never a mix
  virtual bool removeBucket(uint16_t b) = 0;
  // Keeps a copy of a damaged bucket's bytes under a separate name (never read back as a bucket) before it is rewritten.
  // false = could not; the bucket is then left as it is. The default cannot, so a backend that wants repairs overrides it.
  virtual bool keepAside(uint16_t b, const uint8_t* buf, uint32_t len) { (void)b; (void)buf; (void)len; return false; }
  virtual uint32_t freeBytes() = 0;                                       // free space on the contact filesystem
};

// Told before a stored contact is evicted to make room (the stock overwrite event).
class ContactStoreListener {
public:
  virtual void onContactEvicted(const uint8_t* pub_key) = 0;
};

struct ContactStoreConfig {
  uint32_t free_heap = 0;          // measured by the caller at boot; 0 = unknown, the floors apply
  uint32_t heap_margin = 40000;    // bytes of heap never touched by the contact memory
  uint8_t  heap_percent = 40;      // share of the heap above the margin the index and the cache may use
  uint16_t cache_floor = 16;       // fewest cache slots (the board must always manage this many)
  uint16_t cache_ceiling = 96;
  uint16_t store_floor = 100;      // fewest records the store tries to hold when the filesystem allows
  uint16_t store_ceiling = 1000;
  uint32_t fs_reserve = 8192;      // filesystem bytes left alone for prefs, identity, channels, blobs and a bucket swap
  uint16_t fs_bytes_per_record = 200;   // flash cost of one record including file system overhead (estimate)
  bool     stock_policy = false;   // evict the oldest lastmod (stock) instead of the lowest meaningful score
};

struct ContactStoreStats {
  uint16_t used = 0, capacity = 0, cache_slots = 0, cache_used = 0, favourites = 0, pinned = 0;
  uint32_t evictions = 0, refused_full = 0, refused_all_favourites = 0, damaged = 0, write_errors = 0;
  uint32_t loads = 0, hits = 0;
  uint32_t ram_bytes = 0;          // heap taken by the index, the cache and the bucket buffer
  bool persistent = false;
};

class ContactStore {
public:
  static const int NONE = -1;
  static const uint16_t NOSLOT = 0xFFFF;

  enum Refusal : uint8_t { REFUSED_NONE = 0, REFUSED_FULL = 1, REFUSED_ALL_FAVOURITES = 2 };

  struct Idx {                     // one per record, always in RAM: 16 bytes
    uint8_t key[4];                // first four bytes of the public key
    uint32_t lastmod;
    uint32_t last_msg;             // our clock, last direct message either way (0 = none)
    uint16_t slot;                 // cache slot, or NOSLOT
    uint8_t flags;                 // F_*
    uint8_t pad;
  };
  enum : uint8_t { F_USED = 1, F_FAV = 2, F_PATH = 4, F_PROV = 8 };

  struct Slot {                    // one resident contact
    ContactInfo ci;
    int16_t rec;                   // record id, or -1 when the slot is free
    uint8_t pinned;                // resident favourite, never replaced by the cache
    uint8_t prov;                  // allocated but not yet written
    uint32_t last_use;
    uint32_t saved_crc;            // CRC32 of the record image as last loaded or written
  };

  ContactStore();
  ~ContactStore();

  // Sets the store up. backend == NULL gives a RAM-only store of cfg.store_ceiling records, all resident.
  bool begin(ContactBackend* backend, ContactStoreListener* listener, const ContactStoreConfig& cfg);
  bool ready() const { return _ready; }
  bool persistent() const { return _persistent; }
  void end();

  // --- lookups. Returned pointers stay valid until the slot is replaced, which needs at least
  //     CONTACT_WORKING_SET further distinct contacts to be touched.
  ContactInfo* find(const uint8_t* pub_key, int prefix_len, int* rec_out = NULL);
  ContactInfo* findByName(const char* name_prefix);
  int findByHash(const uint8_t* hash, int* out, int max);          // record ids whose key starts with the hash
  ContactInfo* get(int rec);
  int recOfPtr(const ContactInfo* p) const;                        // -1 when p is not a resident contact

  // --- changes
  ContactInfo* allocate(bool allow_evict, uint32_t now, int* rec_out = NULL);   // zeroed, to be filled by the caller
  bool remove(int rec);
  void noteMessage(int rec, uint32_t now);                         // a direct message was sent or received
  void invalidateSecrets();
  // Rescue console "rebuild": re-verifies every stored record, keeps every valid one (cached or not), drops and counts
  // those whose CRC fails, keeps each damaged bucket aside, rewrites the buckets atomically and rebuilds the index.
  // Returns the contacts kept.
  int  rebuild(int* damaged_out, int* isolated_out);
  bool flush();                                                    // writes every changed resident record; false on a write error

  // --- bulk load (migration): records go straight to buckets, bypassing the cache
  void importBegin();
  bool importAdd(const ContactInfo& c, uint32_t last_msg, uint32_t now);
  bool importEnd();
  bool clearAll();                                                 // removes every bucket (a restarted migration)

  // --- iteration and counts
  bool next(int& cursor, ContactInfo& out);                        // used records in id order
  int count() const { return _count; }
  int capacity() const { return _cap; }
  uint32_t newestLastmod();
  int favourites();
  Refusal lastRefusal() const { return _refusal; }

  ContactStoreStats stats();
  int format(char* dst, size_t n);                                 // one line for "get contacts.capacity"

  // --- the record image, public for tests and the migration check
  static void encode(const ContactInfo& c, uint32_t last_msg, uint8_t out[CONTACT_RECORD_SIZE]);
  static bool decode(const uint8_t in[CONTACT_RECORD_SIZE], ContactInfo& c, uint32_t& last_msg);   // false: free or damaged
  static uint32_t crc32(const uint8_t* p, size_t n);
  static int  meaningClass(const Idx& e, uint32_t now);            // 2 recent message, 1 older message or known path, 0 advert only

  // Test access
  const Idx* index() const { return _idx; }
  int cacheSlots() const { return _nslots; }

private:
  bool _ready;
  bool _persistent;
  ContactBackend* _be;
  ContactStoreListener* _listener;
  ContactStoreConfig _cfg;
  Idx* _idx;
  Slot* _slots;
  uint8_t* _scratch;               // one bucket
  int _scratch_bucket;
  uint8_t* _bdirty;                // bit per bucket: needs a rewrite
  int _cap, _nslots, _count, _pinned, _pin_limit, _first_free, _nbuckets;
  uint32_t _tick;
  uint32_t _evictions, _refused_full, _refused_fav, _damaged, _write_errors, _loads, _hits;
  Refusal _refusal;
  bool _static_storage;
  // import state
  bool _importing;
  int _import_bucket;

  static int bucketOf(int rec) { return rec / CONTACT_RECORD_BUCKET; }
  int  acquireSlot();
  void bind(int slot, int rec);
  void unbind(int slot);
  void touch(int slot);
  void refreshPins();
  void syncIndex(int slot);
  void syncAll();
  bool slotDirty(const Slot& s);
  void imageOf(const Slot& s, uint8_t* out);
  bool readRecord(int rec, ContactInfo& out, uint32_t& last_msg);
  bool loadScratch(int b);
  bool flushBucket(int b);
  void markBucket(int b) { if (_bdirty) _bdirty[b >> 3] |= (uint8_t)(1u << (b & 7)); }
  int  freeRecord();
  int  pickVictim(uint32_t now);
  void release();
  bool loadFromBackend();
  void setIdxFromContact(Idx& e, const ContactInfo& c, uint32_t last_msg);
  bool importFlushBucket();
};
