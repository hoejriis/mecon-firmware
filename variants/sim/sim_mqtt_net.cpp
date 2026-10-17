// Simulated network for the MQTT controller. Real sockets on the host, so a real Mosquitto (started by the test) is the
// broker; TLS is OpenSSL; DNS and mDNS are small tables a test rewrites at run time (--mqtt-env), one line each:
//   host<TAB>name<TAB>ipv4                         a DNS name and the address it resolves to
//   mdns<TAB>secure|plain<TAB>site<TAB>ipv4<TAB>port   a responder for _secure-mqtt._tcp or _mqtt._tcp with TXT site=<site>
// Removing a line withdraws the responder. The link is up while the simulated Wi-Fi is joined.
// Every transport event is logged with addresses only: the simulated network never prints a credential.
//
// Interleaving hook (sim only): a test makes a timing-dependent interleaving deterministic by pausing the connect job
// at a named step and acting on the main loop while the job is provably stopped there:
//   pause<TAB>dns|tcp|tls|connack      the job stops at that step for as long as the row is in the file
// The steps are: after the name was resolved, after the TCP connect, after the TLS handshake and pin check, and before
// the first byte of the MQTT CONNECT is written. The job logs "SIMMQTT paused <step>" when it has stopped and
// "SIMMQTT released <step>" when it carries on; removing the row releases it. A step the job does not take (no name to
// resolve, no TLS) is not a stop. Nothing here is compiled outside the simulator.
#ifdef ENABLE_MQTT
#include <helpers/mqtt/MqttNet.h>
#include <WiFi.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <strings.h>
#include "sim_config.h"

namespace {
struct Row { std::string kind, a, b, c, d; };
std::vector<Row> g_rows;
std::mutex g_rows_mu;   // the name service is read by the connect worker (resolve) and the main loop (browse)
long long g_sig_ns = -1, g_sig_size = -1, g_sig_ino = -1;

void reload() {   // callers hold g_rows_mu
  if (g_sim.mqtt_env.empty()) return;
  struct stat st;
  if (stat(g_sim.mqtt_env.c_str(), &st) != 0) return;
  long long ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
  if (ns == g_sig_ns && (long long)st.st_size == g_sig_size && (long long)st.st_ino == g_sig_ino) return;
  FILE* f = fopen(g_sim.mqtt_env.c_str(), "r");
  if (!f) return;
  g_sig_ns = ns; g_sig_size = st.st_size; g_sig_ino = st.st_ino;
  std::vector<Row> rows;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    std::vector<std::string> f5;
    char* p = line;
    for (;;) {
      char* t = strchr(p, '\t');
      if (t) *t = 0;
      f5.push_back(p);
      if (!t) break;
      p = t + 1;
    }
    f5.resize(5);
    rows.push_back({f5[0], f5[1], f5[2], f5[3], f5[4]});
  }
  fclose(f);
  g_rows.swap(rows);
}

// Is a pause armed for `step`? Reads the file itself every time (the table above is cached by modification time, which a
// rewrite within the same clock tick would not change).
bool pauseArmed(const char* step) {
  if (g_sim.mqtt_env.empty()) return false;
  FILE* f = fopen(g_sim.mqtt_env.c_str(), "r");
  if (!f) return false;
  bool armed = false;
  char line[256];
  char want[32];
  snprintf(want, sizeof want, "pause\t%s", step);
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strcmp(line, want) == 0) { armed = true; break; }
  }
  fclose(f);
  return armed;
}

// Called by the connect job at a step boundary. A test that forgets to release does not hang the node for ever.
void pausePoint(const char* step) {
  if (!pauseArmed(step)) return;
  fprintf(stderr, "SIMMQTT paused %s\n", step);
  fflush(stderr);
  for (int i = 0; i < 24000 && pauseArmed(step); i++) usleep(5000);   // at most 120 s
  fprintf(stderr, "SIMMQTT released %s\n", step);
  fflush(stderr);
}

bool parseAddr(const std::string& s, uint32_t& out) {
  in_addr a;
  if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
  out = ntohl(a.s_addr);
  return true;
}

std::string addrStr(uint32_t a) {
  char b[24];
  snprintf(b, sizeof b, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
  return b;
}

// Real milliseconds for a budget given in node (virtual) milliseconds.
int realMs(uint32_t node_ms) {
  double r = node_ms / (g_sim.time_scale > 0 ? g_sim.time_scale : 1.0);
  return r < 50 ? 50 : (int)r;
}

class SimClient : public Client {
  int _fd = -1;
  SSL_CTX* _ctx = nullptr;
  SSL* _ssl = nullptr;
  bool _eof = false;
  bool _first_write = false;   // the next write is the first byte of the MQTT CONNECT (the "connack" stop)

  bool waitFd(short ev, int ms) {
    pollfd p{_fd, ev, 0};
    return poll(&p, 1, ms) > 0 && (p.revents & (ev | POLLHUP | POLLERR));
  }

public:
  ~SimClient() { stop(); }

  MqttOpen open(bool tls, uint32_t addr, uint16_t port, const char* anchor, uint32_t timeout_ms) {
    stop();
    _eof = false;
    _first_write = true;
    fprintf(stderr, "SIMMQTT open addr=%s:%u tls=%d\n", addrStr(addr).c_str(), (unsigned)port, tls ? 1 : 0);
    fflush(stderr);
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_fd < 0) return MqttOpen::Tcp;
    fcntl(_fd, F_SETFL, O_NONBLOCK);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(addr);
    sa.sin_port = htons(port);
    int rc = ::connect(_fd, (sockaddr*)&sa, sizeof sa);
    if (rc != 0 && errno != EINPROGRESS) { stop(); return fail("tcp", MqttOpen::Tcp); }
    if (rc != 0) {
      if (!waitFd(POLLOUT, realMs(timeout_ms))) { stop(); return fail("tcp", MqttOpen::Tcp); }
      int err = 0; socklen_t l = sizeof err;
      getsockopt(_fd, SOL_SOCKET, SO_ERROR, &err, &l);
      if (err) { stop(); return fail("tcp", MqttOpen::Tcp); }
    }
    int one = 1;
    setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    pausePoint("tcp");
    if (!tls) { fprintf(stderr, "SIMMQTT up plain\n"); fflush(stderr); return MqttOpen::Ok; }

    _ctx = SSL_CTX_new(TLS_client_method());
    if (!_ctx) { stop(); return fail("tls", MqttOpen::Tls); }
    SSL_CTX_set_verify(_ctx, SSL_VERIFY_NONE, nullptr);   // authenticated by the pinned fingerprint below, as on the device
    _ssl = SSL_new(_ctx);
    SSL_set_fd(_ssl, _fd);
    SSL_set_connect_state(_ssl);
    uint64_t t0 = (uint64_t)millis();
    for (;;) {
      int r = SSL_connect(_ssl);
      if (r == 1) break;
      int e = SSL_get_error(_ssl, r);
      if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) { stop(); return fail("tls", MqttOpen::Tls); }
      if ((uint64_t)millis() - t0 > timeout_ms) { stop(); return fail("tls", MqttOpen::Tls); }
      waitFd(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, 100);
    }
    if (anchor) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      X509* cert = SSL_get1_peer_certificate(_ssl);
#else
      X509* cert = SSL_get_peer_certificate(_ssl);
#endif
      unsigned char md[EVP_MAX_MD_SIZE];
      unsigned int mdl = 0;
      char hex[2 * EVP_MAX_MD_SIZE + 1] = {0};
      if (cert && X509_digest(cert, EVP_sha256(), md, &mdl) == 1) {
        for (unsigned i = 0; i < mdl; i++) sprintf(hex + 2 * i, "%02x", md[i]);
      }
      if (cert) X509_free(cert);
      if (strcasecmp(hex, anchor) != 0) { stop(); return fail("pin", MqttOpen::Pin); }
    }
    pausePoint("tls");
    fprintf(stderr, "SIMMQTT up tls pinned=%d\n", anchor ? 1 : 0);
    fflush(stderr);
    return MqttOpen::Ok;
  }

  MqttOpen fail(const char* why, MqttOpen r) {
    fprintf(stderr, "SIMMQTT open-fail %s\n", why);
    fflush(stderr);
    return r;
  }

  int connect(IPAddress, uint16_t) override { return 0; }   // the controller opens the transport itself
  int connect(const char*, uint16_t) override { return 0; }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    if (_fd < 0) return 0;
    if (_first_write) { _first_write = false; pausePoint("connack"); }
    size_t off = 0;
    uint64_t t0 = (uint64_t)millis();
    while (off < n && !_eof) {
      if (_ssl) {
        int w = SSL_write(_ssl, buf + off, (int)(n - off));
        if (w > 0) { off += w; continue; }
        int e = SSL_get_error(_ssl, w);
        if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) waitFd(e == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN, 20);
        else { _eof = true; break; }
      } else {
        ssize_t w = send(_fd, buf + off, n - off, MSG_NOSIGNAL);
        if (w > 0) { off += w; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) waitFd(POLLOUT, 20);
        else { _eof = true; break; }
      }
      if ((uint64_t)millis() - t0 > 20000) { _eof = true; break; }
    }
    return off;
  }

  int available() override {
    if (_fd < 0 || _eof) return 0;
    if (_ssl) {
      int p = SSL_pending(_ssl);
      if (p > 0) return p;
      if (!waitFd(POLLIN, 0)) return 0;
      char b;
      int r = SSL_peek(_ssl, &b, 1);
      if (r > 0) { p = SSL_pending(_ssl); return p > 0 ? p : 1; }
      int e = SSL_get_error(_ssl, r);
      if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) _eof = true;
      return 0;
    }
    int cnt = 0;
    if (!waitFd(POLLIN, 0)) return 0;
    if (ioctl(_fd, FIONREAD, &cnt) == 0 && cnt > 0) return cnt;
    char b;
    ssize_t r = recv(_fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) _eof = true;
    return 0;
  }

  int read() override {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  int read(uint8_t* buf, size_t n) override {
    if (_fd < 0 || _eof) return -1;
    if (_ssl) {
      int r = SSL_read(_ssl, buf, (int)n);
      if (r > 0) return r;
      int e = SSL_get_error(_ssl, r);
      if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) _eof = true;
      return -1;
    }
    ssize_t r = recv(_fd, buf, n, MSG_DONTWAIT);
    if (r > 0) return (int)r;
    if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) _eof = true;
    return -1;
  }
  int peek() override { return -1; }
  void flush() override { }
  void stop() override {
    if (_ssl) { if (!_eof) SSL_shutdown(_ssl); SSL_free(_ssl); _ssl = nullptr; }
    if (_ctx) { SSL_CTX_free(_ctx); _ctx = nullptr; }
    if (_fd >= 0) { ::close(_fd); _fd = -1; fprintf(stderr, "SIMMQTT close\n"); fflush(stderr); }
    _eof = true;
  }
  uint8_t connected() override {
    if (_fd < 0) return 0;
    available();   // notices a closed peer
    return _eof ? 0 : 1;
  }
  operator bool() override { return _fd >= 0; }
};

// The connect worker, modelled as on the device: one thread, created on the first job, reused, running one job at a
// time while the main loop carries on. It is never joined (the node exits with the process).
class SimWorker {
  std::mutex _mu;
  std::condition_variable _cv;
  void (*_job)(void*) = nullptr;
  void* _arg = nullptr;
  bool _go = false;
  std::atomic<bool> _busy{false};
  bool _started = false;

  void run() {
    for (;;) {
      void (*job)(void*);
      void* arg;
      {
        std::unique_lock<std::mutex> l(_mu);
        _cv.wait(l, [&] { return _go; });
        _go = false;
        job = _job;
        arg = _arg;
      }
      job(arg);
      _busy.store(false, std::memory_order_release);
    }
  }

public:
  bool start(void (*job)(void*), void* arg) {
    if (_busy.load(std::memory_order_acquire)) return false;
    if (!_started) {
      _started = true;
      std::thread([this] { run(); }).detach();
    }
    _busy.store(true, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> l(_mu);
      _job = job;
      _arg = arg;
      _go = true;
    }
    _cv.notify_one();
    return true;
  }
  bool idle() const { return !_busy.load(std::memory_order_acquire); }
};

class SimNet : public MqttNet {
  SimClient _c;
  SimWorker _w;
  bool _browsing = false;
  unsigned long _browse_at = 0;
public:
  bool asyncStart(void (*job)(void*), void* arg) override { return _w.start(job, arg); }
  bool asyncIdle() override { return _w.idle(); }

  bool linkUp() override { return WiFi.status() == WL_CONNECTED; }

  bool resolve(const char* host, uint32_t& addr) override {
    bool found = false;
    {
      std::lock_guard<std::mutex> l(g_rows_mu);   // not held across the pause: the main loop's browse needs it
      reload();
      for (auto& r : g_rows) {
        if (r.kind == "host" && r.a == host && parseAddr(r.b, addr)) {
          fprintf(stderr, "SIMMQTT resolve name=%s addr=%s\n", host, r.b.c_str());
          fflush(stderr);
          found = true;
          break;
        }
      }
    }
    if (found) { pausePoint("dns"); return true; }
    fprintf(stderr, "SIMMQTT resolve-fail name=%s\n", host);
    fflush(stderr);
    return false;
  }

  void browseBegin() override {
    _browsing = true;
    _browse_at = millis();
    fprintf(stderr, "SIMMQTT browse\n");
    fflush(stderr);
  }

  int browsePoll(MqttResponder* out, int max) override {
    if (!_browsing) return 0;
    if ((unsigned long)(millis() - _browse_at) < 200) return -1;   // a browse takes time and never blocks the loop
    _browsing = false;
    std::lock_guard<std::mutex> l(g_rows_mu);
    reload();
    int n = 0;
    for (auto& r : g_rows) {
      if (r.kind != "mdns" || n >= max) continue;
      uint32_t a;
      if (!parseAddr(r.c, a)) continue;
      out[n].addr = a;
      out[n].port = (uint16_t)atoi(r.d.c_str());
      out[n].secure = r.a == "secure";
      snprintf(out[n].site, sizeof(out[n].site), "%s", r.b.c_str());
      n++;
    }
    return n;
  }

  MqttOpen open(bool tls, uint32_t addr, uint16_t port, const char* anchor, uint32_t timeout_ms) override {
    return _c.open(tls, addr, port, anchor, timeout_ms);
  }
  Client& client() override { return _c; }
  void close() override { _c.stop(); }
};
}  // namespace

MqttNet& mqttPlatformNet() {
  // Never destroyed: the worker thread sleeps on a condition variable, and exit() (reboot, poweroff) would destroy
  // that variable under it, which blocks until the waiter leaves, that is forever.
  static SimNet* n = new SimNet;
  return *n;
}
#endif
