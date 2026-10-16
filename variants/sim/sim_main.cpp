// Entry point of a simulated node: parses options, then runs the example's
// unmodified setup()/loop().
#include <Arduino.h>
#include <unistd.h>
#include <signal.h>
#include "sim_config.h"
#include "sim_clock.h"
#include "sim_toa.h"

void setup();
void loop();

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--toa" && i + 5 < argc) {   // --toa SF BW_KHZ CR PREAMBLE LEN : print microseconds and exit
      int sf = atoi(argv[i + 1]); double bw = atof(argv[i + 2]); int cr = atoi(argv[i + 3]);
      int pre = atoi(argv[i + 4]); int len = atoi(argv[i + 5]);
      printf("%.3f\n", sim_toa_us(sf, bw, cr, pre, len));
      return 0;
    }
    else if (a == "--dir") g_sim.dir = next();
    else if (a == "--ether") g_sim.ether = next();
    else if (a == "--name") g_sim.name = next();
    else if (a == "--port-file") g_sim.port_file = next();
    else if (a == "--seed") g_sim.seed = strtoul(next(), nullptr, 10);
    else if (a == "--time-scale") g_sim.time_scale = atof(next());
    else if (a == "--wifi-env") g_sim.wifi_env = next();
    else if (a == "--mqtt-env") g_sim.mqtt_env = next();
    else if (a == "--ui-frames") g_sim.ui_frames = next();
    else if (a == "--ui-input") g_sim.ui_input = next();
    else if (a == "--ble-env") g_sim.ble_env = next();
    else if (a == "--heap-budget") g_sim.heap_budget = strtoul(next(), nullptr, 10);
    else if (a == "--fs-size") g_sim.fs_size = strtoul(next(), nullptr, 10);
    else if (a == "--rtc") g_sim.rtc_epoch = strtoul(next(), nullptr, 10);
    else if (a == "--fault") {   // --fault /path:BYTES
      std::string v = next(); size_t c = v.rfind(':');
      if (c != std::string::npos) { g_sim.fault_path = v.substr(0, c); g_sim.fault_after = atol(v.c_str() + c + 1); }
    }
    else if (a == "--fault-before") {   // --fault-before rename:/path | remove:/path
      std::string v = next(); size_t c = v.find(':');
      if (c != std::string::npos) { g_sim.fault_op = v.substr(0, c); g_sim.fault_op_path = v.substr(c + 1); }
    }
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (g_sim.dir.empty()) { fprintf(stderr, "--dir is required\n"); return 2; }
  randomSeed(g_sim.seed);
  setup();
  for (;;) {
    loop();
    sim_poll();
    usleep(200);
  }
}
