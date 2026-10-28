#pragma once

#include <helpers/IdentityStore.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include <helpers/ContactStore.h>
#include "NodePrefs.h"

class DataStoreHost {
public:
  virtual bool onContactLoaded(const ContactInfo& contact) =0;
  virtual bool getContactForSave(uint32_t idx, ContactInfo& contact) =0;
  virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) =0;
  virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) =0;
};

class DataStore : public ContactBackend {
  FILESYSTEM* _fs;
  FILESYSTEM* _fsExtra;
  mesh::RTCClock* _clock;
  IdentityStore identity_store;

  void loadPrefsInt(const char *filename, NodePrefs& prefs);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  void checkAdvBlobFile();
#endif

public:
  DataStore(FILESYSTEM& fs, mesh::RTCClock& clock);
  DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock);
  void begin();
  bool formatFileSystem();
  FILESYSTEM* getPrimaryFS() const { return _fs; }
  FILESYSTEM* getSecondaryFS() const { return _fsExtra; }
  bool loadMainIdentity(mesh::LocalIdentity &identity);
  bool saveMainIdentity(const mesh::LocalIdentity &identity);
  // The rotation statement (docs/contract/AUTHORITY.md section 11), one small file replaced atomically. load returns the
  // length read, 0 when there is none.
  size_t loadRotation(uint8_t* buf, size_t cap);
  bool saveRotation(const uint8_t* buf, size_t len);
  void removeRotation();
  // The device interface's identity and boot counter (docs/contract/DEVICE_INTERFACE.md): device_id is created on the
  // first boot and kept; epoch is incremented and saved on every call (once per boot). False when the file could
  // not be written: the values are still usable for this boot.
  bool bootDeviceMeta(mesh::RNG* rng, uint8_t device_id[16], uint32_t* epoch);
  // The Deployment record's two copies (src/helpers/deployment/DepRecord.h), slot 0 = A, 1 = B. A read reports
  // whether the file exists separately from whether it could be read whole: an existing copy that cannot be read
  // makes the record corrupt, never absent.
  // which: DEP_RECORD, or DEP_MARKS for the sequence marks' own two copies (DepRecord.h).
  enum DepFile : uint8_t { DEP_RECORD = 0, DEP_MARKS = 1 };
  bool readDeploymentCopy(int slot, uint8_t* buf, size_t cap, size_t* len, bool* exists, DepFile which = DEP_RECORD);
  bool writeDeploymentCopy(int slot, const uint8_t* frame, size_t len, DepFile which = DEP_RECORD);
  // Removes both copies, the older first (newer_slot: the slot that loaded as the winner, -1 when none did), with any
  // .tmp / .new beside them.
  bool removeDeploymentCopies(int newer_slot, DepFile which = DEP_RECORD);
  void loadPrefs(NodePrefs& prefs);
  bool savePrefs(NodePrefs& prefs);
  // The stock whole-table file (/contacts3): read for the migration into the contact store, and kept as the
  // persistence of the CONTACTS_STOCK_ARRAY fallback, where it is now saved atomically.
  void loadContacts(DataStoreHost* host);
  void saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c) = NULL);
  bool legacyContactsExist();
  bool removeLegacyContacts();
  // ContactBackend: the contact store's bucket files (/ct<N>), each replaced atomically
  bool bucketExists(uint16_t b) override;
  bool listBuckets(uint16_t limit, uint8_t* present) override;   // one directory pass at boot
  bool readBucket(uint16_t b, uint8_t* buf, uint32_t len) override;
  bool writeBucket(uint16_t b, const uint8_t* buf, uint32_t len, bool is_new) override;
  bool removeBucket(uint16_t b) override;
  bool keepAside(uint16_t b, const uint8_t* buf, uint32_t len) override;   // /ct<N>.bad
  uint32_t freeBytes() override;
  void loadChannels(DataStoreHost* host);
  void saveChannels(DataStoreHost* host);
  void migrateToSecondaryFS();
  uint8_t getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]);
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len);
  bool deleteBlobByKey(const uint8_t key[], int key_len);
  File openRead(const char* filename);
  File openRead(FILESYSTEM* fs, const char* filename);
  bool removeFile(const char* filename);
  bool removeFile(FILESYSTEM* fs, const char* filename);
  uint32_t getStorageUsedKb() const;
  uint32_t getStorageTotalKb() const;

private:
  FILESYSTEM* _getContactsChannelsFS() const { if (_fsExtra) return _fsExtra; return _fs;};
};
