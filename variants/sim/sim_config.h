// Process-wide configuration of one simulated node, filled from the command line
// by sim_main.cpp and read by the shims. Nothing here is firmware logic.
#pragma once
#include <stdint.h>
#include <string>

struct SimConfig {
  std::string dir;            // per-node data directory (the board filesystem)
  std::string ether;          // unix socket path of the ether broker ("" = radio deaf and mute)
  std::string name = "node";  // node name used when talking to the broker
  std::string port_file;      // file the TCP serial port number is written to
  uint32_t seed = 1;          // seeds randomSeed() and identity generation
  double time_scale = 1.0;    // virtual ms per real ms (1.0 = real time)
  uint32_t rtc_epoch = 1760000000;  // RTC value at process start (unix seconds)
  std::string wifi_env;       // file listing the simulated Wi-Fi access points (variants/sim/sim_wifi.cpp)
  std::string mqtt_env;       // file with the simulated DNS names and mDNS responders (variants/sim/sim_mqtt_net.cpp)
  std::string ui_frames;      // file the recording display appends its frames to (variants/sim/sim_display.cpp)
  std::string ui_input;       // file of button gestures the node replays, one word per line (click|long|double|triple)
  std::string ble_env;        // file whose content "connected" makes the stubbed BLE interface report a connected phone
  uint32_t heap_budget = 300000;   // free heap the node reports at boot (--heap-budget); the contact cache is sized from it
  uint32_t fs_size = 1536 * 1024;  // size of the simulated file system (--fs-size)
  std::string fs_calls;       // file rewritten after every file system call with the running counts (--fs-calls)
  std::string fault_path;     // fault injection: file path (as the app sees it) ...
  long fault_after = -1;      // ... and bytes after which the process is killed
  std::string fault_op;       // power cut just BEFORE a filesystem operation: "rename" or "remove" ...
  std::string fault_op_path;  // ... on this path (the source path for a rename)
};
// Function-local static so it is safe to touch from other translation units' static
// initialisers (the examples build their global mesh object before main()).
SimConfig& sim_cfg();
#define g_sim (sim_cfg())

// Called once per main-loop iteration: accepts serial clients, pumps sockets.
void sim_poll();
