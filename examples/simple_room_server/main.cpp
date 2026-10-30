#include <Arduino.h>   // needed for PlatformIO
#include <helpers/BootTimes.h>
#include <helpers/BootPresence.h>
#include <Mesh.h>

#include "MyMesh.h"

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Room Server CLI"
  #ifdef SIM_ETHERNET_CONSOLE
    #include <SimEthernetCLI.h>   // the simulated board: same interface, a loopback listener for the W5100S (variants/sim)
  #else
    #include <helpers/nrf52/EthernetCLI.h>
  #endif
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];
#ifdef ENABLE_MECON_IFACE
  // The device interface on the console (docs/contract/DEVICE_INTERFACE.md section 7): "mecon> " lines.
  #include <helpers/iface/ConsoleIface.h>
  #include <helpers/iface/DeviceMeta.h>
  #include <helpers/CryptoRNG.h>
  #include <helpers/deployment/DepFsStore.h>
  #include <helpers/iface/JobFsStore.h>
  static Iface::ConsoleIface the_iface;
  static Dep::FsStore dep_store;   // the Deployment record's files, on the role's own file system
  static Iface::JobFsStore job_store;   // the job table (OPERATIONS.md section 3), on the same file system
  static Iface::SnapFsStore snap_store; // the commit-or-revert snapshot (OPERATIONS.md 3.4)
  static bool ifaceRun(void*, const char* line, char* reply, size_t cap) {
    char cmd[160], out[160];
    snprintf(cmd, sizeof(cmd), "%s", line);
    out[0] = 0;
    the_mesh.handleCommand(0, cmd, out);   // the local console: the CLI gate decides it as the cable
    snprintf(reply, cap, "%s", out);
    memset(out, 0, sizeof(out));
    return strncmp(reply, "??", 2) != 0 && strcmp(reply, "Unknown command") != 0;
  }
  static uint8_t ifacePosture(void*) { return the_mesh.getNodePrefs()->posture; }
  static uint16_t ifaceBattery(void*) { return board.getBattMilliVolts(); }
  static uint32_t ifaceClock(void*) { return the_mesh.getRTCClock()->getCurrentTime(); }
  static void ifaceInventory(void*, Iface::JsonWriter& w) {
    Iface::writeCount(w, "clients", the_mesh.clientCount(), the_mesh.clientCapacity());
  }
  static void ifaceRadioCounts(void*, Iface::RadioCounts* c) {
    c->known = true;
    c->rx_total = radio_driver.getPacketsRecv();
    c->tx_total = radio_driver.getPacketsSent();
    c->rx_errors = radio_driver.getPacketsRecvErrors();
  }
#endif
#ifdef ETHERNET_ENABLED
static char ethernet_command[MAX_POST_TEXT_LEN+1];
#endif

void setup() {
#if defined(ESP_PLATFORM) && defined(ENABLE_MECON_IFACE)
  // A device-interface line is up to 1,600 bytes. Over a board's native USB (Heltec V4) the host delivers it at USB
  // speed, faster than the loop drains the default 256-byte buffer, and the rest is lost; a UART at 115200 is slow enough.
  Serial.setRxBufferSize(2048);
#endif
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  BootPresence::begin(SPIFFS);   // one listing, so boot looks up no file that is not there (helpers/BootPresence.h)
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  sensors.begin();

  the_mesh.begin(fs);
#ifdef ENABLE_MECON_IFACE
  {
    uint8_t device_id[16];
    uint32_t epoch = 0;
    CryptoRNG crng(&fast_rng);   // device_id must not follow from a small seed
    // A failed save still leaves this boot's values, but the epoch may repeat: nothing is signed then (AUTHORITY.md 3.4).
    const bool epoch_saved = Iface::bootDeviceMeta(fs, &crng, device_id, &epoch);
    Iface::ConsoleIface::Host host = { ifaceRun, ifacePosture, ifaceBattery, "room", board.getManufacturerName(),
                                       the_mesh.self_id.pub_key, nullptr,
                                       &the_mesh, &radio_driver, &board, ifaceRadioCounts, &sensors, ifaceInventory,
                                       &dep_store, ifaceClock, &the_mesh.self_id, &job_store, &snap_store };
    dep_store.setFs(fs);
    job_store.setFs(fs);
    snap_store.setFs(fs);
    the_iface.begin(host, device_id, epoch, epoch_saved);
  }
#endif
  BootPresence::end();

  mecon::boot::mark(mecon::boot::MESH, millis());

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
  mecon::boot::mark(mecon::boot::SETUP, millis());
}

void loop() {
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
#ifdef ENABLE_MECON_IFACE
  // A "mecon> " line: past its prefix the interface takes the characters, unechoed, up to its own longer bound.
  while (the_iface.collecting() && Serial.available()) the_iface.feed(Serial.read(), Serial);
  if (the_iface.collecting()) len = 0;
#endif
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
#ifdef ENABLE_MECON_IFACE
    if (len == (int)Iface::ConsoleIface::PREFIX_LEN && memcmp(command, Iface::ConsoleIface::PREFIX, len) == 0) {
      command[0] = 0;
      len = 0;
      the_iface.startLine();
      while (the_iface.collecting() && Serial.available()) the_iface.feed(Serial.read(), Serial);
      break;
    }
#endif
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    // Not the cable: the console is gated as the Ethernet caller (login needed) and never runs as timestamp 0.
    Cli::netConsoleRun(ethernet_console, the_mesh, ADMIN_PASSWORD, rtc_clock.getCurrentTime(), ethernet_command, reply,
                       sizeof(reply), ethernet_handle_command);
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef ENABLE_MECON_IFACE
  the_iface.loop(millis(), Serial);
#endif
  board.loop();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
}
