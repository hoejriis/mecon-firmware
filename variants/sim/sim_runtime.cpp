// Clock, RNG and serial port of the simulated board.
#include <Arduino.h>
#include <random>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include "sim_config.h"
#include "sim_clock.h"

SimConfig& sim_cfg() { static SimConfig c; return c; }

// ---------------------------------------------------------------- clock
namespace {
class RealClock : public SimClock {
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
public:
  uint64_t nowUs() override {
    auto d = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    return (uint64_t)(d * g_sim.time_scale);
  }
  void sleepUs(uint64_t us) override {
    std::this_thread::sleep_for(std::chrono::microseconds((uint64_t)(us / g_sim.time_scale)));
  }
};
SimClock* g_override = nullptr;
RealClock& real() { static RealClock c; return c; }   // constructed on first use
}  // namespace

SimClock* sim_real_clock() { return &real(); }
void sim_set_clock(SimClock* c) { g_override = c; }
SimClock* sim_clock() { return g_override ? g_override : &real(); }

unsigned long millis() { return (unsigned long)(uint32_t)(sim_clock()->nowUs() / 1000); }
unsigned long micros() { return (unsigned long)(uint32_t)sim_clock()->nowUs(); }
void delay(unsigned long ms) { sim_poll(); sim_clock()->sleepUs((uint64_t)ms * 1000); }
void delayMicroseconds(unsigned int us) { sim_clock()->sleepUs(us); }

// ---------------------------------------------------------------- random
static std::mt19937 g_rng(1);
void randomSeed(unsigned long seed) { g_rng.seed((uint32_t)seed); }
long random(long maxv) { return maxv <= 0 ? 0 : (long)(g_rng() % (uint32_t)maxv); }
long random(long minv, long maxv) { return maxv <= minv ? minv : minv + random(maxv - minv); }

// ---------------------------------------------------------------- serial (TCP)
// The USB serial port is a loopback TCP listener. The Companion frame protocol
// and the repeater text CLI travel over it byte for byte, unchanged.
namespace {
int g_listen = -1, g_client = -1;
uint8_t g_rx[4096]; size_t g_rx_len = 0, g_rx_pos = 0;

void serial_open() {
  if (g_listen >= 0) return;
  g_listen = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
  if (bind(g_listen, (sockaddr*)&a, sizeof a) != 0 || listen(g_listen, 1) != 0) { perror("serial bind"); exit(2); }
  socklen_t l = sizeof a; getsockname(g_listen, (sockaddr*)&a, &l);
  fcntl(g_listen, F_SETFL, O_NONBLOCK);
  if (!g_sim.port_file.empty()) {
    std::string tmp = g_sim.port_file + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f) { fprintf(f, "%d\n", ntohs(a.sin_port)); fclose(f); rename(tmp.c_str(), g_sim.port_file.c_str()); }
  }
}

void serial_pump() {
  serial_open();
  if (g_client < 0) {
    int c = accept(g_listen, nullptr, nullptr);
    if (c >= 0) {
      int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
      fcntl(c, F_SETFL, O_NONBLOCK);
      g_client = c; g_rx_len = g_rx_pos = 0;
    }
  }
  if (g_client >= 0 && g_rx_pos >= g_rx_len) {
    g_rx_pos = g_rx_len = 0;
    ssize_t n = recv(g_client, g_rx, sizeof g_rx, 0);
    if (n > 0) g_rx_len = n;
    else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { close(g_client); g_client = -1; }
  }
}
}  // namespace

size_t SimSerial::write(uint8_t b) { return write(&b, 1); }
size_t SimSerial::write(const uint8_t* buf, size_t n) {
  serial_pump();
  if (g_client < 0) return n;  // nobody listening: bytes are dropped, like a USB port with no host
  size_t off = 0;
  while (off < n) {
    ssize_t w = send(g_client, buf + off, n - off, MSG_NOSIGNAL);
    if (w > 0) off += w;
    else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(100);
    else { close(g_client); g_client = -1; break; }
  }
  return n;
}
SimSerial::operator bool() const { serial_pump(); return g_client >= 0; }
int SimSerial::available() { serial_pump(); return (int)(g_rx_len - g_rx_pos); }
int SimSerial::read() { serial_pump(); return g_rx_pos < g_rx_len ? g_rx[g_rx_pos++] : -1; }
int SimSerial::peek() { serial_pump(); return g_rx_pos < g_rx_len ? g_rx[g_rx_pos] : -1; }

SimSerial Serial;

void sim_radio_poll();
void sim_wifi_poll();
void sim_poll() { serial_pump(); sim_radio_poll(); sim_wifi_poll(); }
