#pragma once

#include <helpers/Posture.h>
#include <helpers/cli/CliMqtt.h>
#include <helpers/ContactPlatform.h>
#include <Arduino.h>
#include <Mesh.h>

/*------------ Frame Protocol --------------*/
#define FIRMWARE_VER_CODE 14

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "15 Oct 2026"
#endif

#include <helpers/MeconVersion.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION mecon::versionShort()
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>
#ifdef ENABLE_MECON_IFACE
  #include "MeconIface.h"
#endif

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

// Copied from simple_repeater
#define CTL_TYPE_NODE_DISCOVER_REQ      0x80
#define CTL_TYPE_NODE_DISCOVER_RESP     0x90

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  class Listener {
    public:
      virtual void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) = 0;
      virtual void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) = 0;
      virtual void onQueueSizeChanged(int msgcount) = 0;
      virtual void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) { }
      virtual void onControlDataRecv(const mesh::Packet* pkt) { }
      virtual void onChannelDataRecv(mesh::Packet *pkt, const mesh::GroupChannel &channel, uint16_t data_type,
                                     const uint8_t *data, size_t data_len) { }
      virtual void onACKRecv(uint32_t ack_crc) { }
      virtual uint8_t onUnhandledRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                         uint8_t len, uint8_t *reply) { return 0; /* unknown request type */ }
      virtual bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) { return false; /* not handled */ }
      virtual void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                               const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) { }
      virtual void onRawDataRecv(mesh::Packet *pkt) { }
      virtual ~Listener() { }
  };

  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store);

  void begin();
  void startInterface(BaseSerialInterface &serial);
  void setListener(Listener* listener) { _listener = listener; }

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  uint32_t getBLEPin();
  void setBLEPin(uint32_t active_pin);

  void loop();
#ifdef ENABLE_MECON_IFACE
  // For the device interface (MeconIface): read-only facts, and frame output to one transport of the multiplexed serial.
  const char* hardwareName() const { return board.getManufacturerName(); }
  uint16_t batteryMilliVolts() { return board.getBattMilliVolts(); }
  uint8_t postureValue() const { return _prefs.posture; }
  MeconIface& iface() { return _iface; }
  bool ifaceWritable(const BaseSerialInterface* src) const { return _serial && !_serial->isWriteBusy() && src; }
  void ifaceWrite(const BaseSerialInterface* src, const uint8_t* frame, size_t len) {
    _serial->bindReply(src);
    _serial->writeFrame(frame, len);
    _serial->bindReply(nullptr);
  }
#endif
#ifdef ENABLE_CONNMODE
  // Connectivity mode (ConnMode.h): the mode the device booted into, and a switch asked for by the device button.
  // The switch saves the prefs and restarts shortly after, so the BLE and Wi-Fi stacks are never both up.
  uint8_t getConnectivityMode() { return ConnMode::sanitize(_prefs.connectivity_mode); }
  bool requestConnectivityMode(uint8_t want, uint8_t caller);
  bool connectivityRestartPending() const { return _connmode_restart_at != 0; }
#endif
  void handleCmdFrame(size_t len);
  bool advert();
  void enterCLIRescue();

  int  getRecentlyHeard(AdvertPath dest[], int max_num);

protected:
  float getAirtimeBudgetFactor() const override;
  // the channel last applied to the radio (not merely stored in prefs), for the regulatory duty-cycle floor
  bool getRadioChannel(float& freq_mhz, float& bw_khz) const override {
    freq_mhz = _radio_freq; bw_khz = _radio_bw;
    return _radio_freq > 0;
  }
  int getInterferenceThreshold() const override;
  bool getCADEnabled() const override;
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  uint8_t getAutoAddMaxHops() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text, char* reply) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override;
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override { return setChannel(channel_idx, ch); }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  // Contact memory (docs/CONTACTS.md)
  enum { MIGRATE_NONE = 0, MIGRATE_BULK, MIGRATE_MERGE };
  uint8_t _migrate = MIGRATE_NONE;
  void loadContactsAtBoot();
  void migrateLegacyContacts();
  void reloadContactSecrets();

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = 0;
  }

public:
  void savePrefs() {
    _prefs.node_lat = sensors.node_lat;
    _prefs.node_lon = sensors.node_lon;
    _store->savePrefs(_prefs);
    _prefs.clearDirty();
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled ? "1" : "0");
    if (_prefs.gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _prefs.gps_interval);
      sensors.setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  // identity.rotate (docs/contract/AUTHORITY.md section 11): a new node key from the cryptographic RNG, saved atomically,
  // then swapped in; contacts, the message queue and everything cached from the old key are dropped; one self-advert.
  // Configuration, channels, the Deployment record, posture and device_id stay. 0: nothing changed (*err says why);
  // 1: done; 2: the key is swapped but the contacts could not all be dropped.
  // device_id_hex and job_id go into the rotation statement (the old key's signed statement of the change, stored before the
  // swap and served by identity.rotation, so a manager that lost the result can still learn the new key).
  int rotateIdentity(const char** err, const char* device_id_hex, uint32_t job_id);
  // The stored statement: its signed text and the base64 signature. False when there is none.
  bool readRotation(char* text, size_t tcap, char* sig_b64, size_t scap);

  // One command line from the broker (docs/CLI.md). `ctx` says how the session was authenticated; the engine's table
  // decides whether the line may run. Returns a Cli::Mqtt::Status.
  uint8_t runMqttCli(const Cli::Context& ctx, const char* cmd, char* reply, size_t cap);

private:
  // Boot: a statement whose new key is not the current key never took effect and is deleted; one that is the current key
  // and still marked drop-pending finishes the drop of the old key's contacts and queue.
  void recoverRotation();
  void recoverRotationAtBoot();                 // on nRF52 on the worker (#172)
  static void recoverRotationJob(void* self);
  void recoverRotationOnLoop();
  bool dropOldKeyState();
  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  void writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  void addToOfflineQueue(const uint8_t frame[], int len);
  int getFromOfflineQueue(uint8_t frame[]);
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  bool rescueFilesLocked();
  bool handleCommand(const char* text, uint32_t sender_timestamp, char* reply);
  void checkSerialInterface();
  bool isValidClientRepeatFreq(uint32_t f) const;

  // helpers, short-cuts
  bool saveChannels() { return _store->saveChannels(this); }
  void saveContacts();

  DataStore* _store;
  NodePrefs _prefs;
  uint32_t pending_login;
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  Listener* _listener;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;
  const BaseSerialInterface* _cmd_src = nullptr;   // the interface the command being handled arrived on (multiplexed serial)
  const BaseSerialInterface* _iter_src = nullptr;  // ... and the one the running contact list is streamed to
  bool _cli_rescue;
  bool send_unscoped;   // force un-scoped flood (instead of using send_scope)
  char cli_command[80];
#ifdef ENABLE_CONNMODE
  uint32_t _connmode_restart_at = 0;   // millis() deadline of the restart after a mode switch (0 = none)
  void scheduleConnectivityRestart();
#endif
  uint8_t _cli_caller = Posture::CALLER_NET;   // who is behind a handleCommand(); set by each adapter, fails closed
  Cli::Context _cli_ctx;                       // the same caller as a context for the engine's gate; T_NONE until an adapter sets it
  bool _cli_refused = false;                   // the gate refused the last handleCommand()
  Cli::Context serialContext() const;
  uint8_t frameGate(size_t len);
  bool serialTrusted() const;
#ifdef ENABLE_MECON_IFACE
  friend class MeconIface;
  MeconIface _iface;
#endif
  void setCliCaller(const Cli::Context& ctx, uint8_t caller);
  void recordFactoryReset(uint8_t caller);
  char reply_buf[166];
  float _radio_freq = 0, _radio_bw = 0;
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;
  uint8_t _contacts_retries;      // retries of the delayed contacts save since its last good write

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];

    bool isChannelMsg() const;
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    ContactInfo* contact;
  };
  #define EXPECTED_ACK_TABLE_SIZE 8
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;

  #define ADVERT_PATH_TABLE_SIZE   16
  AdvertPath advert_paths[ADVERT_PATH_TABLE_SIZE]; // circular table
};

extern MyMesh the_mesh;
