#pragma once
// The device interface on the Repeater and Room server console (docs/contract/DEVICE_INTERFACE.md section 7). A
// console line that starts with "mecon> " carries one request envelope; its result, and any event, goes back as one
// line "mecon> <envelope>". Every other line stays the stock CLI. The caller is the console: the cable, decided by
// the CLI privilege table like any console line.
//
// The stock console reads at most 160 characters and echoes each one. Once a line has shown the prefix, this class
// takes the rest of it without echo, into its own buffer of MAX_LINE bytes, allocated only while such a line is
// arriving. A longer line is refused with busy (its id unread) and never truncated.
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "IfaceConfig.h"
#include "IfaceDispatch.h"
#include "Streams.h"

namespace Iface {

class ConsoleIface {
public:
  static const size_t MAX_LINE = 1600;   // the request bound MQTT already sets (section 7)
  static constexpr const char* PREFIX = "mecon> ";
  static const size_t PREFIX_LEN = 7;

  // What the role provides. run() executes one CLI line as the local console and returns false for an unknown line.
  struct Host {
    bool (*run)(void* user, const char* line, char* reply, size_t cap);
    uint8_t (*posture)(void* user);       // Posture value, for the CLI gate and status
    uint16_t (*battery_mv)(void* user);   // 0 when the board cannot measure it
    const char* role;                     // "repeater" or "room"
    const char* hardware;
    const uint8_t* node_key;              // 32 bytes
    void* user;
    // For the radio, power and runtime streams.
    mesh::Dispatcher* dispatcher;
    mesh::Radio* radio;
    mesh::MainBoard* board;
    void (*radio_counts)(void* user, RadioCounts* out);
    SensorManager* sensors;
    void (*inventory)(void* user, JsonWriter& w);   // the role's counts (TELEMETRY.md, inventory)
  };
  static const uint32_t STREAMS_PRODUCED = (1u << SID_HEALTH) | (1u << SID_RADIO) | (1u << SID_POWER) |
                                           (1u << SID_RUNTIME) | (1u << SID_OBSERVATIONS) | (1u << SID_INVENTORY);

  void begin(const Host& host, const uint8_t device_id[16], uint32_t epoch);

  // The console has just read the prefix: the rest of the line is this class's.
  void startLine();
  bool collecting() const { return _collecting; }
  // One more character of the line being collected; at its end the request is answered on out.
  void feed(char c, Print& out);
  // Health events while a subscription runs.
  void loop(uint32_t now, Print& out);

  // For the operations.
  const Host& host() const { return _host; }
  const char* deviceIdHex() const { return _id_hex; }
  uint32_t epoch() const { return _epoch; }
  ConfigHost configHost();
  SubSet& subs() { return _subs; }
  uint32_t produced() const { return streamsProduced(); }
  void scheduleStream(int stream, uint32_t now) { _clock.scheduleNow(stream, now); }

private:
  static bool authorize(void* self, const OpEntry& op, const Request& req);
  static bool cliRun(void* self, const char* line, char* reply, size_t cap);
  static bool cliAllow(void* self, const char* line);
  void answer(Print& out);
  void printLine(Print& out, const char* msg, size_t len);

  Host _host = {};
  char _id_hex[33] = {0};
  uint32_t _epoch = 0;
  char* _line = nullptr;       // allocated while a line is arriving
  size_t _len = 0;
  bool _collecting = false;
  bool _overflow = false;
  ResultCache* _cache = nullptr;
  SubSet _subs;
  StreamClock _clock;
  bool writeStream(int stream, JsonWriter& w);
  static void onObserve(void* self, float snr, float rssi, const uint8_t* raw, int len);
  ObsLimiter _obs;
  bool _env_ok = false, _env_checked = false;
  void refreshEnvironment(uint32_t now);
  uint32_t _env_at = 0;
  uint32_t streamsProduced() const { return STREAMS_PRODUCED | (_env_ok ? (1u << SID_ENVIRONMENT) : 0); }
  LoopGap _gap;
  Print* _out = nullptr;   // the console, for events raised outside loop()
};

}  // namespace Iface
