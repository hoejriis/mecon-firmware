#include <Arduino.h>   // needed for PlatformIO
#include <helpers/BootTimes.h>
#include <helpers/BootPresence.h>
#include <Mesh.h>
#include "MyMesh.h"
#include <helpers/HeapFloor.h>

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

// interface manager
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface
#if defined(BLE_PIN_CODE)
  #ifdef ESP32
    // include esp32 bluetooth interface
    #ifdef SIM_BLE_STUB
      #include "sim_ble.h"    // the simulated board has no BLE stack: a stub (variants/sim/sim_ble.h)
    #elif defined(USE_NIMBLE)
      #include <helpers/esp32/SerialNimBLEInterface.h>   // NimBLE twin, same interface (docs/NIMBLE_EVALUATION.md)
      typedef SerialNimBLEInterface SerialBLEInterface;
    #else
      #include <helpers/esp32/SerialBLEInterface.h>
    #endif
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    // include nrf52 bluetooth interface
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(RP2040_PLATFORM)
    // include rp2040 (Pico W / CYW43) bluetooth interface
    #include <helpers/rp2040/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  #ifndef WIFI_PWD
    #define WIFI_PWD ""
  #endif
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #if defined(ESP32) || defined(RP2040_PLATFORM)
    #include <helpers/wifi/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
    #include <helpers/wifi/WifiController.h>
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
  #ifdef ENABLE_MQTT
    #include <helpers/mqtt/MqttController.h>
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface usb_serial_interface;
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

// platform file system
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

#ifdef ENABLE_MQTT
// The MQTT controller hands each request from the broker here (docs/CLI.md).
#ifdef ENABLE_MECON_IFACE
static size_t mqttIfaceRun(void* user, const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap) {
  return static_cast<MyMesh*>(user)->iface().onMqtt(ctx, msg, len, out, cap);
}
#endif
static uint8_t mqttCliRun(void* user, const Cli::Context& ctx, const char* cmd, char* reply, size_t cap) {
  return static_cast<MyMesh*>(user)->runMqttCli(ctx, cmd, reply, cap);
}
#endif

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

/* WIFI: the prioritised profile controller (helpers/wifi/WifiController.h) owns joining and rejoining */
#ifdef ENABLE_WIFI_INTERFACE
  bool wifi_enabled = false;   // set at boot from prefs; false also when no profile is configured
#endif

void setup() {
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  mecon::boot::mark(mecon::boot::STORE, millis());
  the_mesh.begin();
  mecon::boot::mark(mecon::boot::MESH, millis());
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  mecon::boot::mark(mecon::boot::STORE, millis());
  the_mesh.begin();
  mecon::boot::mark(mecon::boot::MESH, millis());
#elif defined(ESP32)
  SPIFFS.begin(true);
  BootPresence::begin(SPIFFS);   // one listing, so boot looks up no file that is not there (helpers/BootPresence.h)
  store.begin();
  mecon::boot::mark(mecon::boot::STORE, millis());
  the_mesh.begin();
  BootPresence::end();
  mecon::boot::mark(mecon::boot::MESH, millis());
#else
  #error "need to define filesystem"
#endif

#ifdef BLE_PIN_CODE // 123456 by default
  if (the_mesh.getNodePrefs()->ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (disp != NULL && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      the_mesh.setBLEPin(rng.nextInt(100000, 999999)); // random pin each session
    } else {
      the_mesh.setBLEPin(BLE_PIN_CODE); // otherwise static pin
    }
#else
    the_mesh.setBLEPin(BLE_PIN_CODE); // otherwise static pin
#endif
  } else {
    the_mesh.setBLEPin(the_mesh.getNodePrefs()->ble_pin);
  }
#else
  the_mesh.setBLEPin(0);
#endif

  // Hardened posture: no BLE with a default PIN, no TCP servers (Wi-Fi, Ethernet). See helpers/Posture.h.
  const uint8_t posture = the_mesh.getNodePrefs()->posture;

#ifdef ENABLE_CONNMODE
  // Connectivity mode: exactly one of the BLE and the Wi-Fi stacks is brought up. USB (below) is up in both.
  const uint8_t conn_mode = the_mesh.getConnectivityMode();
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE)
#ifdef ENABLE_CONNMODE
  if (!ConnMode::bleMayStart(conn_mode)) {
    // Wi-Fi mode: the BLE stack is never initialised
  } else
#endif
  if (Posture::bleMayStart(posture, the_mesh.getBLEPin())) {
    bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
    interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
  } else {
    Posture::audit().addOnce(rtc_clock.getCurrentTime(), Posture::EV_REFUSED, Posture::CALLER_BLE, Posture::W_BLE_START);
  }
#endif

// add wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  // only start wifi if enabled and at least one profile (or the build-time default network) is configured
  wifi_enabled = the_mesh.getNodePrefs()->wifi_enabled;
#ifdef ENABLE_CONNMODE
  if (!ConnMode::wifiMayStart(conn_mode)) wifi_enabled = false;   // BLE mode: the Wi-Fi stack is never initialised
#endif
  const bool wifi_has_profile = the_mesh.getNodePrefs()->wifi_profiles.configuredCount() > 0;
  const bool tcp_may_start = Posture::tcpServerMayStart(posture);
  // Hardened keeps the Wi-Fi link only for MQTT with a profile Hardened permits (TLS); the TCP server stays off.
  bool mqtt_may_run = false;
#ifdef ENABLE_MQTT
  mqtt_may_run = the_mesh.getNodePrefs()->mqtt_enabled && the_mesh.getNodePrefs()->mqtt_profiles.anyPermitted(posture);
#endif
  if (wifi_enabled && wifi_has_profile && !tcp_may_start && !mqtt_may_run) {
    // Hardened: the Wi-Fi link exists only to serve the TCP companion port, so neither is started
    wifi_enabled = false;
    Posture::audit().addOnce(rtc_clock.getCurrentTime(), Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_NET_START);
  }
  if (wifi_enabled && wifi_has_profile) {
#if defined(ESP32)
    board.setInhibitSleep(true);   // prevent sleep when WiFi is active
#endif
    wifiController().begin(&the_mesh.getNodePrefs()->wifi_profiles);
    if (tcp_may_start) {
      wifi_interface.begin(TCP_PORT);
      interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
    } else {
      Posture::audit().addOnce(rtc_clock.getCurrentTime(), Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_NET_START);
    }
#ifdef ENABLE_MQTT
    if (the_mesh.getNodePrefs()->mqtt_enabled) {
      char node_id[9];
      for (int k = 0; k < 4; k++) sprintf(&node_id[k * 2], "%02x", the_mesh.self_id.pub_key[k]);
      mqttController().setAuditClock([]() -> uint32_t { return rtc_clock.getCurrentTime(); });
      mqttController().setCliHandler(&mqttCliRun, &the_mesh);   // the command line over MQTT (docs/CLI.md)
#ifdef ENABLE_MECON_IFACE
      mqttController().setIfaceHandler(&mqttIfaceRun, &the_mesh, the_mesh.iface().deviceIdHex());   // DEVICE_INTERFACE.md
      the_mesh.iface().setMqttEvents(
          [](const char* stream, const char* payload, size_t len) { return mqttController().publishIfaceEvent(stream, payload, len); },
          []() { return mqttController().sessionUp(); });
#endif
      mqttController().begin(&the_mesh.getNodePrefs()->mqtt_profiles, &mqttPlatformNet(),
                             &the_mesh.getNodePrefs()->posture, node_id);
    }
#endif
  } else {
    WIFI_DEBUG_PRINTLN("wifi disabled");
  }
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  if (Posture::tcpServerMayStart(posture)) {
    ethernet_interface.begin();
    interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
  } else {
    Posture::audit().addOnce(rtc_clock.getCurrentTime(), Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_NET_START);
  }
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

  heapFloor().begin(HEAP_FLOOR_BYTES);   // the enforced heap floor (helpers/HeapFloor.h): first look in loop(), then every 60 s

  the_mesh.startInterface(interface_manager);
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
  the_mesh.setListener(&ui_task);
#endif

  board.onBootComplete();
  mecon::boot::mark(mecon::boot::SETUP, millis());
}

void loop() {
  the_mesh.loop();
  heapFloor().loop(millis(), rtc_clock.getCurrentTime());
  interface_manager.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
  board.loop();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#ifdef ENABLE_WIFI_INTERFACE
  if (wifi_enabled) {
    wifiController().loop();
#ifdef ENABLE_MQTT
    mqttController().loop();
#endif
  }
#endif
}
