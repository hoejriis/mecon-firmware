// Compiled only with -D USE_NIMBLE. The ESP32 envs pull in helpers/esp32/*.cpp, so in a Bluedroid build this file is an
// empty translation unit and changes nothing.
#ifdef USE_NIMBLE

#include "SerialNimBLEInterface.h"
#include "esp_mac.h"

// Same UUIDs as SerialBLEInterface.cpp: the Nordic UART service the MeshCore apps look for.
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

void SerialNimBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device
  NimBLEDevice::init(dev_name);
  NimBLEDevice::setMTU(MAX_FRAME_SIZE);

  // Security. Bluedroid: BLESecurity::setStaticPIN(pin) + setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND).
  // setStaticPIN sets, in arduino-esp32 2.0.x: the static passkey, IO capability ESP_IO_CAP_OUT (display only), and
  // initiator key distribution ENC|ID. Mapped one by one:
  //   static passkey                -> setSecurityPasskey(pin); onPassKeyDisplay() below also returns it, because
  //                                    NimBLE asks that callback instead when the passkey equals 123456
  //   ESP_IO_CAP_OUT                -> BLE_HS_IO_DISPLAY_ONLY (the central must type the PIN: passkey entry, MITM)
  //   SC_MITM_BOND                  -> setSecurityAuth(bonding=true, mitm=true, sc=true)
  //   key distribution ENC|ID       -> both directions ENC|ID, so a phone using a resolvable private address is
  //                                    recognised as the same bonded peer after it rotates its address
  // Bonds persist in NVS (NimBLE's CONFIG_BT_NIMBLE_NVS_PERSIST, default on), up to CONFIG_BT_NIMBLE_MAX_BONDS (set
  // to 15 in the _nimble envs, Bluedroid's CONFIG_BT_SMP_MAX_BONDS in arduino-esp32 2.0.17). When full, NimBLE's
  // default store callback deletes the oldest bond.
  NimBLEDevice::setSecurityPasskey(pin_code);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  // Create the BLE Server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this, false);   // false: this object owns itself, NimBLE must never delete it
  // Bluedroid never restarts advertising by itself; checkRecvFrame() does it after ADVERT_RESTART_DELAY, and only
  // while enabled. NimBLE's default is the same (off); stated so a library default change cannot alter it.
  pServer->advertiseOnDisconnect(false);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Characteristics. Bluedroid: ESP_GATT_PERM_READ_ENC_MITM / ESP_GATT_PERM_WRITE_ENC_MITM, i.e. an encrypted AND
  // authenticated (MITM, passkey-paired) link. NimBLE: READ_ENC|READ_AUTHEN and WRITE_ENC|WRITE_AUTHEN.
  // The CCCD (Bluedroid: an explicit BLE2902) is created by NimBLE itself for a NOTIFY characteristic.
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);

  NimBLECharacteristic * pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  pRxCharacteristic->setCallbacks(this);

  // Advertising. Bluedroid puts the name and the service UUID in the advertisement and repeats them in the scan
  // response. NimBLE puts nothing but the flags in by default, and a 128-bit UUID plus a long name do not fit one
  // 31-byte packet: the UUID goes in the advertisement (apps filter on it), the name in the scan response.
  NimBLEAdvertising* adv = pServer->getAdvertising();
  adv->enableScanResponse(true);
  adv->addServiceUUID(SERVICE_UUID);
  adv->setName(dev_name);
}

// -------- security callbacks, mapped from Bluedroid's BLESecurityCallbacks

// Bluedroid onPassKeyRequest() (the central asks us to INPUT a passkey; never happens with a display-only device) and
// onPassKeyNotify() (we display the passkey) -> NimBLE onPassKeyDisplay(): the passkey to use. Returns the device PIN,
// so the result is the same whatever value NimBLE's setSecurityPasskey() shortcut holds.
uint32_t SerialNimBLEInterface::onPassKeyDisplay() {
  BLE_DEBUG_PRINTLN("onPassKeyDisplay()");
  return _pin_code;
}

// Bluedroid onConfirmPIN() returned true (accept). It is the numeric-comparison step, which a display-only device never
// reaches. NimBLE requires the answer to be injected; the same answer is given.
void SerialNimBLEInterface::onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPassKey(%u)", pass_key);
  NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

// Bluedroid onSecurityRequest() returned true (allow). It is the peripheral's answer to a slave security request,
// which only a central receives: NimBLE has no peripheral-side equivalent, so there is nothing to map.

// Bluedroid onAuthenticationComplete(cmpl): success -> the link is up (deviceConnected); failure -> disconnect and
// re-advertise after ADVERT_RESTART_DELAY. NimBLE calls this on every encryption change (pairing, or re-encryption
// with a stored bond), success or failure, with no status argument: the link state is read from connInfo instead.
// Success here means encrypted AND authenticated (MITM, i.e. paired with the PIN, now or when the bond was made).
// This is the condition the characteristics' ENC_MITM permissions already require for any read or write; requiring it
// here too means an encrypted but unauthenticated (Just Works) link is dropped like a failed pairing instead of being
// kept connected, so it can never receive a notification either. Stricter than or equal to Bluedroid, never looser.
void SerialNimBLEInterface::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  if (connInfo.isEncrypted() && connInfo.isAuthenticated()) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");

    pServer->disconnect(connInfo.getConnHandle());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- server callbacks

// Bluedroid onConnect(server, param): records conn_id. NimBLE: the connection handle. The link is NOT up yet: as with
// Bluedroid, deviceConnected waits for onAuthenticationComplete().
void SerialNimBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_handle=%d, mtu=%d", connInfo.getConnHandle(), connInfo.getMTU());
  last_conn_id = connInfo.getConnHandle();
}

void SerialNimBLEInterface::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", MTU);
}

void SerialNimBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  BLE_DEBUG_PRINTLN("onDisconnect(), reason=%d", reason);
  deviceConnected = false;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- characteristic callbacks

void SerialNimBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  NimBLEAttValue value = pCharacteristic->getValue();
  const uint8_t* rxValue = value.data();
  int len = value.size();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else {
    Frame frame = {};
    frame.len = len;
    memcpy(frame.buf, rxValue, len);

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
  }
}

// ---------- public methods

void SerialNimBLEInterface::clearBuffers() {
  xQueueReset(recv_queue);
  send_queue_len = 0;
}

void SerialNimBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // Bluedroid starts the service here. NimBLE registers the GATT table once, when advertising first starts
  // (NimBLEAdvertising::start calls NimBLEServer::start), and has no per-service start/stop.
  pServer->getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialNimBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialNimBLEInterface::disable");

  // Bluedroid: stop advertising, disconnect last_conn_id, stop the service. NimBLE has no service stop (only removal,
  // which would rebuild the GATT table): with advertising stopped and the link dropped, no central can reach it.
  // Every connection is dropped, not only the last one, so a second central that connected but never paired is not
  // left behind.
  pServer->getAdvertising()->stop();
  for (uint16_t h : pServer->getPeerDevices()) {
    pServer->disconnect(h);
  }
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
}

size_t SerialNimBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

bool SerialNimBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialNimBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialNimBLEInterface -> disconnecting...");

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialNimBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialNimBLEInterface -> connecting...");
      pServer->getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialNimBLEInterface -> re-starting advertising");
      pServer->getAdvertising()->start();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialNimBLEInterface::isConnected() const {
  return deviceConnected;
}

#endif // USE_NIMBLE
