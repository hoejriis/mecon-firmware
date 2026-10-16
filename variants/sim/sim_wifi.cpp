// Simulated Wi-Fi environment. The access points come from a small text file a test rewrites at run time
// (--wifi-env): one line per AP, "ssid<TAB>password<TAB>rssi<TAB>up". An empty password is an open network.
// The node-side API is the ESP32 Arduino subset in shims/WiFi.h; the events mimic the ESP32 reasons
// (201 AP not found, 202 auth failure, 200 beacon timeout, 8 we left). Every transition is logged to stderr
// with the SSID only: the simulated radio never prints a password.
#include <WiFi.h>
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

bool g_scan_running = false, g_scan_have = false;
unsigned long g_scan_at = 0;
std::vector<std::pair<std::string, int>> g_scan;

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

int16_t SimWiFi::scanNetworks(bool async, bool) {
  g_scan_running = true;
  g_scan_have = false;
  g_scan_at = millis();
  if (!async) { while (g_scan_running) { delay(20); extern void sim_wifi_poll(); sim_wifi_poll(); } return (int16_t)g_scan.size(); }
  return WIFI_SCAN_RUNNING;
}

int16_t SimWiFi::scanComplete() {
  if (g_scan_running) return WIFI_SCAN_RUNNING;
  return g_scan_have ? (int16_t)g_scan.size() : WIFI_SCAN_FAILED;
}

void SimWiFi::scanDelete() { g_scan.clear(); g_scan_have = false; }
std::string SimWiFi::SSID(uint8_t i) { return i < g_scan.size() ? g_scan[i].first : ""; }
int32_t SimWiFi::RSSI(uint8_t i) { return i < g_scan.size() ? g_scan[i].second : 0; }

// Called from the node's main loop (sim_poll()).
void sim_wifi_poll() {
  unsigned long now = millis();
  if (g_scan_running && (unsigned long)(now - g_scan_at) >= SCAN_MS) {
    reload();
    g_scan.clear();
    for (auto& a : g_aps) if (a.up) g_scan.push_back({a.ssid, a.rssi});
    g_scan_running = false;
    g_scan_have = true;
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
