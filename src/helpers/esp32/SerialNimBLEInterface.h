#pragma once

// NimBLE twin of SerialBLEInterface (Bluedroid), selected with -D USE_NIMBLE. Same public interface, same Nordic UART
// service, same static-passkey pairing with the device PIN, same bonding and the same transport trust rules: the
// Companion (examples/companion_radio/main.cpp) uses either class unchanged. Where NimBLE's callbacks differ from
// Bluedroid's, the .cpp maps each one explicitly. See docs/NIMBLE_EVALUATION.md.

#include "../BaseSerialInterface.h"
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class SerialNimBLEInterface : public BaseSerialInterface, NimBLEServerCallbacks, NimBLECharacteristicCallbacks {
  NimBLEServer *pServer;
  NimBLEService *pService;
  NimBLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;     // NimBLE connection handle (Bluedroid: conn_id)
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  StaticQueue_t recv_queue_state;
  uint8_t recv_queue_storage[FRAME_QUEUE_SIZE * sizeof(Frame)];
  QueueHandle_t recv_queue;
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();

protected:
  // NimBLEServerCallbacks methods (NimBLE folds Bluedroid's BLESecurityCallbacks into the server callbacks)
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override;
  uint32_t onPassKeyDisplay() override;
  void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pass_key) override;
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;

  // NimBLECharacteristicCallbacks methods
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override;

public:
  // Paired BLE authenticates the peer only when the PIN is not the well-known default (123456).
  // Identical to the Bluedroid class: trust depends on the PIN alone, never on the stack.
  bool isTrustedTransport() const override { return _pin_code != 0 && _pin_code != 123456; }
  bool isBleTransport() const override { return true; }
  SerialNimBLEInterface() {
    pServer = NULL;
    pService = NULL;
    pTxCharacteristic = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    _pin_code = 0;
    recv_queue = xQueueCreateStatic(
      FRAME_QUEUE_SIZE, sizeof(Frame), recv_queue_storage, &recv_queue_state
    );
    send_queue_len = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#ifndef BLE_DEBUG_PRINT
#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
#endif
