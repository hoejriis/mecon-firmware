// Simulated BLE serial interface (selected by SIM_BLE_STUB in examples/companion_radio/main.cpp) for the simulated board.
//
// There is no radio and no GATT stack: the transport is a second loopback TCP listener, next to the USB one, that carries
// the same Companion frames ('<' len16 payload from the app, '>' len16 payload to it). What it does model, because the
// security rules hang on it:
//   * Pairing. A central connects and must send the line "PAIR <pin>\n" first. The right PIN answers "PAIRED\n" and
//     the link is bonded for the life of that connection; a wrong PIN answers "REJECT\n" and the link is closed; any
//     frame before pairing closes it. No frame is read or written before the pairing succeeded.
//   * Trust. The interface reports its transport exactly like the real one (src/helpers/esp32/SerialBLEInterface.h):
//     trusted only with a PIN that is not 0 or the published default 123456. It is never a cable transport.
//   * Lifecycle. The listener exists only after begin() (the stack was brought up: not in Wi-Fi mode, not in Hardened
//     with a default PIN) and takes a central only while enabled (the Bluetooth page toggles it); disable() drops the link.
//     One central at a time, as a phone holds the link.
//   * "SIMBLE ..." lines in the node log say what happened. The PIN itself is never logged.
// Not modelled: radio, advertising, pairing UX (passkey entry), encryption of the air link, MTU, the 60 ms write spacing,
// bond storage across reboots. BLE hardware itself stays unverified.
//
// Port file: <--port-file>.ble, written when begin() runs (the harness reads it; see Node.connect_ble).
// `--ble-env FILE` still works: a file saying "connected" makes isConnected() true (the status screen's phone line)
// without a link, for tests of the screens.
#pragma once
#include <helpers/BaseSerialInterface.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "sim_config.h"
#include "sim_clock.h"

class SerialBLEInterface : public BaseSerialInterface {
  bool _enabled = false;
  uint32_t _pin = 0;
  int _listen = -1, _client = -1;
  bool _paired = false;
  char _line[40];            // the pairing line being received
  size_t _line_len = 0;
  // frame receiver (the same state machine as ArduinoSerialInterface)
  enum { IDLE, HDR, LEN1, BODY } _state = IDLE;
  uint16_t _frame_len = 0;
  size_t _rx_len = 0;
  uint8_t _rx_buf[MAX_FRAME_SIZE];

  static void logLine(const char* what) { fprintf(stderr, "SIMBLE %s\n", what); fflush(stderr); }

  void openListener() {
    _listen = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (bind(_listen, (sockaddr*)&a, sizeof a) != 0 || listen(_listen, 2) != 0) { perror("ble bind"); _exit(2); }
    socklen_t l = sizeof a; getsockname(_listen, (sockaddr*)&a, &l);
    fcntl(_listen, F_SETFL, O_NONBLOCK);
    if (!g_sim.port_file.empty()) {
      std::string path = g_sim.port_file + ".ble", tmp = path + ".tmp";
      if (FILE* f = fopen(tmp.c_str(), "w")) { fprintf(f, "%d\n", ntohs(a.sin_port)); fclose(f); rename(tmp.c_str(), path.c_str()); }
    }
  }

  void dropClient(const char* why) {
    if (_client >= 0) { close(_client); logLine(why); }
    _client = -1;
    _paired = false;
    _line_len = 0;
    _state = IDLE;
  }

  void sendText(const char* s) { send(_client, s, strlen(s), MSG_NOSIGNAL); }

  // Accept a central; read the pairing line; detect a dropped link. Called every main-loop pass.
  void pump() {
    if (_listen < 0) return;
    int c = accept(_listen, nullptr, nullptr);
    if (c >= 0) {
      if (_enabled && _client < 0) {
        int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(c, F_SETFL, O_NONBLOCK);
        _client = c; _paired = false; _line_len = 0; _state = IDLE;
        logLine("central connected");
      } else {
        close(c);   // not advertising (disabled) or already holding a central
      }
    }
    if (_client < 0 || _paired) return;
    // not paired yet: only the pairing line is accepted
    char ch;
    for (;;) {
      ssize_t n = recv(_client, &ch, 1, 0);
      if (n == 0) { dropClient("central left before pairing"); return; }
      if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient("central left before pairing");
        return;
      }
      if (ch != '\n') {
        if (_line_len == 0 && ch != 'P') { sendText("REJECT\n"); dropClient("pairing rejected: data before pairing"); return; }
        if (_line_len >= sizeof(_line) - 1) { sendText("REJECT\n"); dropClient("pairing rejected: not a pairing line"); return; }
        _line[_line_len++] = ch;
        continue;
      }
      _line[_line_len] = 0;
      char* end = nullptr;
      const bool shaped = strncmp(_line, "PAIR ", 5) == 0;
      const unsigned long given = shaped ? strtoul(_line + 5, &end, 10) : 0;
      if (shaped && end && end != _line + 5 && *end == 0 && (uint32_t)given == _pin) {
        _paired = true;
        sendText("PAIRED\n");
        logLine("paired");
      } else {
        sendText("REJECT\n");
        dropClient("pairing rejected");
      }
      return;
    }
  }

public:
  bool isTrustedTransport() const override { return _pin != 0 && _pin != 123456; }   // as the real class
  void begin(const char*, char*, uint32_t pin) {
    _pin = pin;
    fprintf(stderr, "SIMBLE begin pin=%s\n", (pin == 0 || pin == 123456) ? "default" : "per-device");
    fflush(stderr);
    openListener();
  }
  void enable() override { _enabled = true; }
  void disable() override {
    _enabled = false;
    dropClient("disabled: link dropped");
  }
  bool isEnabled() const override { return _enabled; }
  void loop() override { pump(); }
  bool isConnected() const override {
    if (!_enabled) return false;
    if (_paired && _client >= 0) return true;
    if (g_sim.ble_env.empty()) return false;
    static unsigned long next = 0;   // the file is read at most every 100 ms
    static bool last = false;
    unsigned long now = millis();
    if (now >= next) {
      next = now + 100;
      FILE* f = fopen(g_sim.ble_env.c_str(), "r");
      char b[16] = {0};
      size_t n = f ? fread(b, 1, sizeof(b) - 1, f) : 0;
      if (f) fclose(f);
      last = n >= 9 && !strncmp(b, "connected", 9);
    }
    return last;
  }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (len > MAX_FRAME_SIZE || len == 0 || _client < 0 || !_paired) return 0;
    uint8_t hdr[3] = {'>', (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    uint8_t out[3 + MAX_FRAME_SIZE];
    memcpy(out, hdr, 3);
    memcpy(out + 3, src, len);
    size_t off = 0;
    while (off < 3 + len) {
      ssize_t w = send(_client, out + off, 3 + len - off, MSG_NOSIGNAL);
      if (w > 0) off += w;
      else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(100);
      else { dropClient("central left"); return 0; }
    }
    return len;
  }
  size_t checkRecvFrame(uint8_t dest[]) override {
    pump();
    if (_client < 0 || !_paired) return 0;
    uint8_t ch;
    for (;;) {
      ssize_t n = recv(_client, &ch, 1, 0);
      if (n == 0) { dropClient("central left"); return 0; }
      if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient("central left");
        return 0;
      }
      switch (_state) {
        case IDLE: if (ch == '<') _state = HDR; break;
        case HDR: _frame_len = ch; _state = LEN1; break;
        case LEN1:
          _frame_len |= (uint16_t)ch << 8;
          _rx_len = 0;
          _state = _frame_len > 0 ? BODY : IDLE;
          break;
        default:
          if (_rx_len < MAX_FRAME_SIZE) _rx_buf[_rx_len] = ch;   // the rest of an over-long frame is discarded
          _rx_len++;
          if (_rx_len >= _frame_len) {
            if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;
            memcpy(dest, _rx_buf, _frame_len);
            _state = IDLE;
            return _frame_len;
          }
      }
    }
  }
};
