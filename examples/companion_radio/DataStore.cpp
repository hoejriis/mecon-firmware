#include <Arduino.h>
#include "DataStore.h"
#include <helpers/iface/DeviceMeta.h>
#include <helpers/AtomicFile.h>

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(filename);
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "w");
#else
  return fs->open(filename, "w", true);
#endif
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _ContactsChannelsTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _ContactsChannelsTotalBlocks = _getContactsChannelsFS()->_getFS()->cfg->block_count;
  checkAdvBlobFile();
  #if defined(EXTRAFS) || defined(QSPIFLASH)
  migrateToSecondaryFS();
  #endif
#else
  // init 'blob store' support
  _fs->mkdir("/bl");
#endif
}

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <nvs_flash.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _ContactsChannelsTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
    return 0;
  }
  return size;
}
#endif

uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return SPIFFS.usedBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.usedBytes = 0;
  _fs->info(info);
  return info.usedBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());
  int usedBytes = config->block_size * usedBlockCount;
  return usedBytes / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return SPIFFS.totalBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.totalBytes = 0;
  _fs->info(info);
  return info.totalBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int totalBytes = config->block_size * config->block_count;
  return totalBytes / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(filename, "r");
#else
  return _fs->open(filename, "r", false);
#endif
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (_fsExtra == nullptr) {
    return _fs->format();
  } else {
    return _fs->format() && _fsExtra->format();
  }
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  bool fs_success = ((fs::SPIFFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs) {
  AtomicFile::recover(_fs, "/prefs.json");   // finish or drop a save that was cut by a power loss
  if (AtomicFile::exists(_fs, "/prefs.json")) {
    File file = openRead(_fs, "/prefs.json");
    if (file) {
      prefs.loadSerial(file);   // new Serial prefs
      file.close();
    }
  } else if (AtomicFile::exists(_fs, "/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs);
    if (savePrefs(prefs) ) {                // save to new format
      //_fs->remove("/new_prefs"); // remove old
    }
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs) {
  File file = openRead(_fs, filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&_prefs.node_lat, sizeof(_prefs.node_lat));                       // 40
    file.read((uint8_t *)&_prefs.node_lon, sizeof(_prefs.node_lon));                       // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read((uint8_t *)&_prefs._client_repeat, sizeof(_prefs._client_repeat));             // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));           // 78
    file.read(pad, 1);                                                                     // 79
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));       // 88
    file.read((uint8_t *)&_prefs.rx_boosted_gain, sizeof(_prefs.rx_boosted_gain));         // 89
    file.read((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));    // 90
    file.read((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));     // 121

    // migrate old fields
    _prefs.setRepeatEn(_prefs._client_repeat != 0);

    file.close();
  }
}

bool DataStore::bootDeviceMeta(mesh::RNG* rng, uint8_t device_id[16], uint32_t* epoch) {
  return Iface::bootDeviceMeta(_fs, rng, device_id, epoch);
}

bool DataStore::savePrefs(NodePrefs& _prefs) {
  // temp file + verify + swap: a power cut keeps the previous complete prefs
  return AtomicFile::save(_fs, "/prefs.json", [&_prefs](Stream& s) { return _prefs.saveSerial(s); });
}

void DataStore::loadContacts(DataStoreHost* host) {
    AtomicFile::recover(_getContactsChannelsFS(), "/contacts3");   // finish or drop a save that was cut by a power loss
    if (!AtomicFile::exists(_getContactsChannelsFS(), "/contacts3")) return;   // at boot: not in the listing (BootPresence.h)
    File file = openRead(_getContactsChannelsFS(), "/contacts3");
    if (file) {
      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c)) {
  // temp file + verify + swap: a power cut keeps the previous complete table
  AtomicFile::save(_getContactsChannelsFS(), "/contacts3", [host, filter](Stream& s) {
    uint32_t idx = 0;
    ContactInfo c;
    uint8_t unused = 0;

    while (host->getContactForSave(idx, c)) {
      if (filter && !filter(c)) {
        idx++;  // advance to next contact
        continue;
      }
      bool success = (s.write(c.id.pub_key, 32) == 32);
      success = success && (s.write((uint8_t *)&c.name, 32) == 32);
      success = success && (s.write(&c.type, 1) == 1);
      success = success && (s.write(&c.flags, 1) == 1);
      success = success && (s.write(&unused, 1) == 1);
      success = success && (s.write((uint8_t *)&c.sync_since, 4) == 4);
      success = success && (s.write((uint8_t *)&c.out_path_len, 1) == 1);
      success = success && (s.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
      success = success && (s.write(c.out_path, 64) == 64);
      success = success && (s.write((uint8_t *)&c.lastmod, 4) == 4);
      success = success && (s.write((uint8_t *)&c.gps_lat, 4) == 4);
      success = success && (s.write((uint8_t *)&c.gps_lon, 4) == 4);

      if (!success) return false; // write failed

      idx++;  // advance to next contact
    }
    return true;
  });
}

bool DataStore::legacyContactsExist() {
  AtomicFile::recover(_getContactsChannelsFS(), "/contacts3");
  return AtomicFile::exists(_getContactsChannelsFS(), "/contacts3");
}

bool DataStore::removeLegacyContacts() {
  return _getContactsChannelsFS()->remove("/contacts3");
}

// ---- contact store buckets (docs/CONTACTS.md): "/ct<N>", replaced through the same temp-verify-swap as prefs

static void bucketPath(char* dst, size_t n, uint16_t b) {
  snprintf(dst, n, "/ct%u", (unsigned)b);
}

bool DataStore::bucketExists(uint16_t b) {
  char path[16];
  bucketPath(path, sizeof(path), b);
  AtomicFile::recover(_getContactsChannelsFS(), path);
  return _getContactsChannelsFS()->exists(path);
}

// Parses "ct<N>" (suffix NULL) or "ct<N>.tmp" / "ct<N>.new" (suffix ".tmp" / ".new") from a directory entry name, which is
// "ct5" or "/ct5" depending on the file system (a nested path keeps only its last part). Returns the bucket or -1.
static int bucketFromName(const char* name, int* kind) {   // kind: 0 bucket file, 1 swap file, 2 anything else of ours (.bad)
  if (!name) return -1;
  const char* s = strrchr(name, '/');
  s = s ? s + 1 : name;
  if (s[0] != 'c' || s[1] != 't' || s[2] < '0' || s[2] > '9') return -1;
  long n = 0;
  const char* p = s + 2;
  while (*p >= '0' && *p <= '9' && n < 70000) n = n * 10 + (*p++ - '0');
  if (*p == 0) *kind = 0;
  else if (strcmp(p, ".tmp") == 0 || strcmp(p, ".new") == 0) *kind = 1;
  else if (strcmp(p, ".bad") == 0) *kind = 2;
  else return -1;
  return (int)n;
}

// Lists the contacts file system once. SPIFFS is flat (every file is in the root listing, whatever its name) and
// LittleFS / InternalFS keep these files in the root too; a nested layout would still be found by the last path part.
// Only a bucket with a leftover .tmp / .new gets AtomicFile::recover(); no other lookup is made for the rest.
bool DataStore::listBuckets(uint16_t limit, uint8_t* present) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  uint8_t* swap = (uint8_t*)calloc((limit + 7) / 8 + 1, 1);
  if (!swap) return false;
  bool listed = false;
  {
    File root = openRead(fs, "/");
    if (root && root.isDirectory()) {
      listed = true;
      for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        int kind = 0;
        int b = bucketFromName(f.name(), &kind);
        if (b < 0 || b >= limit) continue;
        if (kind == 0 && !f.isDirectory()) present[b >> 3] |= (uint8_t)(1u << (b & 7));
        else if (kind == 1) swap[b >> 3] |= (uint8_t)(1u << (b & 7));
      }
      root.close();
    }
  }
  if (listed) {
    for (int b = 0; b < limit; b++) {     // recovery runs after the listing, never while the directory is open
      if (!(swap[b >> 3] & (1u << (b & 7)))) continue;
      char path[16];
      bucketPath(path, sizeof(path), (uint16_t)b);
      AtomicFile::recover(fs, path);
      if (fs->exists(path)) present[b >> 3] |= (uint8_t)(1u << (b & 7));
      else present[b >> 3] &= (uint8_t)~(1u << (b & 7));
    }
  }
  free(swap);
  return listed;
}

bool DataStore::readBucket(uint16_t b, uint8_t* buf, uint32_t len) {
  char path[16];
  bucketPath(path, sizeof(path), b);
  File file = openRead(_getContactsChannelsFS(), path);
  if (!file) return false;
  bool ok = file.read(buf, len) == (int)len;
  file.close();
  return ok;
}

bool DataStore::writeBucket(uint16_t b, const uint8_t* buf, uint32_t len, bool is_new) {
  char path[16];
  bucketPath(path, sizeof(path), b);
  return AtomicFile::save(_getContactsChannelsFS(), path, [buf, len](Stream& s) { return s.write(buf, len) == len; }, is_new);
}

bool DataStore::removeBucket(uint16_t b) {
  char path[16];
  bucketPath(path, sizeof(path), b);
  return _getContactsChannelsFS()->remove(path);
}

bool DataStore::keepAside(uint16_t b, const uint8_t* buf, uint32_t len) {
  char path[24];
  snprintf(path, sizeof(path), "/ct%u.bad", (unsigned)b);
  return AtomicFile::save(_getContactsChannelsFS(), path, [buf, len](Stream& s) { return s.write(buf, len) == len; });
}

uint32_t DataStore::freeBytes() {
  uint32_t total = getStorageTotalKb();
  if (total == 0) return 0xFFFFFFFFu;           // this platform cannot say
  uint32_t used = getStorageUsedKb();
  return total > used ? (total - used) * 1024 : 0;
}

void DataStore::loadChannels(DataStoreHost* host) {
    AtomicFile::recover(_getContactsChannelsFS(), "/channels2");
    if (!AtomicFile::exists(_getContactsChannelsFS(), "/channels2")) return;   // at boot: not in the listing (BootPresence.h)
    File file = openRead(_getContactsChannelsFS(), "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  // temp file + verify + swap: a power cut keeps the previous complete channel table
  AtomicFile::save(_getContactsChannelsFS(), "/channels2", [host](Stream& file) {
    uint8_t channel_idx = 0;
    ChannelDetails ch;
    uint8_t unused[4];
    memset(unused, 0, 4);

    while (host->getChannelForSave(channel_idx, ch)) {
      bool success = (file.write(unused, 4) == 4);
      success = success && (file.write((uint8_t *)ch.name, 32) == 32);
      success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

      if (!success) return false; // write failed
      channel_idx++;
    }
    return true;
  });
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

#define MAX_ADVERT_PKT_LEN   (2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE)

struct BlobRec {
  uint32_t timestamp;
  uint8_t  key[7];
  uint8_t  len;
  uint8_t  data[MAX_ADVERT_PKT_LEN];
};

void DataStore::checkAdvBlobFile() {
  AtomicFile::recover(_getContactsChannelsFS(), "/adv_blobs");   // finish or drop a save that was cut by a power loss
  if (!AtomicFile::exists(_getContactsChannelsFS(), "/adv_blobs")) {
    File file = openWrite(_getContactsChannelsFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      for (int i = 0; i < MAX_BLOBRECS; i++) {     // pre-allocate to fixed size
        file.write((uint8_t *) &zeroes, sizeof(zeroes));
      }
      file.close();
    }
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts3 and channels2 files to secondary FS if they don't already exist
  if (!_fsExtra->exists("/adv_blobs")) {
    if (_fs->exists("/adv_blobs")) {
    File oldAdvBlobs = openRead(_fs, "/adv_blobs");
    File newAdvBlobs = openWrite(_fsExtra, "/adv_blobs");

    if (oldAdvBlobs && newAdvBlobs) {
      BlobRec rec;
      size_t count = 0;

      // Copy 20 BlobRecs from old to new
      while (count < 20 && oldAdvBlobs.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
        newAdvBlobs.seek(count * sizeof(BlobRec));
        newAdvBlobs.write((uint8_t *)&rec, sizeof(rec));
        count++;
      }
    }
    if (oldAdvBlobs) oldAdvBlobs.close();
    if (newAdvBlobs) newAdvBlobs.close();
    _fs->remove("/adv_blobs");
    }
  }
  if (!_fsExtra->exists("/contacts3")) {
    if (_fs->exists("/contacts3")) {
      File oldFile = openRead(_fs, "/contacts3");
      File newFile = openWrite(_fsExtra, "/contacts3");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/contacts3");
    }
  }
  if (!_fsExtra->exists("/channels2")) {
    if (_fs->exists("/channels2")) {
      File oldFile = openRead(_fs, "/channels2");
      File newFile = openWrite(_fsExtra, "/channels2");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/channels2");
    }
  }
  // cleanup nodes which have been testing the extra fs, copy _main.id and new_prefs back to primary
  if (_fsExtra->exists("/_main.id")) {
      if (_fs->exists("/_main.id")) {_fs->remove("/_main.id");}
      File oldFile = openRead(_fsExtra, "/_main.id");
      File newFile = openWrite(_fs, "/_main.id");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    if (_fs->exists("/new_prefs")) {_fs->remove("/new_prefs");}
      File oldFile = openRead(_fsExtra, "/new_prefs");
      File newFile = openWrite(_fs, "/new_prefs");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/new_prefs");
  }
  // remove files from where they should not be anymore
  if (_fs->exists("/adv_blobs")) {
    _fs->remove("/adv_blobs");
  }
  if (_fs->exists("/contacts3")) {
    _fs->remove("/contacts3");
  }
  if (_fs->exists("/channels2")) {
    _fs->remove("/channels2");
  }
  if (_fsExtra->exists("/_main.id")) {
    _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    _fsExtra->remove("/new_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
  uint8_t len = 0;  // 0 = not found
  if (file) {
    BlobRec tmp;
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        len = tmp.len;
        memcpy(dest_buf, tmp.data, len);
        break;
      }
    }
    file.close();
  }
  return len;
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  if (len < PUB_KEY_SIZE+4+SIGNATURE_SIZE || len > MAX_ADVERT_PKT_LEN) return false;
  checkAdvBlobFile();
  FILESYSTEM* fs = _getContactsChannelsFS();
  uint32_t found_pos = 0;
  {
    File file = openRead(fs, "/adv_blobs");
    if (!file) return false; // error
    uint32_t pos = 0;
    uint32_t min_timestamp = 0xFFFFFFFF;

    // search for matching key OR evict by oldest timestamp
    BlobRec tmp;
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        found_pos = pos;
        break;
      }
      if (tmp.timestamp < min_timestamp) {
        min_timestamp = tmp.timestamp;
        found_pos = pos;
      }

      pos += sizeof(tmp);
    }
    file.close();
  }

  BlobRec upd;
  memset(&upd, 0, sizeof(upd));
  memcpy(upd.key, key, sizeof(upd.key));  // just record 7 byte prefix of key
  memcpy(upd.data, src_buf, len);
  upd.len = len;
  upd.timestamp = _clock->getCurrentTime();

  // The records are fixed size and rewritten in place by stock, so a power cut could leave a short or torn
  // file. Write the whole table to a temp file with the one record replaced, verify it, swap it in.
  return AtomicFile::save(fs, "/adv_blobs", [this, fs, found_pos, &upd](Stream& out) {
    File old = openRead(fs, "/adv_blobs");
    BlobRec rec;
    for (int i = 0; i < MAX_BLOBRECS; i++) {
      const BlobRec* w = &rec;
      if ((uint32_t)i * sizeof(BlobRec) == found_pos) {
        w = &upd;
      } else if (!old || old.read((uint8_t *) &rec, sizeof(rec)) != sizeof(rec)) {
        memset(&rec, 0, sizeof(rec));
      }
      if (w == &upd && old) old.seek((i + 1) * sizeof(BlobRec));   // keep the read position in step
      if (out.write((const uint8_t *) w, sizeof(BlobRec)) != sizeof(BlobRec)) { if (old) old.close(); return false; }
    }
    if (old) old.close();
    return true;
  });
}
bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  return true; // this is just a stub on NRF52/STM32 platforms
}
#else
inline void makeBlobPath(const uint8_t key[], int key_len, char* path, size_t path_size) {
  char fname[18];
  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  AtomicFile::recover(_fs, path);   // finish or drop a save that was cut by a power loss
  if (_fs->exists(path)) {
    File f = openRead(_fs, path);
    if (f) {
      int len = f.read(dest_buf, 255); // currently MAX 255 byte blob len supported!!
      f.close();
      return len;
    }
  }
  return 0; // not found
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  // temp file, read-back check, swap: a power cut keeps the previous complete blob (stock rewrote it in place)
  return AtomicFile::save(_fs, path, [src_buf, len](Stream& s) { return s.write(src_buf, len) == len; });
}

bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  AtomicFile::discard(_fs, path);

  return true; // return true even if file did not exist
}
#endif
