// The contact store (src/helpers/ContactStore.*, docs/CONTACTS.md): record image, cache, eviction, bulk import and
// what survives a failed write. The backend is an in-memory map of buckets that can be told to fail.
#include <gtest/gtest.h>
#include <map>
#include <vector>
#include <cstring>

#include "helpers/ContactStore.h"

namespace {

class MemBackend : public ContactBackend {
public:
  std::map<int, std::vector<uint8_t>> files;
  uint32_t fs_total = 0xFFFFFFFFu;   // size of the file system (unknown by default)
  int fail_after = -1;            // writes allowed before every further write fails (-1 = never fail)
  int writes = 0;

  bool can_list = false;          // a backend that lists its files once at boot
  int exists_calls = 0;
  bool bucketExists(uint16_t b) override { exists_calls++; return files.count(b) != 0; }
  bool listBuckets(uint16_t limit, uint8_t* present) override {
    if (!can_list) return false;
    for (auto& f : files) if (f.first < limit) present[f.first >> 3] |= (uint8_t)(1u << (f.first & 7));
    return true;
  }
  bool readBucket(uint16_t b, uint8_t* buf, uint32_t len) override {
    auto it = files.find(b);
    if (it == files.end() || it->second.size() != len) return false;
    memcpy(buf, it->second.data(), len);
    return true;
  }
  bool writeBucket(uint16_t b, const uint8_t* buf, uint32_t len) override {
    if (fail_after >= 0 && writes >= fail_after) return false;   // the old file stays, as with the atomic swap
    writes++;
    files[b] = std::vector<uint8_t>(buf, buf + len);
    return true;
  }
  bool removeBucket(uint16_t b) override { return files.erase(b) != 0; }
  std::map<int, std::vector<uint8_t>> aside;
  bool aside_fails = false;
  bool keepAside(uint16_t b, const uint8_t* buf, uint32_t len) override {
    if (aside_fails) return false;
    aside[b] = std::vector<uint8_t>(buf, buf + len);
    return true;
  }
  uint32_t freeBytes() override {
    if (fs_total == 0xFFFFFFFFu) return fs_total;
    uint32_t used = 0;
    for (auto& f : files) used += (uint32_t)f.second.size();
    return fs_total > used ? fs_total - used : 0;
  }
};

struct Listener : ContactStoreListener {
  std::vector<std::vector<uint8_t>> evicted;
  void onContactEvicted(const uint8_t* k) override { evicted.push_back(std::vector<uint8_t>(k, k + 32)); }
};

const uint32_t NOW = 1760000000;
const uint32_t DAY = 24 * 3600;

ContactInfo mk(int n, uint8_t flags = 0, uint32_t lastmod = NOW - 100, uint8_t path_len = OUT_PATH_UNKNOWN) {
  ContactInfo c;
  memset(&c, 0, sizeof(c));
  uint8_t key[32];
  memset(key, 0x40, 32);
  key[0] = (uint8_t)(n & 0xFF);
  key[1] = (uint8_t)((n >> 8) & 0xFF);
  key[31] = 0x77;
  c.id = mesh::Identity(key);
  snprintf(c.name, sizeof(c.name), "contact-%d", n);
  c.type = 1;
  c.flags = flags;
  c.out_path_len = path_len;
  c.lastmod = lastmod;
  c.last_advert_timestamp = lastmod - 5;
  c.gps_lat = n * 1000;
  c.gps_lon = -n * 1000;
  return c;
}

uint8_t keyOf(int n) { return (uint8_t)(n & 0xFF); }

int add(ContactStore& s, const ContactInfo& c, bool evict = false, uint32_t now = NOW) {
  int rec = -1;
  ContactInfo* d = s.allocate(evict, now, &rec);
  if (!d) return -1;
  *d = c;
  return rec;
}

ContactStoreConfig cfg(uint32_t heap = 300000) {
  ContactStoreConfig c;
  c.free_heap = heap;
  c.fs_reserve = 0;
  c.store_floor = 8;
  return c;
}

// A store of exactly `records` records on a backend with room for just that many.
ContactStoreConfig tight(MemBackend& be, int records, uint16_t cache = 16) {
  ContactStoreConfig c = cfg();
  be.fs_total = (uint32_t)records * c.fs_bytes_per_record;
  c.cache_floor = cache;
  c.cache_ceiling = cache;
  return c;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- record image

TEST(ContactRecord, FirstBytesAreTheStockRecordByteForByte) {
  ContactInfo c = mk(5, 0x01, 1234, 3);
  c.sync_since = 0x01020304;
  for (int i = 0; i < MAX_PATH_SIZE; i++) c.out_path[i] = (uint8_t)(0xA0 + i);
  uint8_t img[CONTACT_RECORD_SIZE];
  ContactStore::encode(c, 777, img);

  uint8_t stock[152];
  int o = 0;
  memcpy(stock + o, c.id.pub_key, 32); o += 32;
  memcpy(stock + o, c.name, 32); o += 32;
  stock[o++] = c.type; stock[o++] = c.flags; stock[o++] = 0;
  memcpy(stock + o, &c.sync_since, 4); o += 4;
  stock[o++] = c.out_path_len;
  memcpy(stock + o, &c.last_advert_timestamp, 4); o += 4;
  memcpy(stock + o, c.out_path, 64); o += 64;
  memcpy(stock + o, &c.lastmod, 4); o += 4;
  memcpy(stock + o, &c.gps_lat, 4); o += 4;
  memcpy(stock + o, &c.gps_lon, 4); o += 4;
  ASSERT_EQ(o, 152);
  EXPECT_EQ(0, memcmp(img, stock, 152));
}

TEST(ContactRecord, RoundTripsEveryField) {
  ContactInfo c = mk(9, 0x13, 4321, 2);
  c.sync_since = 99;
  c.out_path[0] = 7;
  uint8_t img[CONTACT_RECORD_SIZE];
  ContactStore::encode(c, 555, img);
  ContactInfo d;
  uint32_t lm = 0;
  ASSERT_TRUE(ContactStore::decode(img, d, lm));
  EXPECT_EQ(555u, lm);
  EXPECT_EQ(0, memcmp(c.id.pub_key, d.id.pub_key, 32));
  EXPECT_STREQ(c.name, d.name);
  EXPECT_EQ(c.flags, d.flags);
  EXPECT_EQ(c.out_path_len, d.out_path_len);
  EXPECT_EQ(0, memcmp(c.out_path, d.out_path, 64));
  EXPECT_EQ(c.lastmod, d.lastmod);
  EXPECT_EQ(c.sync_since, d.sync_since);
  EXPECT_EQ(c.gps_lat, d.gps_lat);
  EXPECT_EQ(c.gps_lon, d.gps_lon);
  EXPECT_FALSE(d.shared_secret_valid);
}

TEST(ContactRecord, AnySingleFlippedByteIsRejected) {
  uint8_t img[CONTACT_RECORD_SIZE];
  ContactStore::encode(mk(3), 12, img);
  for (int i = 0; i < CONTACT_RECORD_SIZE; i++) {
    uint8_t bad[CONTACT_RECORD_SIZE];
    memcpy(bad, img, sizeof(bad));
    bad[i] ^= 0x01;
    ContactInfo d;
    uint32_t lm;
    EXPECT_FALSE(ContactStore::decode(bad, d, lm)) << "byte " << i;
  }
}

TEST(ContactRecord, AnAllZeroRecordIsFree) {
  uint8_t zero[CONTACT_RECORD_SIZE] = {0};
  ContactInfo d;
  uint32_t lm;
  EXPECT_FALSE(ContactStore::decode(zero, d, lm));
}

// ---------------------------------------------------------------------------------------------- lookups and persistence

TEST(ContactStore, FindsByFullKeyAndByPrefixAndSurvivesARestart) {
  MemBackend be;
  Listener l;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, &l, cfg()));
    for (int i = 0; i < 40; i++) ASSERT_GE(add(s, mk(i)), 0);
    EXPECT_TRUE(s.flush());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, cfg()));
  EXPECT_EQ(40, s.count());
  ContactInfo probe = mk(23);
  ContactInfo* c = s.find(probe.id.pub_key, 32);
  ASSERT_NE(nullptr, c);
  EXPECT_STREQ("contact-23", c->name);
  EXPECT_EQ(23000, c->gps_lat);
  ASSERT_NE(nullptr, s.find(probe.id.pub_key, 6));
  ASSERT_NE(nullptr, s.find(probe.id.pub_key, 2));
  uint8_t shorter[32];                                  // a two-byte prefix followed by bytes that are not the key's
  memset(shorter, 0xEE, sizeof(shorter));
  memcpy(shorter, probe.id.pub_key, 2);
  ContactInfo* by_two = s.find(shorter, 2);
  ASSERT_NE(nullptr, by_two) << "only the bytes of the prefix may be compared";
  EXPECT_STREQ("contact-23", by_two->name);
  EXPECT_EQ(nullptr, s.find(mk(500).id.pub_key, 32));
}

TEST(ContactStore, ABackendThatListsItsFilesIsNeverProbedBucketByBucketAtBoot) {
  for (int listing = 0; listing < 2; listing++) {
    MemBackend be;
    be.can_list = listing == 1;
    {
      ContactStore s;
      ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
      for (int i = 0; i < 40; i++) ASSERT_GE(add(s, mk(i)), 0);
      EXPECT_TRUE(s.flush());
    }
    be.exists_calls = 0;
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    EXPECT_EQ(40, s.count()) << "same contacts either way";
    if (listing) EXPECT_EQ(0, be.exists_calls) << "the listing replaces every per-bucket lookup";
    else EXPECT_GT(be.exists_calls, 0) << "a backend that cannot list keeps the per-bucket probe";
  }
}

TEST(ContactStore, ContactsSharingTheFirstFourKeyBytesAreTold_Apart) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  ContactInfo a = mk(1), b = mk(1);
  b.id.pub_key[31] = 0x78;                 // same first 31 bytes
  snprintf(b.name, sizeof(b.name), "other");
  ASSERT_GE(add(s, a), 0);
  ASSERT_GE(add(s, b), 0);
  ContactInfo* got = s.find(b.id.pub_key, 32);
  ASSERT_NE(nullptr, got);
  EXPECT_STREQ("other", got->name);
  got = s.find(a.id.pub_key, 32);
  ASSERT_NE(nullptr, got);
  EXPECT_STREQ("contact-1", got->name);
}

TEST(ContactStore, FindByHashReturnsEveryRecordWithThatFirstByte) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  for (int i = 0; i < 20; i++) ASSERT_GE(add(s, mk(i * 256 + 7)), 0);   // all start with 0x07
  for (int i = 0; i < 15; i++) ASSERT_GE(add(s, mk(i + 1000)), 0);     // 1000.. start with other bytes
  uint8_t h = 7;
  int out[40];
  EXPECT_EQ(20, s.findByHash(&h, out, 40));
  EXPECT_EQ(5, s.findByHash(&h, out, 5));
}

TEST(ContactStore, AChangeThroughTheReturnedPointerIsSavedByFlush) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 10; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
    ContactInfo* c = s.find(mk(4).id.pub_key, 32);
    c->flags |= 0x01;
    c->out_path_len = 2;
    snprintf(c->name, sizeof(c->name), "renamed");
    EXPECT_TRUE(s.flush());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  ContactInfo* c = s.find(mk(4).id.pub_key, 32);
  ASSERT_NE(nullptr, c);
  EXPECT_STREQ("renamed", c->name);
  EXPECT_EQ(1, c->flags & 1);
  EXPECT_EQ(2, c->out_path_len);
}

TEST(ContactStore, FlushWritesOnlyTheBucketsThatChanged) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  for (int i = 0; i < 64; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  int before = be.writes;
  s.find(mk(3).id.pub_key, 32)->lastmod += 1;
  ASSERT_TRUE(s.flush());
  EXPECT_EQ(before + 1, be.writes);
  ASSERT_TRUE(s.flush());
  EXPECT_EQ(before + 1, be.writes) << "nothing changed, nothing written";
}

TEST(ContactStore, RemoveIsSavedAndAnEmptyBucketFileGoesAway) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 9; i++) add(s, mk(i));       // bucket 0 holds 8, bucket 1 holds 1
    ASSERT_TRUE(s.flush());
    EXPECT_EQ(2u, be.files.size());
    int rec = -1;
    ASSERT_NE(nullptr, s.find(mk(8).id.pub_key, 32, &rec));
    EXPECT_TRUE(s.remove(rec));
    ASSERT_TRUE(s.flush());
    EXPECT_EQ(1u, be.files.size());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  EXPECT_EQ(8, s.count());
  EXPECT_EQ(nullptr, s.find(mk(8).id.pub_key, 32));
}

TEST(ContactStore, ADamagedRecordIsSkippedAndCountedTheOthersLoad) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 8; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
  }
  be.files[0][3 * CONTACT_RECORD_SIZE + 10] ^= 0xFF;     // damage record 3
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  EXPECT_EQ(7, s.count());
  EXPECT_EQ(1u, s.stats().damaged);
  EXPECT_EQ(nullptr, s.find(mk(3).id.pub_key, 32));
  EXPECT_NE(nullptr, s.find(mk(4).id.pub_key, 32));
}

// ---------------------------------------------------------------------------------------------- the cache

TEST(ContactCache, SizeComesFromTheFreeHeapBetweenTheFloorAndTheCeiling) {
  MemBackend be;
  ContactStoreConfig c = cfg(300000);
  c.cache_floor = 16; c.cache_ceiling = 96;
  ContactStore big;
  ASSERT_TRUE(big.begin(&be, nullptr, c));
  EXPECT_EQ(96, big.cacheSlots());

  c.free_heap = 50000;       // 10000 above the margin, 4000 usable: below the floor
  ContactStore small;
  ASSERT_TRUE(small.begin(&be, nullptr, c));
  EXPECT_EQ(16, small.cacheSlots());

  c.free_heap = 40000 + 60000;      // 60 KB above the margin -> 24 KB budget
  ContactStore mid;
  ASSERT_TRUE(mid.begin(&be, nullptr, c));
  EXPECT_GT(mid.cacheSlots(), 16);
  EXPECT_LT(mid.cacheSlots(), 96);

  c.free_heap = 0;           // unknown
  c.store_floor = 100;
  ContactStore unk;
  ASSERT_TRUE(unk.begin(&be, nullptr, c));
  EXPECT_EQ(16, unk.cacheSlots());
}

TEST(ContactCache, AStoreLargerThanTheCacheKeepsEveryContactReachable) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 120; i++) ASSERT_GE(add(s, mk(i)), 0) << i;
  EXPECT_EQ(120, s.count());
  EXPECT_EQ(16, s.cacheSlots());
  ASSERT_TRUE(s.flush());
  for (int i = 0; i < 120; i++) {
    ContactInfo* c = s.find(mk(i).id.pub_key, 32);
    ASSERT_NE(nullptr, c) << i;
    EXPECT_EQ(i * 1000, c->gps_lat);
  }
  EXPECT_LE(s.stats().cache_used, 16);
}

TEST(ContactCache, FavouritesStayResidentWhileEverythingElseCycles) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 5; i++) ASSERT_GE(add(s, mk(i, 0x01)), 0);
  for (int i = 5; i < 120; i++) ASSERT_GE(add(s, mk(i)), 0);
  ASSERT_TRUE(s.flush());
  for (int i = 5; i < 120; i++) ASSERT_NE(nullptr, s.find(mk(i).id.pub_key, 32));   // cycle the cache
  uint32_t loads = s.stats().loads;
  for (int i = 0; i < 5; i++) ASSERT_NE(nullptr, s.find(mk(i).id.pub_key, 32));
  EXPECT_EQ(loads, s.stats().loads) << "a favourite was read from the file system";
  EXPECT_EQ(5, s.stats().pinned);
}

TEST(ContactCache, FavouritesAreResidentFromBootWithoutBeingTouched) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
    for (int i = 0; i < 4; i++) add(s, mk(i, 0x01));
    for (int i = 4; i < 60; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  EXPECT_EQ(4, s.stats().pinned);
  uint32_t loads = s.stats().loads;
  for (int i = 0; i < 4; i++) ASSERT_NE(nullptr, s.find(mk(i).id.pub_key, 32));
  EXPECT_EQ(loads, s.stats().loads);
}

TEST(ContactCache, PinnedFavouritesNeverTakeTheWholeCache) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 30; i++) ASSERT_GE(add(s, mk(i, 0x01)), 0);     // more favourites than slots
  for (int i = 30; i < 50; i++) ASSERT_GE(add(s, mk(i)), 0);
  ASSERT_TRUE(s.flush());
  EXPECT_LE(s.stats().pinned, 16 - CONTACT_WORKING_SET);
  for (int i = 0; i < 50; i++) ASSERT_NE(nullptr, s.find(mk(i).id.pub_key, 32)) << i;   // all still reachable
}

TEST(ContactCache, TheLeastRecentlyUsedNonFavouriteIsReplacedFirst) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  s.find(mk(0).id.pub_key, 32);                      // 0 is now the most recently used
  uint32_t loads = s.stats().loads;
  add(s, mk(100));                                   // needs a slot: the oldest (1) goes
  ASSERT_NE(nullptr, s.find(mk(0).id.pub_key, 32));
  EXPECT_EQ(loads, s.stats().loads);
  ASSERT_NE(nullptr, s.find(mk(1).id.pub_key, 32));
  EXPECT_EQ(loads + 1, s.stats().loads);
}

TEST(ContactCache, ADirtyRecordIsWrittenBeforeItsSlotIsReused) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  s.find(mk(1).id.pub_key, 32)->gps_lat = 424242;    // a change nobody flushed
  for (int i = 100; i < 140; i++) add(s, mk(i));      // cycles the whole cache
  ContactInfo* c = s.find(mk(1).id.pub_key, 32);
  ASSERT_NE(nullptr, c);
  EXPECT_EQ(424242, c->gps_lat);
}

TEST(ContactCache, ACacheSlotIsNotReusedWhileItsRecordCannotBeWritten) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  for (int i = 0; i < 16; i++) s.find(mk(i).id.pub_key, 32)->gps_lat += 1;   // every slot dirty
  be.fail_after = be.writes;                                                 // the file system stops taking writes
  int rec = -1;
  EXPECT_EQ(nullptr, s.allocate(false, NOW, &rec)) << "a changed record was dropped to make room";
  EXPECT_GT(s.stats().write_errors, 0u);
  be.fail_after = -1;
  EXPECT_TRUE(s.flush());
  EXPECT_EQ(1001, s.find(mk(1).id.pub_key, 32)->gps_lat);
}

// ---------------------------------------------------------------------------------------------- capacity

TEST(ContactCapacity, ComesFromTheFreeFilesystemSpaceAndIsReported) {
  MemBackend be;
  ContactStoreConfig c = cfg();
  be.fs_total = 30 * c.fs_bytes_per_record;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, c));
  EXPECT_EQ(32, s.capacity());                       // rounded up to whole buckets
  char line[200];
  s.format(line, sizeof(line));
  EXPECT_NE(nullptr, strstr(line, "store 0/32"));
  EXPECT_NE(nullptr, strstr(line, "adaptive"));
}

TEST(ContactCapacity, FollowsTheFreeHeapAboveTheFloorAndStopsAtTheCeiling) {
  MemBackend be;
  ContactStoreConfig c = cfg(40000 + 25000);         // 10 KB budget: 5 KB of index = 312 records
  c.store_floor = 100; c.store_ceiling = 1000;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, c));
  EXPECT_GT(s.capacity(), 300);
  EXPECT_LT(s.capacity(), 330);

  c.free_heap = 1000000;
  ContactStore big;
  ASSERT_TRUE(big.begin(&be, nullptr, c));
  EXPECT_EQ(1000, big.capacity());

  c.free_heap = 41000;                                // almost nothing: the floor holds
  ContactStore floor;
  ASSERT_TRUE(floor.begin(&be, nullptr, c));
  EXPECT_EQ(104, floor.capacity());                   // 100 rounded up to whole buckets
}

TEST(ContactCapacity, NeverShrinksBelowWhatIsAlreadyStored) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 100; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
  }
  ContactStoreConfig c = cfg();
  be.fs_total = 1;                                    // the file system is full now
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, c));
  EXPECT_GE(s.capacity(), 100);
  EXPECT_EQ(100, s.count());
}

TEST(ContactCapacity, OurOwnFilesCountAsFreeSpaceWhenSizingTheStore) {
  MemBackend be;
  ContactStoreConfig c = cfg();
  be.fs_total = 100 * c.fs_bytes_per_record + 12 * 1280;     // room for 100 records after the 12 buckets of the first run
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, c));
    for (int i = 0; i < 96; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
  }
  ContactStore s;                                     // what is free now is small: the buckets are there
  ASSERT_TRUE(s.begin(&be, nullptr, c));
  EXPECT_GE(s.capacity(), 100);                       // free space plus the 12 buckets already there
  EXPECT_LT(s.capacity(), 200);
}

// ---------------------------------------------------------------------------------------------- eviction

TEST(ContactEviction, OnlyHappensWhenTheStoreIsFullAndTheCallerAllowsIt) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 16; i++) ASSERT_GE(add(s, mk(i)), 0);
  EXPECT_EQ(-1, add(s, mk(99), false)) << "overwriting was not allowed";
  EXPECT_EQ(ContactStore::REFUSED_FULL, s.lastRefusal());
  EXPECT_TRUE(l.evicted.empty());
  EXPECT_EQ(16, s.count());
  EXPECT_GE(add(s, mk(99), true), 0);
  EXPECT_EQ(1u, l.evicted.size());
  EXPECT_EQ(16, s.count());
}

TEST(ContactEviction, TheOverwriteEventCarriesTheEvictedKeyAndComesBeforeTheSlotIsReused) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i, 0, NOW - 1000 + i));      // contact 0 is the oldest
  add(s, mk(99), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32));
  EXPECT_EQ(nullptr, s.find(mk(0).id.pub_key, 32));
  EXPECT_NE(nullptr, s.find(mk(99).id.pub_key, 32));
}

TEST(ContactEviction, ALowestScoreContactThatIsOnlyOnTheFlashIsEvictedToo) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i, 0, NOW - 1000 + i));
  ASSERT_TRUE(s.flush());
  ContactStore t;
  ASSERT_TRUE(t.begin(&be, &l, tight(be, 16, 16)));     // a restart: nothing is resident
  EXPECT_EQ(0u, t.stats().cache_used);
  add(t, mk(99), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32)) << "the event must name the record that went";
}

TEST(ContactEviction, TheLeastMeaningfulGoesFirst) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  // 0: advert only, heard long ago; 1: advert only, heard just now; 2: a known path; the rest messaged recently
  int r0 = add(s, mk(0, 0, NOW - 50 * DAY));
  int r1 = add(s, mk(1, 0, NOW - 10));
  int r2 = add(s, mk(2, 0, NOW - 40 * DAY, 2));
  (void)r0; (void)r2;
  for (int i = 3; i < 16; i++) { int r = add(s, mk(i, 0, NOW - 3 * DAY)); s.noteMessage(r, NOW - DAY); }
  (void)r1;
  add(s, mk(100, 0, NOW), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32)) << "the oldest advert-only contact goes first";
  add(s, mk(101, 0, NOW), true);
  EXPECT_EQ(0, memcmp(l.evicted[1].data(), mk(1).id.pub_key, 32)) << "an advert-only contact goes before one with a known path";
  add(s, mk(102, 0, NOW), true);
  EXPECT_EQ(0, memcmp(l.evicted[2].data(), mk(100).id.pub_key, 32)) << "then the other advert-only newcomers";
}

TEST(ContactEviction, AContactWeMessagedOutlivesOneHeardOnce) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  int friendRec = add(s, mk(0, 0, NOW - 20 * DAY));        // advert is old ...
  s.noteMessage(friendRec, NOW - 2 * DAY);                 // ... but we wrote to them
  for (int i = 1; i < 16; i++) add(s, mk(i, 0, NOW - 100 + i));     // strangers heard a moment ago
  for (int k = 0; k < 15; k++) add(s, mk(200 + k), true);            // 15 newcomers push the strangers out one by one
  EXPECT_NE(nullptr, s.find(mk(0).id.pub_key, 32)) << "the contact with message history was evicted";
  for (const auto& e : l.evicted) EXPECT_NE(0, memcmp(e.data(), mk(0).id.pub_key, 32));
}

TEST(ContactEviction, AMessageNewerThanTheWindowCountsMoreThanAnOldOne) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  int old = add(s, mk(0, 0, NOW - 100 * DAY));
  s.noteMessage(old, NOW - 90 * DAY);                       // messaged, but long ago
  int recent = add(s, mk(1, 0, NOW - 100 * DAY));
  s.noteMessage(recent, NOW - 1 * DAY);
  for (int i = 2; i < 16; i++) { int r = add(s, mk(i, 0, NOW - DAY)); s.noteMessage(r, NOW - 5 * DAY); }
  add(s, mk(100), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32));
}

TEST(ContactEviction, AnOlderMessageStillCountsMoreThanAdvertOnly) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  int old = add(s, mk(0, 0, NOW - 200 * DAY));
  s.noteMessage(old, NOW - 150 * DAY);                      // we talked months ago ...
  for (int i = 1; i < 16; i++) add(s, mk(i, 0, NOW - 100 * DAY + i));     // ... strangers heard more recently than that message
  add(s, mk(99, 0, NOW), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_NE(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32)) << "an old message counted for nothing";
}

TEST(ContactEviction, ARecentMessageOutranksAnOldOneEvenIfTheOldContactWasHeardLater) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  int old = add(s, mk(0, 0, NOW - 1 * DAY));                 // heard yesterday, last message 100 days ago
  s.noteMessage(old, NOW - 100 * DAY);
  int recent = add(s, mk(1, 0, NOW - 60 * DAY));             // last heard 60 days ago, but a message 2 days ago
  s.noteMessage(recent, NOW - 2 * DAY);
  for (int i = 2; i < 16; i++) { int r = add(s, mk(i, 0, NOW - DAY)); s.noteMessage(r, NOW - 3 * DAY); }
  add(s, mk(99, 0, NOW), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32)) << "the recency window must decide before the clock does";
}

TEST(ContactEviction, AFavouriteIsNeverEvictedHoweverOld) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 15; i++) add(s, mk(i, 0x01, NOW - 400 * DAY));       // 15 ancient favourites
  add(s, mk(15, 0, NOW - 1));                                                // one fresh non-favourite
  for (int k = 0; k < 10; k++) add(s, mk(100 + k), true);                    // newcomers keep replacing the one non-favourite
  for (int i = 0; i < 15; i++) EXPECT_NE(nullptr, s.find(mk(i).id.pub_key, 32)) << i;
  for (const auto& e : l.evicted) {
    for (int i = 0; i < 15; i++) EXPECT_NE(0, memcmp(e.data(), mk(i).id.pub_key, 32));
  }
}

TEST(ContactEviction, AddingToAStoreOfOnlyFavouritesIsRefusedAndSaysWhy) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 16; i++) ASSERT_GE(add(s, mk(i, 0x01)), 0);
  EXPECT_EQ(-1, add(s, mk(99, 0x01), true));
  EXPECT_EQ(ContactStore::REFUSED_ALL_FAVOURITES, s.lastRefusal());
  EXPECT_EQ(1u, s.stats().refused_all_favourites);
  EXPECT_TRUE(l.evicted.empty());
  EXPECT_EQ(16, s.count());
  for (int i = 0; i < 16; i++) EXPECT_NE(nullptr, s.find(mk(i).id.pub_key, 32));
}

TEST(ContactEviction, ANewFavouriteDisplacesANonFavouriteWhenOverwritingIsAllowed) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i, i == 0 ? 0x00 : 0x01));
  EXPECT_GE(add(s, mk(99, 0x01), true), 0);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32));
}

TEST(ContactEviction, TheStockPolicyEvictsTheOldestLastmodWhateverItsHistory) {
  MemBackend be;
  Listener l;
  ContactStoreConfig c = tight(be, 16);
  c.stock_policy = true;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, c));
  int r0 = add(s, mk(0, 0, NOW - 500));
  s.noteMessage(r0, NOW - 10);                         // messaged, but the oldest lastmod
  for (int i = 1; i < 16; i++) add(s, mk(i, 0, NOW - 100 + i));
  add(s, mk(99), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32));
}

TEST(ContactEviction, ScoresAreReadFromChangesMadeThroughThePointer) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  for (int i = 0; i < 16; i++) add(s, mk(i, 0, NOW - 100 + i));
  ContactInfo* c = s.find(mk(0).id.pub_key, 32);
  c->flags |= 0x01;                                    // the app made contact 0 a favourite after it was stored
  add(s, mk(99), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_EQ(0, memcmp(l.evicted[0].data(), mk(1).id.pub_key, 32)) << "a favourite set through the pointer must be protected";
}

// ---------------------------------------------------------------------------------------------- iteration, import

TEST(ContactIteration, WalksEveryStoredContactIncludingThoseNotInTheCache) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 100; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  int cursor = 0, n = 0;
  ContactInfo c;
  bool seen[100] = {false};
  while (s.next(cursor, c)) {
    int id = -1;
    sscanf(c.name, "contact-%d", &id);
    ASSERT_GE(id, 0);
    ASSERT_LT(id, 100);
    seen[id] = true;
    n++;
  }
  EXPECT_EQ(100, n);
  for (int i = 0; i < 100; i++) EXPECT_TRUE(seen[i]) << i;
}

TEST(ContactIteration, ShowsAChangeNotYetFlushed) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  for (int i = 0; i < 4; i++) add(s, mk(i));
  s.find(mk(2).id.pub_key, 32)->gps_lat = 31337;
  int cursor = 0;
  ContactInfo c;
  bool found = false;
  while (s.next(cursor, c)) if (c.gps_lat == 31337) found = true;
  EXPECT_TRUE(found);
}

TEST(ContactImport, BulkImportWritesEachBucketOnceAndReadsBackIdentically) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    s.importBegin();
    for (int i = 0; i < 350; i++) ASSERT_TRUE(s.importAdd(mk(i, i % 7 == 0 ? 0x01 : 0, NOW - i), 0, NOW));
    ASSERT_TRUE(s.importEnd());
    EXPECT_EQ(350, s.count());
    EXPECT_EQ((350 + 7) / 8, be.writes) << "one write per bucket";
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  EXPECT_EQ(350, s.count());
  for (int i = 0; i < 350; i++) {
    ContactInfo want = mk(i, i % 7 == 0 ? 0x01 : 0, NOW - i);
    ContactInfo* got = s.find(want.id.pub_key, 32);
    ASSERT_NE(nullptr, got) << i;
    EXPECT_STREQ(want.name, got->name);
    EXPECT_EQ(want.flags, got->flags);
    EXPECT_EQ(want.lastmod, got->lastmod);
    EXPECT_EQ(want.gps_lat, got->gps_lat);
  }
}

TEST(ContactImport, MoreContactsThanRoomKeepsTheMostMeaningful) {
  MemBackend be;
  Listener l;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  s.importBegin();
  for (int i = 0; i < 40; i++) ASSERT_TRUE(s.importAdd(mk(i, i < 3 ? 0x01 : 0, NOW - 1000 + i), 0, NOW));
  ASSERT_TRUE(s.importEnd());
  EXPECT_EQ(16, s.count());
  for (int i = 0; i < 3; i++) EXPECT_NE(nullptr, s.find(mk(i).id.pub_key, 32)) << "favourite " << i;
  for (int i = 27; i < 40; i++) EXPECT_NE(nullptr, s.find(mk(i).id.pub_key, 32)) << "newest " << i;
}

TEST(ContactImport, ClearAllEmptiesTheStoreAndTheBackend) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  for (int i = 0; i < 30; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  EXPECT_FALSE(be.files.empty());
  ASSERT_TRUE(s.clearAll());
  EXPECT_EQ(0, s.count());
  EXPECT_TRUE(be.files.empty());
  EXPECT_GE(add(s, mk(1)), 0);
}

// ---------------------------------------------------------------------------------------------- failed writes

TEST(ContactWrites, AFailedWriteLeavesThePreviousRecordAndIsRetried) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 8; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
    s.find(mk(3).id.pub_key, 32)->gps_lat = 555;
    be.fail_after = be.writes;                        // the swap fails: the old file stays
    EXPECT_FALSE(s.flush());
    EXPECT_EQ(1u, s.stats().write_errors);
  }
  {
    ContactStore s;                                   // "power cut" at that point
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    EXPECT_EQ(8, s.count());
    EXPECT_EQ(3000, s.find(mk(3).id.pub_key, 32)->gps_lat) << "the previous version of the record must survive";
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  s.find(mk(3).id.pub_key, 32)->gps_lat = 555;
  be.fail_after = -1;
  EXPECT_TRUE(s.flush());
  ContactStore t;
  ASSERT_TRUE(t.begin(&be, nullptr, cfg()));
  EXPECT_EQ(555, t.find(mk(3).id.pub_key, 32)->gps_lat);
}

TEST(ContactWrites, AFlushCutBetweenBucketsLeavesEveryBucketWhole) {
  MemBackend be;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
    for (int i = 0; i < 24; i++) add(s, mk(i));
    ASSERT_TRUE(s.flush());
    for (int i = 0; i < 24; i += 8) s.find(mk(i).id.pub_key, 32)->gps_lat = 9999;   // one change per bucket
    be.fail_after = be.writes + 1;                                                // the second write onward is lost
    EXPECT_FALSE(s.flush());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  EXPECT_EQ(24, s.count());
  int new_versions = 0;
  for (int i = 0; i < 24; i += 8) if (s.find(mk(i).id.pub_key, 32)->gps_lat == 9999) new_versions++;
  EXPECT_EQ(1, new_versions);
  for (int i = 0; i < 24; i++) ASSERT_NE(nullptr, s.find(mk(i).id.pub_key, 32)) << i;
}

// ---------------------------------------------------------------------------------------------- RAM only

TEST(ContactRamOnly, HoldsTheNominalCountWithEverythingResidentAndSavesNothing) {
  Listener l;
  ContactStoreConfig c;
  c.store_ceiling = 20;
  ContactStore s;
  ASSERT_TRUE(s.begin(nullptr, &l, c));
  EXPECT_EQ(20, s.capacity());
  EXPECT_FALSE(s.stats().persistent);
  for (int i = 0; i < 20; i++) ASSERT_GE(add(s, mk(i)), 0);
  EXPECT_EQ(-1, add(s, mk(99), false));
  EXPECT_GE(add(s, mk(99), true), 0);
  EXPECT_EQ(1u, l.evicted.size());
  EXPECT_TRUE(s.flush());
  for (int i = 1; i < 20; i++) EXPECT_NE(nullptr, s.find(mk(i).id.pub_key, 32));
}

TEST(ContactRamOnly, ARemovedContactFreesItsRecord) {
  ContactStoreConfig c;
  c.store_ceiling = 4;
  ContactStore s;
  ASSERT_TRUE(s.begin(nullptr, nullptr, c));
  for (int i = 0; i < 4; i++) add(s, mk(i));
  int rec = -1;
  ASSERT_NE(nullptr, s.find(mk(2).id.pub_key, 32, &rec));
  EXPECT_TRUE(s.remove(rec));
  EXPECT_EQ(3, s.count());
  EXPECT_GE(add(s, mk(50)), 0);
  EXPECT_EQ(4, s.count());
  EXPECT_EQ(nullptr, s.find(mk(2).id.pub_key, 32));
}

TEST(ContactStore, RecOfPtrNamesTheRecordOfAResidentContactOnly) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  int rec = add(s, mk(7));
  ContactInfo* c = s.find(mk(7).id.pub_key, 32);
  EXPECT_EQ(rec, s.recOfPtr(c));
  ContactInfo copy = *c;
  EXPECT_EQ(-1, s.recOfPtr(&copy));
}

// "rebuild" works from the persistent store: more stored contacts than cache slots, all of them survive.
TEST(ContactStore, RebuildKeepsEveryStoredContactNotOnlyTheCachedOnes) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 60; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  ASSERT_LT(s.stats().cache_used, 60);
  int damaged = -1, isolated = -1;
  EXPECT_EQ(60, s.rebuild(&damaged, &isolated));
  EXPECT_EQ(0, damaged);
  EXPECT_EQ(0, isolated);
  EXPECT_EQ(60, s.count());
  EXPECT_TRUE(be.aside.empty());
  ContactStore t;
  ASSERT_TRUE(t.begin(&be, nullptr, tight(be, 200, 16)));
  int cursor = 0, n = 0;
  ContactInfo c;
  while (t.next(cursor, c)) n++;
  EXPECT_EQ(60, n);
  for (int i = 0; i < 60; i++) ASSERT_NE(nullptr, t.find(mk(i).id.pub_key, 32)) << i;
}

TEST(ContactStore, RebuildWritesAChangedCachedRecordAndKeepsTheOthers) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 40; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  ContactInfo* c = s.find(mk(39).id.pub_key, 32);
  ASSERT_NE(nullptr, c);
  snprintf(c->name, sizeof(c->name), "changed");               // changed through the pointer, not yet flushed
  int d, i;
  EXPECT_EQ(40, s.rebuild(&d, &i));
  ContactStore t;
  ASSERT_TRUE(t.begin(&be, nullptr, tight(be, 200, 16)));
  EXPECT_STREQ("changed", t.find(mk(39).id.pub_key, 32)->name);
  EXPECT_EQ(40, t.count());
}

TEST(ContactStore, RebuildIsolatesADamagedRecordAndKeepsTheBucketAside) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 40; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  std::vector<uint8_t> before = be.files[1];
  be.files[1][2 * CONTACT_RECORD_SIZE + 10] ^= 0xFF;           // record 10: bucket 1, third record
  std::vector<uint8_t> damaged_image = be.files[1];
  s.end();
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  EXPECT_EQ(39, s.count());
  int d, i;
  EXPECT_EQ(39, s.rebuild(&d, &i));
  EXPECT_EQ(1, d);
  EXPECT_EQ(1, i);
  ASSERT_EQ(1u, be.aside.size());
  EXPECT_EQ(damaged_image, be.aside[1]) << "the damaged bucket is kept as it was found";
  ContactStore t;
  ASSERT_TRUE(t.begin(&be, nullptr, tight(be, 200, 16)));
  EXPECT_EQ(39, t.count());
  EXPECT_EQ(0u, t.stats().damaged) << "the rewritten bucket is clean";
  EXPECT_EQ(nullptr, t.find(mk(10).id.pub_key, 32));
  for (int n = 0; n < 40; n++) if (n != 10) ASSERT_NE(nullptr, t.find(mk(n).id.pub_key, 32)) << n;
  for (int j = 0; j < 8; j++) {                                // the other records of the bucket are byte for byte as before
    if (j == 2) continue;
    EXPECT_EQ(0, memcmp(&be.files[1][j * CONTACT_RECORD_SIZE], &before[j * CONTACT_RECORD_SIZE], CONTACT_RECORD_SIZE)) << j;
  }
}

TEST(ContactStore, RebuildLeavesTheBucketAloneWhenItCannotBeKeptAside) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 24; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  be.files[0][5] ^= 0xFF;
  std::vector<uint8_t> found = be.files[0];
  be.aside_fails = true;
  s.end();
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  int d, i;
  s.rebuild(&d, &i);
  EXPECT_EQ(1, d);
  EXPECT_EQ(0, i);
  EXPECT_EQ(found, be.files[0]) << "no copy, no rewrite";
}

TEST(ContactStore, RebuildDropsAnIndexEntryWhoseRecordIsGone) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, tight(be, 200, 16)));
  for (int i = 0; i < 40; i++) add(s, mk(i));
  ASSERT_TRUE(s.flush());
  be.files.erase(2);                                           // a whole bucket vanished under the store
  int d, i;
  int kept = s.rebuild(&d, &i);
  EXPECT_LT(kept, 40);
  EXPECT_EQ(kept, s.count());
  int cursor = 0, n = 0;
  ContactInfo c;
  while (s.next(cursor, c)) n++;
  EXPECT_EQ(kept, n);
}

TEST(ContactStore, InvalidateSecretsDropsEveryCachedSharedSecret) {
  MemBackend be;
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, nullptr, cfg()));
  for (int i = 0; i < 5; i++) add(s, mk(i));
  for (int i = 0; i < 5; i++) s.find(mk(i).id.pub_key, 32)->shared_secret_valid = true;
  s.invalidateSecrets();
  for (int i = 0; i < 5; i++) EXPECT_FALSE(s.find(mk(i).id.pub_key, 32)->shared_secret_valid) << i;
}

TEST(ContactStore, NoteMessageIsKeptAcrossARestart) {
  MemBackend be;
  Listener l;
  {
    ContactStore s;
    ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
    int r = add(s, mk(0, 0, NOW - 90 * DAY));
    s.noteMessage(r, NOW - DAY);
    for (int i = 1; i < 16; i++) add(s, mk(i, 0, NOW - 100 + i));
    ASSERT_TRUE(s.flush());
  }
  ContactStore s;
  ASSERT_TRUE(s.begin(&be, &l, tight(be, 16)));
  add(s, mk(99), true);
  ASSERT_EQ(1u, l.evicted.size());
  EXPECT_NE(0, memcmp(l.evicted[0].data(), mk(0).id.pub_key, 32)) << "message history was lost in the restart";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
