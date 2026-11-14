#include "MyMesh.h"
#include <helpers/PassBlame.h>
#include <helpers/net/NetCommands.h>
#include <helpers/Uptime.h>
#include <helpers/iface/Observe.h>
#include <helpers/iface/DeviceId.h>
#include <helpers/iface/DevIdSig.h>
#include <helpers/iface/Provenance.h>
#include <helpers/iface/Wipe.h>
#include <helpers/AuditStore.h>
#include <helpers/LazySaveRetry.h>
#include <helpers/CryptoRNG.h>
#include <helpers/NrfWorker.h>

#include <Arduino.h> // needed for PlatformIO
#ifdef ENABLE_WIFI_INTERFACE
#include <WiFi.h>
#include <helpers/wifi/WifiController.h>
#ifdef ENABLE_MQTT
#include <helpers/mqtt/MqttController.h>
#endif
#endif
#include <Mesh.h>

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65
#define CMD_RUN_CLI_COMMAND           66  // v14+

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28
#define RESP_CODE_CLI_REPLY           29  // v14+, a reply to CMD_RUN_CLI_COMMAND

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6
#define ERR_CODE_NOT_PERMITTED          7  // command refused on this transport (or for this caller)

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)

// A contact's flags that grant it something: telemetry (base, location, environment) and the remote CLI.
#define CONTACT_GRANT_FLAGS       0x1E
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return _prefs.interference_threshold;
}
bool MyMesh::getCADEnabled() const {
  return _prefs.cad_enabled;
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  Iface::observe(snr, rssi, raw, len);   // the observations stream, when the device interface is built in
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::markContactsDirty() { dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); }

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  if (_serial && _serial->isConnected()) {   // the store can evict at boot, before startInterface sets _serial (#223)
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial && _serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (_listener) _listener->onDiscoveredContact(contact, is_new, path_len, path);

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  if (_listener) {
    uint32_t ack_crc;
    memcpy(&ack_crc, data, 4);
    _listener->onACKRecv(ack_crc);
  }
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
#ifdef ENABLE_MECON_IFACE
  {
    uint32_t ack_code;
    uint8_t to[6];
    memcpy(&ack_code, data, 4);
    if (_iface.messages() && _iface.messages()->onAck(ack_code, to)) {
      ContactInfo* from = lookupContactByPubKey(to, sizeof(to));
      if (from) return from;   // as for a table entry above: the caller stops this ACK here, and may learn a path
    }
  }
#endif
  return checkConnectionsAck(data);
}

#ifdef ENABLE_MECON_IFACE
void MyMesh::onMessagePacket(const mesh::Packet* pkt) {
  if (_msg_capture) const_cast<mesh::Packet*>(pkt)->calculatePacketHash(_msg_capture->hash);
}

void MyMesh::logTx(mesh::Packet* packet, int len) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  _lora_tx.sent(hash, millis());
  if (!_iface.messages() || !_iface.messages()->anyOpen()) return;
  _iface.messages()->onTx(hash, millis());
}

void MyMesh::logTxFail(mesh::Packet* packet, int len) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  _lora_tx.failed(hash);
  if (!_iface.messages() || !_iface.messages()->anyOpen()) return;
  _iface.messages()->onTxFail(hash);
}

void MyMesh::onDutyHold(mesh::Packet* packet, uint32_t wait_ms) {
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  _lora_tx.held(hash);
  if (!_iface.messages() || !_iface.messages()->anyOpen()) return;
  _iface.messages()->onHeld(hash, wait_ms);
}

// The native send, as CMD_SEND_TXT_MSG makes it, less the app's own ACK table: the ACK is the outcome table's, so a stock
// app is not told of a message it did not send. No contact is created for a recipient that is not stored.
MyMesh::ScopedSend::ScopedSend(MyMesh& mesh, const Iface::SendScope& s) : m(mesh), scope(mesh.send_scope), unscoped(mesh.send_unscoped) {
  if (s.mode == Iface::SendScope::UNSCOPED) m.send_unscoped = true;
  if (s.mode != Iface::SendScope::NAMED) return;
  m.send_unscoped = false;
  memcpy(m.send_scope.key, s.key, sizeof(m.send_scope.key));
}
MyMesh::ScopedSend::~ScopedSend() {
  m.send_scope = scope;
  m.send_unscoped = unscoped;
  memset(scope.key, 0, sizeof(scope.key));
}

Iface::MessageSend MyMesh::messageSendDirect(const uint8_t key[32], uint32_t ts, uint8_t attempt, const char* text,
                                             const Iface::SendScope& scope, Iface::MessageSent& out) {
  ContactInfo* to = contactStore().find(key, PUB_KEY_SIZE);
  if (!to) return Iface::MS_NOT_FOUND;
  uint32_t est = 0;
  _msg_capture = &out;
  ScopedSend scoped(*this, scope);
  const int r = sendMessage(*to, ts, attempt, text, out.ack, est);
  _msg_capture = nullptr;
  if (r == MSG_SEND_FAILED) return Iface::MS_QUEUE_FULL;
  out.est_ms = est;
  out.flood = r == MSG_SEND_SENT_FLOOD;
  return Iface::MS_OK;
}

// As CMD_SEND_CHANNEL_TXT_MSG makes it. The native send cuts a text that does not fit beside the sender's name; here that
// is an answer instead, since a manager that asked for the whole text must not believe it went.
Iface::MessageSend MyMesh::messageSendChannel(int slot, uint32_t ts, const char* text, size_t len, const Iface::SendScope& scope,
                                              Iface::MessageSent& out) {
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails ch;
  if (!getChannel(slot, ch) || !ch.name[0]) return Iface::MS_NOT_FOUND;
  if (strlen(_prefs.node_name) + 2 + len > MAX_TEXT_LEN) return Iface::MS_TOO_LONG;
  _msg_capture = &out;
  ScopedSend scoped(*this, scope);
  const bool sent = sendGroupMessage(ts, ch.channel, _prefs.node_name, text, (int)len);
  _msg_capture = nullptr;
  if (!sent) return Iface::MS_QUEUE_FULL;
  out.ack = 0;
  out.est_ms = 0;
  out.flood = true;
  return Iface::MS_OK;
#else
  return Iface::MS_NOT_FOUND;
#endif
}
#endif

#ifdef ENABLE_MECON_IFACE
// The manager's copy (OPERATIONS.md section 6.4). The native queue below is the stock app's and is not touched by it.
// False when a manager already delivered this message: the app has it, so it is not queued a second time.
bool MyMesh::keepReceived(mesh::Packet* pkt, const uint8_t* from, int channel, uint8_t txt_type, uint32_t ts,
                          const uint8_t* signer, const char* text) {
  const size_t full = strlen(text);
  const uint32_t id = Iface::Inbox::identity(from, channel, ts, text, full);
  const bool again = _iface.inbox().wasDelivered(id);
  _iface.inbox().remember(id, again);
  if (again) return false;
  Iface::InboxEntry* e = _iface.inbox().add();
  if (!e) return true;
  pkt->calculatePacketHash(e->hash);
  e->ts = ts;
  e->rx_ts = getRTCClock()->knownTime();
  e->rx_ms = millis();
  if (from) memcpy(e->from, from, sizeof(e->from));
  if (signer) memcpy(e->signer, signer, sizeof(e->signer));
  e->channel = (int8_t)channel;
  e->snr_x4 = (int8_t)(pkt->getSNR() * 4);
  e->path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  e->type = txt_type;
  size_t n = full;
  e->cut = n > Iface::InboxEntry::TEXT_CAP;
  if (e->cut) n = Iface::InboxEntry::TEXT_CAP;
  memcpy(e->text, text, n);
  e->len = (uint8_t)n;
  return true;
}

// message.deliver (OPERATIONS.md section 6.5): a message the device missed, on the native receive queue as a stock app
// reads one, with the marker in its text because the frame has no field to say how it came. Nothing goes on the radio and
// no ACK is made.
Iface::DeliverResult MyMesh::messageDeliver(const uint8_t* from, int channel, uint32_t ts, const char* text, size_t len) {
  static const char MARK[] = "[via manager] ";
  const size_t mark = sizeof(MARK) - 1;
  uint8_t prefix[6];
  if (from) {
    ContactInfo* c = lookupContactByPubKey(from, PUB_KEY_SIZE);
    if (!c) return Iface::DELIVER_NOT_FOUND;
    memcpy(prefix, from, sizeof(prefix));
  } else {
#ifdef MAX_GROUP_CHANNELS
    ChannelDetails ch;
    if (!getChannel(channel, ch) || !ch.name[0]) return Iface::DELIVER_NOT_FOUND;
#else
    return Iface::DELIVER_NOT_FOUND;
#endif
  }
  const uint32_t id = Iface::Inbox::identity(from, channel, ts, text, len);
  if (_iface.inbox().wasHeard(id)) return Iface::DELIVER_DUPLICATE;
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = from ? RESP_CODE_CONTACT_MSG_RECV_V3 : RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = 0;   // no reception, so no SNR
    out_frame[i++] = 0;
    out_frame[i++] = 0;
  } else {
    out_frame[i++] = from ? RESP_CODE_CONTACT_MSG_RECV : RESP_CODE_CHANNEL_MSG_RECV;
  }
  if (from) {
    memcpy(&out_frame[i], prefix, sizeof(prefix));
    i += sizeof(prefix);
  } else {
    out_frame[i++] = (uint8_t)channel;
  }
  out_frame[i++] = 0xFF;   // no path: it did not come over the air
  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &ts, 4);
  i += 4;
  if ((size_t)i + mark + len > MAX_FRAME_SIZE) return Iface::DELIVER_TOO_LONG;   // refused rather than cut
  bool room = offline_queue_len < OFFLINE_QUEUE_SIZE;
  for (int q = 0; q < offline_queue_len && !room; q++) room = offline_queue[q].isChannelMsg();
  if (!room) return Iface::DELIVER_QUEUE_FULL;   // the native queue would drop it without a word
  memcpy(&out_frame[i], MARK, mark);
  i += mark;
  memcpy(&out_frame[i], text, len);
  i += len;
  addToOfflineQueue(out_frame, i);
  _iface.inbox().remember(id, true);
  if (Iface::InboxEntry* e = _iface.inbox().add()) {
    e->ts = ts;
    const uint32_t now = getRTCClock()->getCurrentTime();
    e->rx_ts = now >= 1704067200 ? now : 0;
    e->rx_ms = millis();
    if (from) memcpy(e->from, from, sizeof(e->from));
    e->channel = (int8_t)channel;
    e->path_len = 0xFF;
    e->type = TXT_TYPE_PLAIN;
    e->from_manager = true;
    const size_t n = len > Iface::InboxEntry::TEXT_CAP ? Iface::InboxEntry::TEXT_CAP : len;
    e->cut = n < len;
    memcpy(e->text, text, n);
    e->len = (uint8_t)n;
  }
  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING;
    _serial->writeFrame(frame, 1);
  }
  if (_listener) _listener->onQueueSizeChanged(offline_queue_len);
  return Iface::DELIVER_OK;
}
#endif

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
#ifdef ENABLE_MECON_IFACE
  if (!keepReceived(pkt, from.id.pub_key, -1, txt_type, sender_timestamp, extra_len >= 4 ? extra : nullptr, text)) return;
#endif
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

  if (_listener) {
    _listener->onMessageRecv(pkt, from, txt_type, sender_timestamp, text);
    _listener->onQueueSizeChanged(offline_queue_len);
  }
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  return _prefs.isRepeatEn();
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text, char* reply) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  if (from.isRemoteCLIAllowed()) {
    // The flag makes the contact a remote CLI caller; what it may then run (reads and reboot, nothing at all in
    // Hardened) is the engine's table. sender_timestamp == 0 means "local console" to handleCommand(), and the
    // peer chooses this value: never let a remote caller present as local.
    setCliCaller(Cli::Context(Cli::T_LORA, Cli::A_CONTACT_FLAG, _prefs.posture), Posture::CALLER_REMOTE_CLI);
    if (!handleCommand(text, sender_timestamp ? sender_timestamp : 1, reply)) {
      strcat(reply, "Unknown command");   // reply may have cmd prefix from 'text'
    }
    setCliCaller(Cli::Context(), Posture::CALLER_NET);
  } else {
    queueMessage(from, TXT_TYPE_CLI_COMMAND, pkt, sender_timestamp, NULL, 0, text);
  }
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
#ifdef ENABLE_MECON_IFACE
  // It was decrypted with a stored channel, so there is a slot to name; with none, -1 would read as a direct message.
  if (channel_idx < 127 && !keepReceived(pkt, nullptr, channel_idx, TXT_TYPE_PLAIN, timestamp, nullptr, text)) return;
#endif
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    // Get the channel name from the channel index
    ChannelDetails channel_details;
    if (!getChannel(channel_idx, channel_details)) {
      strcpy(channel_details.name, "Unknown");
    }
    _listener->onChannelMessageRecv(pkt, channel_details, text);
    _listener->onQueueSizeChanged(offline_queue_len);
  }
}

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
#ifdef ENABLE_MECON_IFACE
  if (data_type == Iface::LORA_CHANNEL_TYPE) {   // the device's own frames: never passed to the app as channel data
    if (_iface.onLoraChannel(findChannelIdx(channel), data, data_len)) pkt->markDoNotRetransmit();   // it has arrived
    return;
  }
#endif
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    _listener->onChannelDataRecv(pkt, channel, data_type, data, data_len);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      float temperature = board.getMCUTemperature();
      if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
        telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
      }

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  } else if (_listener) {
    return _listener->onUnhandledRequest(contact, sender_timestamp, data, len, reply);
  }
  return 0; // unknown
}

// A device-interface frame over LoRa (LORA_MANAGEMENT.md). Native anonymous requests keep what they do: only the marker comes here.
void MyMesh::onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) {
#ifdef ENABLE_MECON_IFACE
  if (len > Iface::LORA_ANON_TAG + Iface::LORA_HDR && data[Iface::LORA_ANON_TAG] == Iface::LORA_MARKER && Iface::loraSink().fn) {
    uint8_t back[64];
    const uint8_t back_len = Iface::loraReplyPath(packet->isRouteFlood(), packet->getPathHashSize(), packet->getPathHashCount(),
                                                  packet->path, back, sizeof(back));
    Iface::loraSink().fn(Iface::loraSink().user, sender.pub_key, secret, back, back_len, data + Iface::LORA_ANON_TAG,
                         len - Iface::LORA_ANON_TAG);
  } else if (len > Iface::LORA_ANON_TAG && data[Iface::LORA_ANON_TAG] == Iface::REPORT_MARKER) {
    _iface.onRfReport(sender.pub_key, PUB_KEY_SIZE, data + Iface::LORA_ANON_TAG, len - Iface::LORA_ANON_TAG);
  }
#else
  (void)packet; (void)secret; (void)sender; (void)data; (void)len;
#endif
}

#ifdef ENABLE_MECON_IFACE
bool MyMesh::loraReply(const uint8_t* key, const uint8_t* secret, const uint8_t* path, uint8_t path_len, const uint8_t* frame, size_t n,
                       bool flood_ok) {
  if (path_len == Iface::LoraLink::PATH_UNKNOWN && !flood_ok) return true;   // never a flood for an unverified answer: dropped
  mesh::Identity to(key);
  mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, to, secret, frame, n);
  if (!reply) return false;
  traceQueued(reply, n > 5 ? frame[5] : 0xFF);   // a reply frame: magic, xid, index, count
  if (path_len != Iface::LoraLink::PATH_UNKNOWN) sendDirect(reply, path, path_len, 300);
  else sendFlood(reply, 300);
  return true;
}

// A frame on a channel slot, with its two addresses, as a flood on that channel.
bool MyMesh::loraChannelSend(int slot, const uint8_t* to_key, const uint8_t* frame, size_t n) {
  ChannelDetails ch;
  if (n > 160 || !getChannel(slot, ch)) return false;
  uint8_t temp[3 + Iface::LORA_CH_ADDR + 160];
  temp[0] = (uint8_t)(Iface::LORA_CHANNEL_TYPE & 0xFF);
  temp[1] = (uint8_t)(Iface::LORA_CHANNEL_TYPE >> 8);
  temp[2] = (uint8_t)(Iface::LORA_CH_ADDR + n);
  memcpy(temp + 3, to_key, Iface::LORA_CH_PREFIX);
  memcpy(temp + 3 + Iface::LORA_CH_PREFIX, self_id.pub_key, Iface::LORA_CH_PREFIX);
  static uint8_t send_tag = 0;
  temp[3 + Iface::LORA_CH_ADDR - 1] = ++send_tag;
  memcpy(temp + 3 + Iface::LORA_CH_ADDR, frame, n);
  mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_DATA, ch.channel, temp, 3 + Iface::LORA_CH_ADDR + n);
  if (!pkt) return false;
  sendFlood(pkt, 300);
  return true;
}

bool MyMesh::rfSend(const uint8_t* to, const uint8_t* frame, size_t n) {
  ContactInfo* recipient = lookupContactByPubKey(to, PUB_KEY_SIZE);
  ContactInfo anon;
  if (!recipient) {   // as CMD_SEND_ANON_REQ: a node that is not a contact, tried directly
    memset(&anon, 0, sizeof(anon));
    memcpy(anon.id.pub_key, to, PUB_KEY_SIZE);
    anon.out_path_len = 0;
    anon.type = ADV_TYPE_NONE;
    anon.lastmod = getRTCClock()->getCurrentTime();
    if (!addContact(anon)) return false;
    recipient = &anon;
  }
  // As sendAnonReq, with the packet in hand so the trace knows it (#542)
  uint8_t temp[MAX_PACKET_PAYLOAD];
  if (4 + n > sizeof(temp)) return false;
  const uint32_t tag = getRTCClock()->getCurrentTimeUnique();
  memcpy(temp, &tag, 4);
  memcpy(&temp[4], frame, n);
  mesh::Packet* pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, recipient->id, recipient->getSharedSecret(self_id), temp, 4 + n);
  if (!pkt) return false;
  traceQueued(pkt, n > 2 ? frame[2] : 0xFF);   // a request frame: marker, xid, index, count
  if (recipient->out_path_len == OUT_PATH_UNKNOWN) sendFloodScoped(*recipient, pkt);
  else sendDirect(pkt, recipient->out_path, recipient->out_path_len);
  return true;
}

void MyMesh::traceQueued(mesh::Packet* pkt, uint8_t idx) {
  uint8_t hash[MAX_HASH_SIZE];
  pkt->calculatePacketHash(hash);
  _lora_tx.queued(hash, idx);
}
#endif

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
#ifdef ENABLE_MECON_IFACE
  if (len > Iface::LORA_REPLY_HDR && memcmp(data, Iface::LORA_REPLY_MAGIC, 4) == 0) {
    _iface.onRfReply(contact.id.pub_key, data, len);
    return;
  }
#endif
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else {
    bool handled;
    if (_listener && len > 4) {
      handled = _listener->onUnhandledResponse(contact, tag, &data[4], len - 4);
    } else {
      handled = false;
    }
    if (!handled) {   // let the app know about unknown/unhandled responses (eg. push telemetry packets)
      int i = 0;
      out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], data, len);
      i += len;
      _serial->writeFrame(out_frame, i);
    }
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }

  if (_listener) _listener->onControlDataRecv(packet);
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onRawDataRecv(packet);
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onTraceRecv(packet, tag, auth_code, flags, path_snrs, path_hashes, path_len);
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _listener(NULL), _iter(0) {
  _iter_started = false;
  _cli_rescue = false;
  cli_command[0] = 0;
  offline_queue_len = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  _contacts_retries = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  send_unscoped = false;

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.tx_delay_factor = 0.5f;
  _prefs.direct_tx_delay_factor = 0.2f;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
  _prefs.radio_fem_rxgain = 1;
#ifdef RADIO_FEM_TXGAIN
  _prefs.radio_fem_txgain = RADIO_FEM_TXGAIN;   // board-specific default for PA
#else
  _prefs.radio_fem_txgain = 0;
#endif
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
  _prefs.setRepeatEn(false);
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif
}

void MyMesh::begin() {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }
#ifdef ENABLE_MECON_IFACE
  {
    uint8_t device_id[16];
    uint32_t epoch = 0;
    CryptoRNG crng(getRNG());   // device_id must not follow from a small seed (docs/contract/AUTHORITY.md section 12)
    // A failed save still leaves this boot's values, but the epoch may repeat: nothing is signed then (section 3.4).
    const bool saved = _store->bootDeviceMeta(&crng, device_id, &epoch);
    _iface.begin(this, device_id, epoch, saved);
  }
#endif

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs);
  auditStore().begin(_store->getPrimaryFS());
  sensors.node_lat = _prefs.node_lat;
  sensors.node_lon = _prefs.node_lon;

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.tx_delay_factor = constrain(_prefs.tx_delay_factor, 0, 2.0f);
  _prefs.direct_tx_delay_factor = constrain(_prefs.direct_tx_delay_factor, 0, 2.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours

  _active_ble_pin = _prefs.ble_pin;

  loadContactsAtBoot();
#if defined(ENABLE_MECON_IFACE) && defined(MAX_GROUP_CHANNELS)
  if (ChannelDetails* pub = addChannel("Public", PUBLIC_GROUP_PSK)) _public_channel = *pub;   // device.reset channels
#else
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure public channel
#endif
  _store->loadChannels(this);
#ifdef ENABLE_MECON_IFACE
  recoverRotationAtBoot();   // a rotation cut by a power loss: delete a statement that never took effect, finish a pending drop
  _iface.jobsLoad();   // after the prefs, before the radio and the links: a revert at boot takes effect at once
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  _radio_freq = _prefs.freq; _radio_bw = _prefs.bw;
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);

  board.attachDynamicPrefs(_prefs.getCustom());

  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}
void MyMesh::setBLEPin(uint32_t active_pin) {
  _active_ble_pin = active_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

// Which native frames the engine gates (src/helpers/cli/), and as what. Messaging, contact handling (short of a grant)
// and reads are not gated: the TCP port and BLE with a default PIN serve them, as the posture doc promises ("messaging only").
// Everything that manages the device is: settings, the radio, raw transmission, signing with the node key, reboot,
// and the remote administration of another node under this node's identity (a CLI text, any text to a repeater,
// which runs every text from an admin as a command, and binary requests). docs/CLI.md lists the frames.
uint8_t MyMesh::frameGate(size_t len) {
  switch (cmd_frame[0]) {
    case CMD_RUN_CLI_COMMAND: return Cli::FR_CLI;
    case CMD_EXPORT_PRIVATE_KEY: return Cli::FR_KEY_EXPORT;
    case CMD_IMPORT_PRIVATE_KEY: return Cli::FR_KEY_IMPORT;
    case CMD_SET_DEVICE_PIN: return Cli::FR_SET_PIN;
    case CMD_FACTORY_RESET: return Cli::FR_FACTORY_RESET;
    case CMD_REBOOT: return Cli::FR_APP_REBOOT;
    case CMD_SET_ADVERT_NAME: case CMD_SET_ADVERT_LATLON: case CMD_SET_RADIO_PARAMS: case CMD_SET_RADIO_TX_POWER:
    case CMD_SET_TUNING_PARAMS: case CMD_SET_OTHER_PARAMS: case CMD_SET_CHANNEL: case CMD_SET_CUSTOM_VAR:
    case CMD_SET_AUTOADD_CONFIG: case CMD_SET_PATH_HASH_MODE: case CMD_SET_DEFAULT_FLOOD_SCOPE:
    case CMD_SEND_RAW_DATA: case CMD_SEND_RAW_PACKET: case CMD_SEND_CONTROL_DATA:
    case CMD_SIGN_START: case CMD_SIGN_DATA: case CMD_SIGN_FINISH:
    case CMD_SEND_BINARY_REQ:
      return Cli::FR_APP_WRITE;
    case CMD_SET_DEVICE_TIME:
      // The clock only moves forward here (the handler refuses an earlier time), so the app cannot undo a push ahead,
      // and replay windows and ACLs compare against it: setting it is a setting. The first set since boot is open to
      // any caller, as the app does on connecting; what the clock reads at boot (a contact's time, a stale RTC) may be
      // far behind.
      return _clock_set ? Cli::FR_APP_WRITE : Cli::FR_NONE;
    case CMD_ADD_UPDATE_CONTACT: {
      // Granting a contact telemetry or the remote CLI is a setting: the remote CLI runs that key's texts as commands.
      // The rest of the record, and taking a grant away, is contact handling.
      if (len < 1 + PUB_KEY_SIZE + 2) return Cli::FR_NONE;   // too short: the handler answers it
      const ContactInfo* had = lookupContactByPubKey(&cmd_frame[1], PUB_KEY_SIZE);
      const uint8_t held = had ? had->flags : 0;
      return (cmd_frame[1 + PUB_KEY_SIZE + 1] & ~held & CONTACT_GRANT_FLAGS) ? Cli::FR_APP_WRITE : Cli::FR_NONE;
    }
    case CMD_SEND_TXT_MSG: {
      if (len < 2) return Cli::FR_NONE;   // too short to send anything: the handler answers it
      const uint8_t txt_type = cmd_frame[1];
      if (txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND) return Cli::FR_APP_WRITE;
      if (len >= 13) {
        const ContactInfo* to = lookupContactByPubKey(&cmd_frame[7], 6);
        if (to && to->type == ADV_TYPE_REPEATER) return Cli::FR_APP_WRITE;
      }
      return Cli::FR_NONE;
    }
  }
  return Cli::FR_NONE;
}

// Whether the frame's caller may see this device's secrets inside a read it is otherwise allowed (the BLE PIN in the
// device info, a channel's secret): a cable or BLE paired with the device's own PIN, as for any read of the CLI.
bool MyMesh::serialTrusted() const {
  return Cli::decide(serialContext(), Cli::C_READ).allow;
}

// The caller context of a frame that arrived over the serial interface: a cable is trusted by possession, BLE paired
// with a PIN of the device's own is trusted, and everything else (the open TCP port, BLE with a default PIN, an
// interface that did not say) authenticates nobody.
Cli::Context MyMesh::serialContext() const {
  if (_serial && _serial->isCableTransport()) return Cli::Context(Cli::T_USB, Cli::A_CABLE, _prefs.posture);
  if (_serial && _serial->isTrustedTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);
  // BLE with a default PIN is still BLE: it authenticates nobody (untrusted row) but a refusal is audited as "ble".
  if (_serial && _serial->isBleTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_DEFAULT_PIN, _prefs.posture);
  return Cli::Context(Cli::T_TCP, Cli::A_NONE, _prefs.posture);
}

void MyMesh::setCliCaller(const Cli::Context& ctx, uint8_t caller) {
  _cli_ctx = ctx;
  _cli_caller = caller;
}

// CMD_SET_DEVICE_TIME and clock.set: the clock moves forward only (a time earlier than it reads is refused).
bool MyMesh::setClockForward(uint32_t secs) {
  if (secs < getRTCClock()->getCurrentTime()) return false;
  getRTCClock()->setCurrentTime(secs);
  _clock_set = true;
  return true;
}

// `clock back` and clock.back: set a clock that ran ahead back to secs, which must be at or above the floor every "clock is
// set" check uses and earlier than the clock now. Audited as clock-back for caller. false, and nothing changed, otherwise.
bool MyMesh::setClockBack(uint32_t secs, uint8_t caller) {
  if (secs < 1704067200UL || secs >= getRTCClock()->getCurrentTime()) return false;
  getRTCClock()->setCurrentTime(secs);
  getRTCClock()->restartUnique();
  Posture::audit().add(secs, Posture::EV_CLOCK_BACK, caller, Posture::W_NONE);
  return true;
}

// ble.pin.set: as CMD_SET_DEVICE_PIN, 0 (a PIN shown per session, where a screen can show one) or six digits. Saved; the
// BLE stack takes it at its next start.
bool MyMesh::setStoredBlePin(uint32_t pin) {
  if (pin != 0 && (pin < 100000 || pin > 999999)) return false;
  _prefs.ble_pin = pin;
  savePrefs();
  return true;
}

// The self advert of CMD_SEND_SELF_ADVERT and of advert.send: flooded in the default scope, or zero hop. false when no
// packet could be allocated.
bool MyMesh::sendSelfAdvert(bool flood) {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (!pkt) return false;
  if (flood) {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
    sendFloodScoped(default_scope, pkt, 0);
  } else {
    sendZeroHop(pkt);
  }
  return true;
}

void MyMesh::handleCmdFrame(size_t len) {
  const uint32_t now = getRTCClock()->getCurrentTime();
  {
    const uint8_t frame = frameGate(len);
    Cli::Context fctx = serialContext();
#ifdef ENABLE_MECON_IFACE
    // Once enrolled, an unsigned app changes no settings and only the cable factory-resets (AUTHORITY.md section 9): the
    // privilege table decides every gated frame with the enrolled flag. Messaging frames are not gated.
    fctx.enrolled = _iface.enrolled();
    // A command line has its own gate, with the unlock and a reason in words: a locked cable is decided there, not here.
    if (frame == Cli::FR_CLI) fctx.unlocked = true;
#endif
    if (frame != Cli::FR_NONE && !Cli::allowFrame(fctx, frame, now)) {
      MESH_DEBUG_PRINTLN("command %d refused for this caller", (int)cmd_frame[0]);
      writeErrFrame(ERR_CODE_NOT_PERMITTED);
      return;
    }
#ifdef ENABLE_MECON_IFACE
    // A locked cable reads nothing natively either (AUTHORITY.md section 9). It keeps the two frames that say what the device
    // is, the interface that offers the unlock, and the frames it had beyond reads, which the gate above has decided.
    if (frame == Cli::FR_NONE && cmd_frame[0] != CMD_APP_START && cmd_frame[0] != CMD_DEVICE_QUERY && cmd_frame[0] != CMD_MECON &&
        _iface.cableLocked(_cmd_src, fctx)) {
      writeErrFrame(ERR_CODE_NOT_PERMITTED);
      return;
    }
#endif
  }
  if (cmd_frame[0] == CMD_DEVICE_QUERY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand
#ifdef ENABLE_MECON_IFACE
    _iface.onNewSession(_cmd_src);
#endif

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    {
      int half = getContactCapacity() / 2;   // the app takes one byte
      out_frame[i++] = half > 255 ? 255 : half;   // v3+
    }
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    {
      const uint32_t pin = serialTrusted() ? _prefs.ble_pin : 0;   // an untrusted caller could pair with it
      memcpy(&out_frame[i], &pin, 4);
    }
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    out_frame[i++] = _prefs.isRepeatEn() ? 1 : 0;   // v9+
    out_frame[i++] = _prefs.path_hash_mode;  // v10+
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    _iter_started = false; // stop any left-over ContactsIterator
#ifdef ENABLE_MECON_IFACE
    _iface.onNewSession(_cmd_src);   // a USB link has no drop signal: an app starting over is a new session, locked again
#endif
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
#ifdef ENABLE_MECON_IFACE
  } else if (cmd_frame[0] == CMD_MECON) {   // the device interface (MeconIface.h); the reply is bound to its source
    _iface.onFrame(_cmd_src, cmd_frame, len, millis());
#endif
  } else if (cmd_frame[0] == CMD_RUN_CLI_COMMAND && len >= 3) { // V14+
    int i = 1;
    char *text = (char *)&cmd_frame[i];
    int tlen = len - i;
    text[tlen] = 0; // ensure null

    reply_buf[0] = 0;
    {
      Cli::Context ctx = serialContext();
#ifdef ENABLE_MECON_IFACE
      ctx.unlocked = ctx.transport == Cli::T_USB && ctx.auth == Cli::A_CABLE && _iface.unlockedFor(_cmd_src);   // section 10
#endif
      setCliCaller(ctx, ctx.transport == Cli::T_USB ? Posture::CALLER_CABLE : Posture::CALLER_BLE);
    }
    if (!handleCommand(text, 0, reply_buf)) {
      strcat(reply_buf, "Unknown command");   // reply_buf may have cmd prefix from 'text'
    }
    out_frame[0] = RESP_CODE_CLI_REPLY;
    int rlen = strlen(reply_buf);
    memcpy(&out_frame[1], reply_buf, rlen);
    _serial->writeFrame(out_frame, 1 + rlen);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, txt_type, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      // TODO: add expected ACK to table
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel text msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_DATA) { // send GroupChannel datagram
    if (len < 4) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }
    int i = 1;
    uint8_t channel_idx = cmd_frame[i++];
    uint8_t path_len = cmd_frame[i++];

    // validate path len, allowing 0xFF for flood
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }

    // parse provided path if not flood
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) {
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
    }

    uint16_t data_type = ((uint16_t)cmd_frame[i]) | (((uint16_t)cmd_frame[i + 1]) << 8);
    i += 2;
    const uint8_t *payload = &cmd_frame[i];
    int payload_len = (len > (size_t)i) ? (int)(len - i) : 0;

    ChannelDetails channel;
    if (!getChannel(channel_idx, channel)) {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    } else if (data_type == DATA_TYPE_RESERVED) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (payload_len > MAX_GROUP_DATA_LENGTH) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_GROUP_DATA_LENGTH);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts(); // total, NOT filtered count
      memcpy(&reply[1], &count, 4);
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _iter_src = _cmd_src;
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    if (setClockForward(secs)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    if (sendSelfAdvert(len >= 2 && cmd_frame[1] == 1)) {   // optional param (1 = flood, 0 = zero hop)
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1 && cmd_frame[1 + PUB_KEY_SIZE] == ADV_TYPE_NONE) {
    // Type 0 is the anonymous slot's (BaseChatMesh::addContact): a contact added with it is unlisted and overwritten
    // by the next unknown sender, and an update to it would turn a stored contact into one. Refused, as stock did not.
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    const uint32_t curr = getRTCClock()->getCurrentTime();
    uint32_t last_mod = curr;  // fallback value if not present in cmd_frame
    // The newest lastmod sets the clock at boot (bootstrapRTCfromContacts): a caller that may not set the clock does
    // not set one ahead of it here either.
    const bool clamp = !serialTrusted();
    if (recipient) {
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      if (clamp && last_mod > curr) last_mod = curr;
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      if (clamp && last_mod > curr) last_mod = curr;
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient && removeContact(*recipient)) {
      _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
    }
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
        pkt = createSelfAdvert(_prefs.node_name);
      } else {
        pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
      }
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
      _serial->writeFrame(out_frame, out_len);
      if (_listener) _listener->onQueueSizeChanged(offline_queue_len);
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];
    uint8_t repeat = 0;  // default - false
    if (len > i) {
      repeat = cmd_frame[i++];   // FIRMWARE_VER_CODE  9+
    }

    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.setRepeatEn(repeat != 0);
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      _radio_freq = _prefs.freq; _radio_bw = _prefs.bw;   // the duty-cycle floor follows at once
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
      saveContacts();
    }
    board.reboot();
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // the cached ECDH shared secrets belong to the old key
          reloadContactSecrets();
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    uint8_t path_len = cmd_frame[i++];
    if (path_len >= 0 && mesh::Packet::isValidPathLen(path_len)) {
      uint8_t path[MAX_PATH_SIZE];
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
      if (i + 4 > len) {  // min payload 4 bytes
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      } else {
        auto pkt = createRawData(&cmd_frame[i], len - i);
        if (pkt) {
          sendDirect(pkt, path, path_len);
          writeOKFrame();
        } else {
          writeErrFrame(ERR_CODE_TABLE_FULL);
        }
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    ContactInfo anon;
    if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
      memset(&anon, 0, sizeof(anon));
      memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
      anon.out_path_len = 0;   // default to zero-hop direct
      anon.type = ADV_TYPE_NONE;  // unknown
      anon.lastmod = getRTCClock()->getCurrentTime();

      if (addContact(anon)) recipient = &anon;
    }
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      // 'Path Discovery' is just a special case of flood + Telemetry req
      uint8_t req_data[9];
      req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
      req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
      memset(&req_data[2], 0, 3);  // reserved
      getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
      auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
      recipient->out_path_len = save;
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      StrHelper::strzcpy((char *)&out_frame[i], channel.name, 32);   // the whole field: nothing of an earlier reply goes with it
      i += 32;
      if (serialTrusted()) memcpy(&out_frame[i], channel.channel.secret, 16);
      else memset(&out_frame[i], 0, 16);   // the name serves messaging; the secret is not an untrusted caller's
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    out_frame[0] = RESP_CODE_SIGN_START;
    out_frame[1] = 0; // reserved
    uint32_t len = MAX_SIGN_DATA_LEN;
    memcpy(&out_frame[2], &len, 4);
    _serial->writeFrame(out_frame, 6);

    if (sign_data) {
      free(sign_data);
    }
    sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
    sign_data_len = 0;
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > MAX_SIGN_DATA_LEN) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data && sign_data_len >= 6 && memcmp(sign_data, "MECON-", 6) == 0) {
      // The MECON protocols sign under their own domain prefixes ("MECON-UPLINK-v1", "MECON-NODEROT-v1"); the native signer
      // never signs bytes that begin that way, enrolled or not (AUTHORITY.md section 3.4).
      free(sign_data);
      sign_data = NULL;
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = (uint32_t)Uptime::secs(_ms->getMillis());
      uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    if (_serial) {
      MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
      _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
    }
    bool success = _store->formatFileSystem();
    if (success) {
      recordFactoryReset((_serial && _serial->isCableTransport()) ? Posture::CALLER_CABLE : Posture::CALLER_BLE);
      writeOKFrame();
      delay(1000);
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_DEFAULT_FLOOD_SCOPE && len >= 1) {
    if (len >= 1+31+16) {
      int n = strlen((char *) &cmd_frame[1]);
      if (n > 0 && n < 31) {
        strcpy(_prefs.default_scope_name, (char *) &cmd_frame[1]);
        memcpy(_prefs.default_scope_key, &cmd_frame[1+31], 16);
        savePrefs();
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));  // set default scope to null
      memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (strlen(_prefs.default_scope_name) > 0) {
      memcpy(&out_frame[1], _prefs.default_scope_name, 31);
      memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
      _serial->writeFrame(out_frame, 1+31+16);
    } else {
      _serial->writeFrame(out_frame, 1);   // no name or key means null
    }
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    if (len >= 3) {
      _prefs.autoadd_max_hops = min(cmd_frame[2], (uint8_t)64);
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    out_frame[i++] = _prefs.autoadd_max_hops;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_GET_ALLOWED_REPEAT_FREQ) {
    int i = 0;
    out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
    for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
      auto r = &repeat_freq_ranges[k];
      memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
      memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
    }
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_RAW_PACKET && len >= 4) {
    auto pkt = obtainNewPacket();
    if (pkt) {
      uint8_t priority = cmd_frame[1];
      if (tryParsePacket(pkt, &cmd_frame[2], len - 2)) {
        sendPacket(pkt, priority, 0);
        writeOKFrame();
      } else {
        releasePacket(pkt);
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

#ifdef CONTACTS_STOCK_ARRAY
static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}
#endif

void MyMesh::saveContacts() {
#ifdef CONTACTS_STOCK_ARRAY
  _store->saveContacts(this, save_filter);
#else
  flushContacts();   // rewrites the buckets that hold a changed record, each one atomically
#endif
}

bool MyMesh::onContactLoaded(const ContactInfo& contact) {
  if (_migrate == MIGRATE_COUNT) {
    if (!contactStore().find(contact.id.pub_key, PUB_KEY_SIZE)) _unmigrated++;
    return true;
  }
  if (_migrate != MIGRATE_NONE) {
    ContactStore& cs = contactStore();
    if (_migrate == MIGRATE_MERGE && cs.find(contact.id.pub_key, PUB_KEY_SIZE)) return true;   // the store's is newer
    // A migration never evicts: a contact that does not fit stays in the old file, which is then kept (#223).
    if (cs.count() >= cs.capacity()) { _migrate_full = true; return false; }
    if (_migrate == MIGRATE_BULK) return cs.importAdd(contact, 0, getRTCClock()->getCurrentTime());
    if (!addContact(contact)) { _migrate_full = true; return false; }
    return true;
  }
  return addContact(contact);
}

// Boot: the contact store is set up from the free heap and file system space, then a stock /contacts3 file, if
// there is one, moves into it (an empty store takes it in bulk, bucket by bucket; a store that already holds
// contacts merges it, which is also how a migration that was cut short finishes). The old file is removed
// only after the last bucket is safely written.
void MyMesh::loadContactsAtBoot() {
  resetContacts();
#ifdef CONTACTS_STOCK_ARRAY
  _store->loadContacts(this);
#else
  initContactStore(_store, contactStoreConfigFor(MAX_CONTACTS));
  if (_store->legacyContactsExist()) migrateLegacyContacts();
#endif
  bootstrapRTCfromContacts();
}

void MyMesh::migrateLegacyContacts() {
  ContactStore& cs = contactStore();
  _migrate_full = false;
  bool bulk = cs.count() == 0;
  _migrate = bulk ? MIGRATE_BULK : MIGRATE_MERGE;
  if (bulk) cs.importBegin();
  _store->loadContacts(this);
  bool ok = bulk ? cs.importEnd() : cs.flush();
  _migrate = MIGRATE_NONE;
  if (ok && !_migrate_full) _store->removeLegacyContacts();
  // Whatever is left in the old file and not in the store is counted, and reported by get contacts.capacity, so a
  // shortfall is visible rather than silent; the next boot tries again.
  _unmigrated = 0;
  if (_store->legacyContactsExist()) {
    _migrate = MIGRATE_COUNT;
    _store->loadContacts(this);
    _migrate = MIGRATE_NONE;
  }
  if (_unmigrated) MESH_DEBUG_PRINTLN("contacts: %u stay in /contacts3, the store has no room for them", _unmigrated);
}

void MyMesh::reloadContactSecrets() {
#ifdef CONTACTS_STOCK_ARRAY
  resetContacts();
  _store->loadContacts(this);
#else
  invalidateContactSecrets();
#endif
}

#ifdef ENABLE_MECON_IFACE
// The statement's file and the role's half of a rotation, for the steps in helpers/iface/IdentityRotation.cpp.
size_t MyMesh::rotLoad(void* u, uint8_t* buf, size_t cap) { return static_cast<MyMesh*>(u)->_store->loadRotation(buf, cap); }
bool MyMesh::rotSave(void* u, const uint8_t* buf, size_t len) { return static_cast<MyMesh*>(u)->_store->saveRotation(buf, len); }
void MyMesh::rotRemove(void* u) { static_cast<MyMesh*>(u)->_store->removeRotation(); }
bool MyMesh::rotSaveKey(void* u, const mesh::LocalIdentity& id) { return static_cast<MyMesh*>(u)->_store->saveMainIdentity(id); }
bool MyMesh::rotDrop(void* u) { return static_cast<MyMesh*>(u)->dropOldKeyState(); }
Iface::RotationStore MyMesh::rotationStore() { return Iface::RotationStore{rotLoad, rotSave, rotRemove, rotSaveKey, rotDrop, this}; }

bool MyMesh::readRotation(char* text, size_t tcap, char* sig_b64, size_t scap) {
  return Iface::rotationRead(rotationStore(), text, tcap, sig_b64, scap);
}

// Drops what belonged to the old key: contacts (their shared secrets), the message queue and what was cached from them.
bool MyMesh::resetScope(Iface::ResetScope s, uint8_t caller) {
  switch (s) {
    case Iface::RS_RADIO:   // the build's defaults, as a first boot sets them; applied at the reboot that follows
      _prefs.freq = LORA_FREQ;
      _prefs.bw = LORA_BW;
      _prefs.sf = LORA_SF;
      _prefs.cr = LORA_CR;
      _prefs.tx_power_dbm = LORA_TX_POWER;
      savePrefs();
      return true;
    case Iface::RS_NETWORK:   // every stored profile; a build's own compiled-in network is not stored, so it stays
#ifdef ENABLE_WIFI_INTERFACE
      for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) _prefs.wifi_profiles.clear(i);
#endif
#ifdef ENABLE_MQTT
      for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) _prefs.mqtt_profiles.clear(i);
#endif
      savePrefs();
      return true;
#ifdef MAX_GROUP_CHANNELS
    case Iface::RS_CHANNELS: {   // every slot empty but the first, which is the public channel again
      ChannelDetails ch;
      memset(&ch, 0, sizeof(ch));
      for (int i = 1; i < MAX_GROUP_CHANNELS; i++) setChannel(i, ch);
      setChannel(0, _public_channel);   // as boot made it, before the stored channels were loaded
      return saveChannels();
    }
#endif
    case Iface::RS_CONTACTS: {   // the table and its blobs; the message queue and identity stay
      ContactStore& cs = contactStore();
      _store->deleteAllBlobs();
      const bool ok = cs.clearAll();
      resetContacts();   // the transient anon slots
#ifdef CONTACTS_STOCK_ARRAY
      saveContacts();
#endif
      dirty_contacts_expiry = 0;
      return ok;
    }
    case Iface::RS_BONDS:   // carried out at the next BLE start, before advertising (main.cpp): the stack holds the store
      return _store->setBondsClearPending(true);
    case Iface::RS_FACTORY: {   // the native factory reset (CMD_FACTORY_RESET)
      const bool ok = _store->formatFileSystem();
      if (ok) recordFactoryReset(caller);
      dirty_contacts_expiry = 0;   // nothing written back into the erased files before the reboot
      return ok;
    }
    default:
      return false;
  }
}

bool MyMesh::dropOldKeyState() {
  bool dropped = true;
  {
    ContactStore& cs = contactStore();
    _store->deleteAllBlobs();   // the old key's adverts
    dropped = cs.clearAll();
    resetContacts();   // the transient anon slots
#ifdef CONTACTS_STOCK_ARRAY
    saveContacts();   // the stock array is a file: write the empty list
#endif
  }
  offline_queue_len = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  clearPendingReqs();
  dirty_contacts_expiry = 0;
  if (_listener) _listener->onQueueSizeChanged(0);
  return dropped;
}

// On nRF52 on the worker: two 512 B statement buffers and a save through LittleFS are deeper than the loop task's 4 KB
// has room for (#172). Kept out of line so the stack gate can check the hand-off (stack-gate.yml).
__attribute__((noinline)) void MyMesh::recoverRotationAtBoot() {
  if (!NrfWorker::run(recoverRotationJob, this, 30000)) recoverRotationOnLoop();
}

__attribute__((noinline)) void MyMesh::recoverRotationJob(void* self) { static_cast<MyMesh*>(self)->recoverRotation(); }

// No worker (no memory at boot): the recovery runs on the loop task's small stack, as before #172. A degraded mode, as
// for received packets (Dispatcher::recvOnLoop); the stack gate does not count it (stack-gate.yml).
__attribute__((noinline)) void MyMesh::recoverRotationOnLoop() { recoverRotation(); }

void MyMesh::recoverRotation() { Iface::rotationRecover(rotationStore(), self_id); }

int MyMesh::rotateIdentity(const char** err, const char* device_id_hex, uint32_t job_id) {
  const int r = Iface::rotationRun(rotationStore(), self_id, device_id_hex, job_id, err);
  if (!r) return 0;
  // One self-advert, zero hop like the app's default.
  mesh::Packet* pkt = _prefs.advert_loc_policy == ADVERT_LOC_NONE ? createSelfAdvert(_prefs.node_name)
                                                                   : createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  if (pkt) sendZeroHop(pkt);
  return r;
}
#endif

// The command line over MQTT: the controller built the context from the broker session, the gate inside
// handleCommand() decides, and a refusal is a reply. A remote caller always has a non-zero timestamp.
uint8_t MyMesh::runMqttCli(const Cli::Context& ctx, const char* cmd, char* reply, size_t cap) {
  reply_buf[0] = 0;
  setCliCaller(ctx, Posture::CALLER_MQTT);
  const bool known = handleCommand(cmd, 1, reply_buf);
  setCliCaller(Cli::Context(), Posture::CALLER_NET);
  if (!known) strcat(reply_buf, "Unknown command");   // reply_buf may hold a reflected prefix
  snprintf(reply, cap, "%s", reply_buf);
  if (_cli_refused) return Cli::Mqtt::S_REFUSED;
  return known ? Cli::Mqtt::S_OK : Cli::Mqtt::S_UNKNOWN;
}

// A factory reset returns the device to Normal; the audit log (erased with the files) is written again with
// the reset on it, so the history of what happened before the reset is not itself a casualty of it.
void MyMesh::recordFactoryReset(uint8_t caller) {
#ifdef ENABLE_MECON_IFACE
  _iface.onFactoryReset();   // the record went with the format; the provisioned marker goes with it
#endif
  _prefs.posture = Posture::NORMAL;
  Posture::audit().add(getRTCClock()->getCurrentTime(), Posture::EV_FACTORY_RESET, caller, Posture::W_NONE);
  auditStore().flush(millis());
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

bool MyMesh::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    *reply = 0;
    command += 3;
  }

  // The engine's gate (src/helpers/cli/, docs/CLI.md): who may run which class of command. The adapter that carried
  // the line set the context (setCliCaller); a remote caller always arrives with a non-zero timestamp, and a context
  // that disagrees with the timestamp is treated as untrusted.
  _cli_refused = false;
  {
    Cli::Context ctx = _cli_ctx;
    ctx.posture = _prefs.posture;
    ctx.remote = sender_timestamp != 0;
#ifdef ENABLE_MECON_IFACE
    ctx.enrolled = _iface.enrolled();   // the one place that gates a command line: every adapter and the rescue console
    ctx.has_unlock_secret = _iface.hasUnlockSecret();
#endif
    Cli::Outcome outcome;
    if (Cli::gate(command, ctx, Cli::ROLE_COMPANION, getRTCClock()->getCurrentTime(), reply, 150, &outcome)) {
      _cli_refused = !outcome.allowed;
      return true;
    }
  }

  {
    bool changed = false;
    const uint8_t caller = _cli_caller;
    if (Posture::handleCommand(command, caller, _prefs.posture, getRTCClock()->getCurrentTime(), reply, 150, changed,
                               _cli_ctx.auth == Cli::A_SIGNED)) {
      if (changed) { savePrefs(); auditStore().flush(millis()); }   // the posture and its audit entry reach flash together
      return true;
    }
  }

#ifdef ENABLE_CONNMODE
  {
    bool changed = false;
    const uint8_t caller = _cli_caller;
    if (ConnMode::handleCommand(command, caller, _prefs.connectivity_mode, getRTCClock()->getCurrentTime(), reply, 150, changed,
                                _cli_ctx.auth == Cli::A_SIGNED)) {
      if (changed) {
        savePrefs();
        auditStore().flush(millis());   // the new mode and its audit entry reach flash together
        scheduleConnectivityRestart();
      }
      return true;
    }
  }
#endif

  if (memcmp(command, "clock back", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
    const uint32_t curr = getRTCClock()->getCurrentTime();
    const uint32_t secs = command[10] ? strtoul(&command[11], nullptr, 10) : 0;
    if (!setClockBack(secs, _cli_caller)) {
      strcpy(reply, "ERR: clock back <epoch seconds>, earlier than the clock now");
      return true;
    }
    snprintf(reply, 150, "OK - clock back %lu s; peers that hold this contact ignore its adverts for that long",
             (unsigned long)(curr - secs));
    return true;
  }

  if (strcmp(command, "get dutycycle") == 0) {
    formatDutyCycleReply(reply);
    return true;
  }

  if (strcmp(command, "get contacts.capacity") == 0) {   // docs/CONTACTS.md
    int n = contactStore().format(reply, 150);
    if (_unmigrated && n > 0 && n < 150) snprintf(reply + n, 150 - n, " unmigrated %u", (unsigned)_unmigrated);
    return true;
  }

  if (_prefs.getRadioPrefs()->handleCommand(command, sender_timestamp, reply)) {  // is radio CLI command?
    if (_prefs.getRadioPrefs()->isDirty()) { savePrefs(); }
    return true;
  }

  // MECON's own commands (`mecon.`), then the hook for variant-specific CLI processing
  {
    Cli::Context ctx = _cli_ctx;
    ctx.posture = _prefs.posture;
    ctx.remote = sender_timestamp != 0;
#ifdef ENABLE_MECON_IFACE
    ctx.enrolled = _iface.enrolled();   // so `mecon.can` reports the verdict the gate would give
    ctx.has_unlock_secret = _iface.hasUnlockSecret();
#endif
    if (Cli::handleMecon(command, ctx, Cli::ROLE_COMPANION, reply, 150)) return true;
  }
  if (board.handleCommand(command, sender_timestamp, reply)) {
    if (_prefs.isDirty()) { savePrefs(); }
    return true;
  }

  if (strcmp(command, "reboot") == 0) {
    board.reboot(); // doesn't return
    return true;
  }

  if (strcmp(command, "poweroff") == 0 || strcmp(command, "shutdown") == 0) {
    board.powerOff(); // doesn't return
    return true;
  }

  if (memcmp(command, "set name ", 9) == 0) {
    if (AdvertDataParser::isValidName(&command[9])) {
      StrHelper::strncpy(_prefs.node_name, &command[9], sizeof(_prefs.node_name));
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, bad chars");
    }
    return true;
  }
  if (strcmp(command, "get name") == 0) {
    sprintf(reply, "> %s", _prefs.node_name);
    return true;
  }
  if (strcmp(command, "get device.id") == 0 || memcmp(command, "get device.id ", 14) == 0) {
    // The device interface's device_id (DEVICE_INTERFACE.md section 1) and the node key's signature binding it
    // (AUTHORITY.md section 3.5): "> <device_id> <sig>", or "> <device_id> <sig> <nonce>" for `get device.id <nonce>`.
    const bool has_arg = command[13] == ' ';
    const char* nonce = has_arg ? &command[14] : "";
    char sig[89];
    if (!*Iface::deviceIdHex()) strcpy(reply, "Error: unsupported");
    else if (has_arg && !*nonce) strcpy(reply, "Error: bad nonce");   // a space and no nonce is not the fixed form
    else if (!Iface::signDeviceId(sig, self_id, Iface::deviceIdHex(), nonce)) strcpy(reply, "Error: bad nonce");
    else if (*nonce) snprintf(reply, Iface::DEVID_REPLY_CAP, "> %s %s %s", Iface::deviceIdHex(), sig, nonce);
    else snprintf(reply, Iface::DEVID_REPLY_CAP, "> %s %s", Iface::deviceIdHex(), sig);
    return true;
  }

  if (memcmp(command, "set pin ", 8) == 0) {
    _prefs.ble_pin = atoi(&command[8]);
    savePrefs();
    sprintf(reply, "> pin is now %06d", _prefs.ble_pin);
    return true;
  }

#ifdef ENABLE_WIFI_INTERFACE
  if (netCommand(_prefs, [](void* self) { static_cast<MyMesh*>(self)->savePrefs(); }, this, command, reply)) return true;
#endif

  if (strcmp(command, "board") == 0) {
    strcpy(reply, board.getManufacturerName());
    return true;
  }

  if (strcmp(command, "ver") == 0) {
    sprintf(reply, "%s (Build: %s)", mecon::versionFull(), FIRMWARE_BUILD_DATE);
    return true;
  }

#ifdef ENABLE_MECON_IFACE
  if (handleContactPolicyCommand(command, reply)) return true;
#endif

  if (strcmp(command, "get tz.offset") == 0) {
    sprintf(reply, "> %d", _prefs.tz_offset);
    return true;
  }
  if (memcmp(command, "set tz.offset ", 14) == 0) {
    const float tz = atof(&command[14]);   // checked before the cast: 250 must not wrap to -6
    if (tz < -12 || tz > 14) {
      strcpy(reply, "Error, must be from -12 to +14");
    } else {
      _prefs.tz_offset = (int8_t)tz;
      savePrefs();
      strcpy(reply, "OK");
    }
    return true;
  }

  return false;  // not handled
}

#ifdef ENABLE_MECON_IFACE
// The contact auto-add, telemetry and advert-location settings that only the app frames 38 and 58 set
// (CMD_SET_OTHER_PARAMS, CMD_SET_AUTOADD_CONFIG), as CLI lines, so the device interface's config keys reach them. Built
// with the device interface only: a board without it keeps the app frames (a small STM32 build has no room to spare).
bool MyMesh::handleContactPolicyCommand(const char* command, char* reply) {
  enum Kind : uint8_t { MANUAL, AUTOADD, HOPS, TELEM, LOCATION };
  static const struct { const char* get; const char* set; uint8_t kind; uint8_t arg; } LINES[] = {
    {"get contacts.manual_add", "set contacts.manual_add ", MANUAL, 1},
    {"get contacts.autoadd.chat", "set contacts.autoadd.chat ", AUTOADD, AUTO_ADD_CHAT},
    {"get contacts.autoadd.repeater", "set contacts.autoadd.repeater ", AUTOADD, AUTO_ADD_REPEATER},
    {"get contacts.autoadd.room", "set contacts.autoadd.room ", AUTOADD, AUTO_ADD_ROOM_SERVER},
    {"get contacts.autoadd.sensor", "set contacts.autoadd.sensor ", AUTOADD, AUTO_ADD_SENSOR},
    {"get contacts.autoadd.overwrite", "set contacts.autoadd.overwrite ", AUTOADD, AUTO_ADD_OVERWRITE_OLDEST},
    {"get contacts.autoadd.max_hops", "set contacts.autoadd.max_hops ", HOPS, 0},
    {"get telemetry.base", "set telemetry.base ", TELEM, 0},
    {"get telemetry.location", "set telemetry.location ", TELEM, 1},
    {"get telemetry.environment", "set telemetry.environment ", TELEM, 2},
    {"get advert.location", "set advert.location ", LOCATION, 0},
  };
  static const char* const TELEM_NAMES[] = {"deny", "flags", "all"};
  for (const auto& l : LINES) {
    const bool get = strcmp(command, l.get) == 0;
    const size_t sl = strlen(l.set);
    if (!get && strncmp(command, l.set, sl) != 0) continue;
    const char* val = command + sl;
    uint8_t* tel = l.arg == 0 ? &_prefs.telemetry_mode_base : l.arg == 1 ? &_prefs.telemetry_mode_loc : &_prefs.telemetry_mode_env;
    uint8_t* bits = l.kind == MANUAL ? &_prefs.manual_add_contacts : &_prefs.autoadd_config;
    switch (l.kind) {
      case MANUAL:
      case AUTOADD:
        if (get) { sprintf(reply, "> %s", (*bits & l.arg) ? "on" : "off"); return true; }
        if (strcmp(val, "on") == 0) *bits |= l.arg;
        else if (strcmp(val, "off") == 0) *bits &= ~l.arg;
        else { strcpy(reply, "Error, on or off"); return true; }
        break;
      case HOPS: {
        if (get) { sprintf(reply, "> %u", (unsigned)_prefs.autoadd_max_hops); return true; }
        char* end = nullptr;
        const long v = strtol(val, &end, 10);
        if (!*val || *end || v < 0 || v > 64) { strcpy(reply, "Error, 0 to 64"); return true; }
        _prefs.autoadd_max_hops = (uint8_t)v;
        break;
      }
      case TELEM: {
        if (get) { sprintf(reply, "> %s", *tel < 3 ? TELEM_NAMES[*tel] : "deny"); return true; }
        uint8_t m = 0;
        while (m < 3 && strcmp(val, TELEM_NAMES[m]) != 0) m++;
        if (m == 3) { strcpy(reply, "Error, deny, flags or all"); return true; }
        *tel = m;
        break;
      }
      case LOCATION:
        if (get) { sprintf(reply, "> %s", _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? "share" : "none"); return true; }
        if (strcmp(val, "share") == 0) _prefs.advert_loc_policy = ADVERT_LOC_SHARE;
        else if (strcmp(val, "none") == 0) _prefs.advert_loc_policy = ADVERT_LOC_NONE;
        else { strcpy(reply, "Error, none or share"); return true; }
        break;
    }
    savePrefs();
    strcpy(reply, "OK");
    return true;
  }
  return false;
}
#endif

bool MyMesh::rescueFilesLocked() {
#ifdef ENABLE_MECON_IFACE
  if (_iface.enrolled()) return true;   // a healthy or a corrupt record
#endif
  return _prefs.posture != Posture::NORMAL;
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  // `cli_command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(cli_command) and the loop below would
  // then index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(cli_command)) {
    cli_command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (cli_command_dropping) {   // inside an over-long line: discarded up to its end, never run (#165)
      if (c == '\r') { cli_command_dropping = false; Serial.println("\n  -> ERR: line too long"); }
      continue;
    }
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1 && cli_command[len - 1] != '\r') {  // buffer full with no end of line: an over-long line
    // Stock ran the first part as a line and then the rest as a new command. The line is dropped up to its end
    // instead, and answered once there (#165).
    cli_command[0] = 0;
    len = 0;
    cli_command_dropping = true;
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator

    reply_buf[0] = 0;
    setCliCaller(Cli::Context(Cli::T_USB, Cli::A_CABLE, _prefs.posture), Posture::CALLER_CABLE);   // the rescue console is the USB / UART serial port
    // erase and rebuild are the cable's recovery operations (AUTHORITY.md section 11), decided below, not command lines
    const bool recovery = strcmp(cli_command, "rebuild") == 0 || strcmp(cli_command, "erase") == 0;
    const bool file_cmd = memcmp(cli_command, "ls", 2) == 0 || memcmp(cli_command, "cat", 3) == 0 || memcmp(cli_command, "rm ", 3) == 0;
    if (file_cmd && rescueFilesLocked()) {
      // The file commands read and delete the record (the local unlock secret is in it), the marks and the identity file
      // (the node key): they would bypass sections 9 to 11 of AUTHORITY.md. Refused whatever the posture while a record
      // exists, and in Hardened even without one, with a reply that says why. Factory reset ("erase") and "rebuild" are
      // the recovery paths.
      Serial.println("  Error: file commands are refused on a device that belongs to a Deployment or is Hardened; use rebuild or erase");
    } else if (!recovery && handleCommand(cli_command, 0, reply_buf)) {
      // command was handled, print reply output
      Serial.print("  "); Serial.print(reply_buf); Serial.println();
    } else if (strcmp(cli_command, "rebuild") == 0) {
#ifdef ENABLE_MECON_IFACE
      // A rebuild must not unenrol: the Deployment record, its marks and the provisioned marker are read first, and written
      // back after. If they cannot be read, nothing is rebuilt.
      MeconIface::RecordSnapshot* snap = _iface.snapshotRecord();
      if (!snap) {
        Serial.println("  Error: record unreadable; use deployment.reset or factory reset");
      } else {
#endif
#ifndef CONTACTS_STOCK_ARRAY
      // The contacts are rebuilt from the persistent store itself, so the file system is not formatted (that would
      // erase the contacts that are not in the cache). The other files are written again from memory.
      _store->saveMainIdentity(self_id);
      savePrefs();
      saveChannels();
      Posture::audit().markAllDirty();
      auditStore().flush(millis());
      int damaged = 0, isolated = 0;
      int kept = rebuildContacts(&damaged, &isolated);
#ifdef ENABLE_MECON_IFACE
      if (!_iface.restoreRecord(snap)) Serial.println("  Error: the Deployment record could not be written back; use deployment.reset");
#endif
      Serial.print("  > rebuilt "); Serial.print(kept); Serial.print(" contacts, "); Serial.print(damaged); Serial.println(" damaged");
      if (isolated > 0) { Serial.print("  > "); Serial.print(isolated); Serial.println(" damaged bucket(s) kept aside as /ct<N>.bad"); }
#else
      bool success = _store->formatFileSystem();
#ifdef ENABLE_MECON_IFACE
      // The format erased the provisioned marker too (NVS on ESP32): it goes back before anything else, so a power cut from
      // here on reads as a corrupt record, never as a device that was never provisioned (AUTHORITY.md section 4).
      if (!_iface.restoreMarker(snap)) Serial.println("  Error: the provisioned marker could not be written back; use deployment.reset");
#endif
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Posture::audit().markAllDirty();   // the format erased the audit file; the log is still in RAM
        auditStore().flush(millis());
#ifdef ENABLE_MECON_IFACE
        if (!_iface.restoreRecord(snap)) Serial.println("  Error: the Deployment record could not be written back; use deployment.reset");
#endif
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
#endif
#ifdef ENABLE_MECON_IFACE
      }
      MeconIface::freeSnapshot(snap);
#endif
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        recordFactoryReset(Posture::CALLER_CABLE);
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      Serial.println("  Error: unknown command");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    // the reply, and the trust decision, belong to the interface this frame arrived on (a multiplexed interface)
    _cmd_src = _serial->rxSource();
    _serial->bindReply(_cmd_src);
    handleCmdFrame(len);
    _serial->bindReply(nullptr);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    _serial->bindReply(_iter_src);   // the rest of the contact list goes to whoever asked for it
    if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
    _serial->bindReply(nullptr);
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

#ifdef ENABLE_CONNMODE
// The reply to the command (or the "restarting" screen) goes out first; the restart follows a moment later.
void MyMesh::scheduleConnectivityRestart() {
  _connmode_restart_at = millis() + 1500;
  if (_connmode_restart_at == 0) _connmode_restart_at = 1;
  _connmode_reply_owed = true;   // the next reply written, the one to this request, counts the 1.5 s again (#226)
}

bool MyMesh::requestConnectivityMode(uint8_t want, uint8_t caller) {
  if (!ConnMode::requestSwitch(_prefs.connectivity_mode, want, caller, getRTCClock()->getCurrentTime())) return false;
  savePrefs();
  auditStore().flush(millis());
  scheduleConnectivityRestart();
  return true;
}
#endif

void MyMesh::loop() {
  Uptime::ms(millis());   // every pass, so the 64-bit uptime never misses a wrap of millis() (#165)
  { TimeScope ts("mesh.radio"); BaseChatMesh::loop(); }   // the sections of a loop pass (helpers/PassBlame.h)
  { TimeScope ts("mesh.audit"); auditStore().service(millis()); }
#ifdef ENABLE_CONNMODE
  if (_connmode_restart_at != 0 && millisHasNowPassed(_connmode_restart_at)) {
    _connmode_restart_at = 0;
    board.reboot();   // starts the stack of the new mode; the mode is already saved
  }
#endif

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    { TimeScope ts("mesh.serial"); checkSerialInterface(); }
#ifdef ENABLE_MECON_IFACE
    { TimeScope ts("mesh.iface"); _iface.loop(millis()); }
#endif
#if defined(ENABLE_WIFI_INTERFACE) && defined(RP2040_PLATFORM) && !defined(ENABLE_USB_INTERFACE)
    // RP2040 WiFi builds are headless and have no way into the rescue CLI (that needs a
    // display + long-press), so serve config commands on the otherwise unused USB serial
    checkCLIRescueCmd();
#endif
  }

  // is there are pending dirty contacts write needed?
  // Not while a request is arriving in frames: one bucket's write holds the loop for up to about a second on a Heltec
  // V3, which overflows its UART's 1 KB receive ring and loses the request (#519). The save waits for the next pass.
  bool receiving = false;
#ifdef ENABLE_MECON_IFACE
  receiving = _iface.rxPartial();
#endif
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry) && !receiving) {
    TimeScope ts("mesh.contacts");
#ifdef CONTACTS_STOCK_ARRAY
    saveContacts();
    dirty_contacts_expiry = 0;
#else
    // One bucket per pass: after a burst of adds, a flush of every bucket at once held the loop for seconds.
    // The timer stays due until the last changed bucket is written. After a write error it is set again a few
    // times, further apart each time (LazySaveRetry); then the save waits for the next change.
    bool more = false;
    bool ok = contactStore().flushSome(1, &more);
    if (ok) _contacts_retries = 0;
    uint32_t retry = ok ? 0 : LazySaveRetry::delayMs(++_contacts_retries);
    if (retry) {
      dirty_contacts_expiry = futureMillis((int)retry);
    } else if (!ok || !more) {
      dirty_contacts_expiry = 0;
      _contacts_retries = 0;
    }
#endif
  }
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundTotal() > 0 || dirty_contacts_expiry != 0 || Posture::audit().dirty()
#ifdef ENABLE_CONNMODE
         || _connmode_restart_at != 0
#endif
      ;
}
