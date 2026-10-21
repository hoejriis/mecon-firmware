// SimRadio: mesh::Radio over a unix-socket "ether" broker.
//
// Wire format (both directions): u32 length (LE, covers type+payload), u8 type, payload.
//   node -> broker  'H' hello      name
//                   'P' params     f32 freq, f32 bw, u8 sf, u8 cr   (receive configuration)
//                   'T' transmit   f32 freq, f32 bw, u8 sf, u8 cr, u32 toa_us, raw packet
//   broker -> node  'B' busy       u32 duration_us   (a transmission this node can hear began)
//                   'R' receive    f32 rssi, f32 snr, raw packet   (delivered at the end of airtime)
// Lock-step virtual time (--lockstep, sim/ether.py with lockstep=True):
//   node -> broker  'L' join       (before 'H'; the broker answers with a 'G' carrying its current time)
//                   'A' advance    u64 abs_us   (done up to now; the next thing to do is due at abs_us)
//   broker -> node  'G' grant      u64 abs_us   (the broker's time; every reception due by then was sent first)
// Under lock-step the socket is read only while waiting for a grant, never inside loop(): a frame becomes visible to
// the firmware at a barrier, whatever the wall-clock moment it arrived, which is what makes a run repeatable.
#include "target.h"
#include "sim_config.h"
#include "sim_clock.h"
#include <deque>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

struct SimRadio::Impl {
  int fd = -1;
  std::vector<uint8_t> inbuf;
  std::deque<Rx> rxq;
  uint64_t busy_until_us = 0;
  uint64_t tx_end_us = 0;
  bool tx_active = false;
  float last_rssi = 0, last_snr = 0;
  bool began = false;
  uint64_t granted_us = 0;   // lock-step: the broker time of the last grant
  bool got_grant = false;
};

static void lockstep_set_now(uint64_t abs_us);

static void put_f32(uint8_t* p, float v) { memcpy(p, &v, 4); }
static float get_f32(const uint8_t* p) { float v; memcpy(&v, p, 4); return v; }

static void send_frame(int fd, char type, const uint8_t* payload, size_t n) {
  std::vector<uint8_t> f(5 + n);
  uint32_t len = 1 + n;
  memcpy(f.data(), &len, 4);
  f[4] = (uint8_t)type;
  if (n) memcpy(f.data() + 5, payload, n);
  size_t off = 0;
  while (off < f.size()) {
    ssize_t w = send(fd, f.data() + off, f.size() - off, MSG_NOSIGNAL);
    if (w > 0) off += w;
    else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(100);
    else return;
  }
}

SimRadio::SimRadio() : _i(new Impl()), _freq(LORA_FREQ), _bw(LORA_BW), _sf(LORA_SF), _cr(LORA_CR), _tx_dbm(0),
                       _n_recv(0), _n_sent(0), _n_err(0) {}

void SimRadio::connectEther() {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un a{}; a.sun_family = AF_UNIX;
  strncpy(a.sun_path, g_sim.ether.c_str(), sizeof(a.sun_path) - 1);
  if (connect(fd, (sockaddr*)&a, sizeof a) != 0) { perror("ether connect"); exit(3); }
  fcntl(fd, F_SETFL, O_NONBLOCK);
  _i->fd = fd;
  if (g_sim.lockstep) send_frame(fd, 'L', nullptr, 0);
  send_frame(fd, 'H', (const uint8_t*)g_sim.name.data(), g_sim.name.size());
}

void SimRadio::begin() {
  if (_i->began || g_sim.ether.empty()) return;
  _i->began = true;
  if (_i->fd < 0) connectEther();
  sendParams();
}

void SimRadio::sendParams() {
  if (_i->fd < 0) return;
  uint8_t f[10];
  put_f32(f, _freq); put_f32(f + 4, _bw); f[8] = _sf; f[9] = _cr;
  send_frame(_i->fd, 'P', f, 10);
}

// Append whatever the socket holds to the input buffer. `block`: wait (bounded) for at least one byte first.
// Returns false once the broker has gone away.
bool SimRadio::readEther(bool block) {
  if (_i->fd < 0) return false;
  if (block) {
    pollfd p{_i->fd, POLLIN, 0};
    if (poll(&p, 1, 1000) <= 0) return true;
  }
  uint8_t tmp[2048];
  for (;;) {
    ssize_t n = recv(_i->fd, tmp, sizeof tmp, 0);
    if (n > 0) _i->inbuf.insert(_i->inbuf.end(), tmp, tmp + n);
    else if (n == 0) { close(_i->fd); _i->fd = -1; return false; }
    else break;
  }
  return true;
}

// Apply the complete frames in the input buffer, in order. With `stop_at_grant` it stops right after a 'G' (and
// returns true), leaving later frames for the next barrier.
bool SimRadio::parseFrames(bool stop_at_grant) {
  auto& b = _i->inbuf;
  size_t pos = 0;
  bool grant = false;
  while (b.size() - pos >= 5) {
    uint32_t len; memcpy(&len, &b[pos], 4);
    if (b.size() - pos < 4 + len) break;
    char type = (char)b[pos + 4];
    const uint8_t* p = &b[pos + 5];
    size_t plen = len - 1;
    if (type == 'B' && plen >= 4) {
      uint32_t dur; memcpy(&dur, p, 4);
      uint64_t until = sim_clock()->nowUs() + dur;
      if (until > _i->busy_until_us) _i->busy_until_us = until;
    } else if (type == 'R' && plen > 8) {
      Rx r; r.rssi = get_f32(p); r.snr = get_f32(p + 4);
      r.len = (int)std::min<size_t>(plen - 8, sizeof r.data);
      memcpy(r.data, p + 8, r.len);
      _i->rxq.push_back(r);
    } else if (type == 'G' && plen >= 8) {
      memcpy(&_i->granted_us, p, 8);
      _i->got_grant = true;
      grant = true;
    }
    pos += 4 + len;
    if (grant && stop_at_grant) break;
  }
  b.erase(b.begin(), b.begin() + pos);
  return grant;
}

void SimRadio::loop() {
  if (_i->fd < 0 || g_sim.lockstep) return;   // under lock-step the socket is read only at a barrier
  readEther(false);
  parseFrames(false);
}

uint64_t SimRadio::lockstepJoin() {
  if (_i->fd < 0) connectEther();
  _i->got_grant = false;
  for (;;) {
    if (parseFrames(true)) return _i->granted_us;
    if (!readEther(true)) { fprintf(stderr, "lockstep: the ether closed the connection at join\n"); exit(3); }
  }
}

uint64_t SimRadio::lockstepAdvance(uint64_t abs_us) {
  if (_i->fd < 0) { fprintf(stderr, "lockstep: no ether connection\n"); exit(3); }
  send_frame(_i->fd, 'A', (const uint8_t*)&abs_us, 8);
  for (;;) {
    // grants below the target only move the clock (another node's barrier came first); keep waiting
    while (parseFrames(true)) {
      lockstep_set_now(_i->granted_us);
      if (_i->granted_us >= abs_us) return _i->granted_us;
    }
    if (!readEther(true)) { fprintf(stderr, "lockstep: the ether closed the connection\n"); exit(3); }
  }
}

int SimRadio::recvRaw(uint8_t* bytes, int sz) {
  loop();
  if (_i->rxq.empty() || _i->tx_active) return 0;
  Rx r = _i->rxq.front(); _i->rxq.pop_front();
  _i->last_rssi = r.rssi; _i->last_snr = r.snr;
  int n = std::min(sz, r.len);
  memcpy(bytes, r.data, n);
  _n_recv++;
  return n;
}

uint32_t SimRadio::getEstAirtimeFor(int len_bytes) {
  return (uint32_t)(sim_toa_us(_sf, _bw, _cr, preambleSymbols(_sf), len_bytes) / 1000.0 + 0.5);
}

float SimRadio::packetScore(float snr, int packet_len) {
  // Same shape as upstream's SNR-threshold score at an assumed SF10 (-15 dB floor).
  const float thr = -15.0f;
  if (snr < thr) return 0.0f;
  float s = (snr - thr) / 10.0f * (1.0f - packet_len / 256.0f);
  return s < 0 ? 0 : (s > 1 ? 1 : s);
}

bool SimRadio::startSendRaw(const uint8_t* bytes, int len) {
  if (len <= 0 || len > 255) return false;
  double toa = sim_toa_us(_sf, _bw, _cr, preambleSymbols(_sf), len);
  uint8_t f[14 + 255];
  put_f32(f, _freq); put_f32(f + 4, _bw); f[8] = _sf; f[9] = _cr;
  uint32_t toa_us = (uint32_t)(toa + 0.5);
  memcpy(f + 10, &toa_us, 4);
  memcpy(f + 14, bytes, len);
  if (_i->fd >= 0) send_frame(_i->fd, 'T', f, 14 + len);
  _i->tx_active = true;
  _i->tx_end_us = sim_clock()->nowUs() + toa_us;
  _n_sent++;
  return true;
}

bool SimRadio::isSendComplete() { return _i->tx_active && sim_clock()->nowUs() >= _i->tx_end_us; }
void SimRadio::onSendFinished() { _i->tx_active = false; }
bool SimRadio::isInRecvMode() const { return !_i->tx_active; }
bool SimRadio::isReceiving() { loop(); return sim_clock()->nowUs() < _i->busy_until_us; }
float SimRadio::getLastRSSI() const { return _i->last_rssi; }
float SimRadio::getLastSNR() const { return _i->last_snr; }
uint32_t SimRadio::getRngSeed() { return g_sim.seed; }

void sim_radio_poll() { radio_driver.loop(); }

// ---------------------------------------------------------------- lock-step clock
// Virtual time owned by the broker. A node's clock reads (granted broker time - broker time at the join), so it
// starts at 0 at every boot as millis() does on a board, and a transmission the node starts at node time t is
// stamped join + t by the broker, exactly.
namespace {
uint64_t g_ls_join = 0, g_ls_now = 0;

class LockstepClock : public SimClock {
public:
  uint64_t nowUs() override { return g_ls_now - g_ls_join; }
  void sleepUs(uint64_t us) override {
    if (us == 0) return;
    radio_driver.lockstepAdvance(g_ls_now + us);
  }
};
LockstepClock g_lockstep_clock;
}  // namespace

static void lockstep_set_now(uint64_t abs_us) { if (abs_us > g_ls_now) g_ls_now = abs_us; }

// Called from main() before setup(): join the broker's clock.
void sim_lockstep_begin() {
  g_ls_now = g_ls_join = radio_driver.lockstepJoin();
  sim_set_clock(&g_lockstep_clock);
}

// Called after every loop(): report the end of the current quantum (on the broker's grid, so nodes stay aligned after
// a delay()) and wait for it.
void sim_lockstep_step() {
  uint64_t q = g_sim.quantum_us ? g_sim.quantum_us : 1;
  radio_driver.lockstepAdvance((g_ls_now / q + 1) * q);
}
