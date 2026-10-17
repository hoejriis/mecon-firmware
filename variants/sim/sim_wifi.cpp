// Simulated Wi-Fi environment. The access points come from a small text file a test rewrites at run time
// (--wifi-env): one line per AP, "ssid<TAB>password<TAB>rssi<TAB>up". An empty password is an open network.
// The node-side API is the ESP32 Arduino subset in shims/WiFi.h; the events mimic the ESP32 reasons
// (201 AP not found, 202 auth failure, 200 beacon timeout, 8 we left). Every transition is logged to stderr
// with the SSID only: the simulated radio never prints a password.
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <sys/stat.h>
#include <vector>
#include <stdio.h>
#include <string.h>
#include "sim_config.h"

namespace {
const unsigned long SCAN_MS = 300;      // virtual ms a scan takes
const unsigned long CONNECT_MS = 400;   // virtual ms from begin() to the outcome

struct Ap { std::string ssid, pwd; int rssi; bool up; };
std::vector<Ap> g_aps;
long long g_sig_ns = -1, g_sig_size = -1, g_sig_ino = -1;
unsigned long g_last_check = 0;

enum Phase { IDLE, CONNECTING, CONNECTED };
Phase g_phase = IDLE;
int g_status = WL_DISCONNECTED;
std::string g_ssid, g_pwd;
unsigned long g_phase_at = 0;
void (*g_cb)(WiFiEvent_t, WiFiEventInfo_t) = nullptr;

// The scan hardware and the two views of its result. The driver holds a result list until somebody reads it
// (esp_wifi_scan_get_ap_records frees it); Arduino's WiFiScanClass reads it from the done event and keeps its own
// table (WiFi.scanComplete/SSID/RSSI). Faults a test can inject (line "#scan<TAB>mode" in the environment file):
//   ok     a scan takes SCAN_MS and both views see it
//   wedge  the next scan never ends and the driver refuses new scans until esp_wifi_scan_stop (then back to ok)
//   deaf   the done event reaches the event handler but not Arduino's tables (the driver keeps the list)
bool g_scan_running = false, g_drv_busy = false, g_hang = false, g_wedge_armed = false, g_deaf = false;
bool g_drv_list = false, g_ard_have = false;
std::string g_scan_mode = "ok";
unsigned long g_scan_at = 0;
std::vector<std::pair<std::string, int>> g_scan;       // what the last scan found
esp_event_handler_t g_scan_done_cb = nullptr;
void* g_scan_done_arg = nullptr;

void reload() {
  if (g_sim.wifi_env.empty()) return;
  struct stat st;
  if (stat(g_sim.wifi_env.c_str(), &st) != 0) return;
  long long ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
  if (ns == g_sig_ns && (long long)st.st_size == g_sig_size && (long long)st.st_ino == g_sig_ino) return;
  FILE* f = fopen(g_sim.wifi_env.c_str(), "r");
  if (!f) return;
  g_sig_ns = ns; g_sig_size = st.st_size; g_sig_ino = st.st_ino;
  std::vector<Ap> aps;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, "#scan\t", 6) == 0) {
      g_deaf = strcmp(line + 6, "deaf") == 0;
      if (g_scan_mode != line + 6) {   // "wedge" is armed once per change of mode, not by every rewrite of the file
        g_scan_mode = line + 6;
        g_wedge_armed = g_scan_mode == "wedge";
      }
      continue;
    }
    char* t1 = strchr(line, '\t');
    if (!t1) continue;
    *t1++ = 0;
    char* t2 = strchr(t1, '\t');
    if (!t2) continue;
    *t2++ = 0;
    char* t3 = strchr(t2, '\t');
    if (!t3) continue;
    *t3++ = 0;
    aps.push_back({line, t1, atoi(t2), atoi(t3) != 0});
  }
  fclose(f);
  g_aps.swap(aps);
}

const Ap* find(const std::string& ssid) {
  reload();
  for (auto& a : g_aps) if (a.up && a.ssid == ssid) return &a;
  return nullptr;
}

void fire(int ev, uint8_t reason) {
  if (!g_cb) return;
  WiFiEventInfo_t info;
  info.wifi_sta_disconnected.reason = reason;
  g_cb(ev, info);
}
}  // namespace

SimWiFi WiFi;
const char* WIFI_EVENT_BASE_NAME = "WIFI_EVENT";

std::string SimIPAddress::toString() const { return WiFi.status() == WL_CONNECTED ? "192.0.2.10" : "0.0.0.0"; }

void SimWiFi::onEvent(void (*cb)(WiFiEvent_t, WiFiEventInfo_t)) { g_cb = cb; }

void SimWiFi::begin(const char* ssid, const char* pwd) {
  g_ssid = ssid;
  g_pwd = pwd;
  g_phase = CONNECTING;
  g_status = WL_DISCONNECTED;
  g_phase_at = millis();
  fprintf(stderr, "SIMWIFI join ssid=%s\n", ssid);
  fflush(stderr);
}

void SimWiFi::disconnect(bool) {
  if (g_phase == IDLE) return;
  bool was_up = g_phase == CONNECTED;
  g_phase = IDLE;
  g_status = WL_DISCONNECTED;
  fprintf(stderr, "SIMWIFI leave ssid=%s%s\n", g_ssid.c_str(), was_up ? "" : " (join abandoned)");
  fflush(stderr);
  fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 8);   // WIFI_REASON_ASSOC_LEAVE
}

int SimWiFi::status() { return g_status; }

int32_t SimWiFi::RSSI() {
  if (g_phase != CONNECTED) return 0;
  const Ap* a = find(g_ssid);
  return a ? a->rssi : 0;
}

SimIPAddress SimWiFi::localIP() { return SimIPAddress(); }

// Arduino's view of the last scan: the table its own done handler filled (empty when it never saw the event).
int16_t SimWiFi::scanComplete() { return g_ard_have ? (int16_t)g_scan.size() : WIFI_SCAN_FAILED; }
void SimWiFi::scanDelete() { g_ard_have = false; }
std::string SimWiFi::SSID(uint8_t i) { return g_ard_have && i < g_scan.size() ? g_scan[i].first : ""; }
int32_t SimWiFi::RSSI(uint8_t i) { return g_ard_have && i < g_scan.size() ? g_scan[i].second : 0; }

// The ESP-IDF scan API the controller drives directly (variants/sim/shims/esp_wifi.h).
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool) {
  if (g_drv_busy) {   // the driver is still scanning (or wedged): it refuses a second scan
    fprintf(stderr, "SIMSCAN refused\n");
    fflush(stderr);
    return ESP_ERR_WIFI_STATE;
  }
  reload();
  g_drv_busy = g_scan_running = true;
  g_hang = g_wedge_armed;
  g_wedge_armed = false;
  g_scan_at = millis();
  fprintf(stderr, "SIMSCAN start%s\n", g_hang ? " (will wedge)" : "");
  fflush(stderr);
  return ESP_OK;
}

esp_err_t esp_wifi_scan_stop() {
  fprintf(stderr, "SIMSCAN stop\n");
  fflush(stderr);
  g_drv_busy = g_scan_running = g_hang = false;
  return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t* n) {
  *n = g_drv_list ? (uint16_t)g_scan.size() : 0;
  return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t* n, wifi_ap_record_t* rec) {
  if (!g_drv_list) { *n = 0; return ESP_OK; }
  uint16_t k = 0;
  for (; k < *n && k < g_scan.size(); k++) {
    memset(&rec[k], 0, sizeof rec[k]);
    strncpy((char*)rec[k].ssid, g_scan[k].first.c_str(), sizeof(rec[k].ssid) - 1);
    rec[k].rssi = (int8_t)g_scan[k].second;
  }
  *n = k;
  g_drv_list = false;   // reading frees the driver's list
  return ESP_OK;
}

esp_err_t esp_event_handler_register(esp_event_base_t, int32_t id, esp_event_handler_t cb, void* arg) {
  if (id == WIFI_EVENT_SCAN_DONE) { g_scan_done_cb = cb; g_scan_done_arg = arg; }
  return ESP_OK;
}

// Called from the node's main loop (sim_poll()).
void sim_wifi_poll() {
  unsigned long now = millis();
  if (g_scan_running && !g_hang && (unsigned long)(now - g_scan_at) >= SCAN_MS) {
    reload();
    g_scan.clear();
    for (auto& a : g_aps) if (a.up) g_scan.push_back({a.ssid, a.rssi});
    g_scan_running = g_drv_busy = false;
    g_drv_list = true;
    g_ard_have = !g_deaf;
    if (g_ard_have) g_drv_list = false;   // Arduino's handler read (and freed) the driver's list
    fprintf(stderr, "SIMSCAN done n=%d%s\n", (int)g_scan.size(), g_deaf ? " (arduino deaf)" : "");
    fflush(stderr);
    if (g_scan_done_cb) {
      wifi_event_sta_scan_done_t ev = {0, (uint8_t)g_scan.size(), 1};
      g_scan_done_cb(g_scan_done_arg, WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &ev);
    }
  }
  if (g_phase == CONNECTING && (unsigned long)(now - g_phase_at) >= CONNECT_MS) {
    const Ap* a = find(g_ssid);
    if (!a) {
      g_phase = IDLE; g_status = WL_NO_SSID_AVAIL;
      fprintf(stderr, "SIMWIFI fail ssid=%s reason=notfound\n", g_ssid.c_str());
      fflush(stderr);
      fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 201);   // WIFI_REASON_NO_AP_FOUND
    } else if (!a->pwd.empty() && a->pwd != g_pwd) {
      g_phase = IDLE; g_status = WL_CONNECT_FAILED;
      fprintf(stderr, "SIMWIFI fail ssid=%s reason=auth\n", g_ssid.c_str());
      fflush(stderr);
      fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 202);   // WIFI_REASON_AUTH_FAIL
    } else {
      g_phase = CONNECTED; g_status = WL_CONNECTED;
      fprintf(stderr, "SIMWIFI up ssid=%s\n", g_ssid.c_str());
      fflush(stderr);
      fire(ARDUINO_EVENT_WIFI_STA_GOT_IP, 0);
    }
  } else if (g_phase == CONNECTED && (unsigned long)(now - g_last_check) >= 50) {
    g_last_check = now;
    const Ap* a = find(g_ssid);
    if (!a || (!a->pwd.empty() && a->pwd != g_pwd)) {
      g_phase = IDLE; g_status = WL_DISCONNECTED;
      fprintf(stderr, "SIMWIFI lost ssid=%s\n", g_ssid.c_str());
      fflush(stderr);
      fire(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 200);   // WIFI_REASON_BEACON_TIMEOUT
    }
  }
}
