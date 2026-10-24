#pragma once
// Settings over the device interface (docs/contract/DEVICE_INTERFACE.md section 5): config.schema, config.get and
// config.apply, built on the CLI's own get/set lines so there is one store, one validation and one audit trail. A key
// exists here only if the CLI has a line for it. The CLI and the authorization check are injected, so the logic runs
// in the native unit tests against a fake CLI.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

enum KeyType : uint8_t { K_STRING, K_INT, K_BOOL, K_ENUM };

enum KeyFlags : uint8_t {
  KF_WRITABLE = 1,
  KF_WRITE_ONLY = 2,   // a secret: get reports "set" or "unset", never the value
  KF_REBOOT = 4,       // takes effect after a reboot
  KF_RESTARTS = 8      // the device restarts itself shortly after the write (connectivity.mode)
};

// How a value is read from the CLI's reply to get_line.
enum Field : uint8_t {
  F_WHOLE,        // everything after "> "
  F_WIFI_SSID,    // "> <n>: <ssid> prio=<p> pwd=<set|unset> last=<r>"
  F_WIFI_PRIO,
  F_WIFI_PWD,
  F_MQTT_TYPE,    // "> <n>: static <host>:<port> tls=on ... prio=<p> user=<set|unset> pwd=<set|unset> ..."
  F_MQTT_HOST,    // "> <n>: discovery site=<id> anchor=... prio=<p> user=... pwd=... ..."
  F_MQTT_PORT,
  F_MQTT_TLS,
  F_MQTT_PRIO,
  F_MQTT_USER,
  F_MQTT_PWD,
  F_MQTT_SITE
};

struct KeyDef {
  const char* key;        // "name", or a pattern with one index: "wifi.profile.{n}.ssid"
  uint8_t type;
  int32_t min, max;       // K_INT: the range; K_STRING: the length range
  const char* choices;    // K_ENUM: "ble|wifi"
  uint8_t flags;
  uint8_t idx_max;        // highest {n} (0 when the key has no index)
  const char* get_line;   // printf format, %u = the index
  uint8_t field;
  const char* set_line;   // printf format, %u = the index, %s = the value as the CLI takes it
  const char* format;     // K_STRING with a fixed shape: its name in the schema, checked by checkFormat(); else nullptr
};

// A string value with a fixed shape: "freq,bw,sf,cr" for radio (MHz, kHz, spreading factor 5-12, coding rate 5-8).
bool checkFormat(const char* format, const char* value);

extern const KeyDef KEYS[];
extern const size_t NKEYS;

// Runs one CLI line as the current caller and writes its reply; false when the line is unknown.
typedef bool (*CliRunFn)(void* user, const char* line, char* reply, size_t cap);
// Whether the current caller may run this line (the CLI privilege table), without running it.
typedef bool (*CliAllowFn)(void* user, const char* line);

struct ConfigHost {
  CliRunFn run;
  CliAllowFn allow;
  void* user;
};

// The key definition for a concrete key ("wifi.profile.3.ssid"), with its index; nullptr when there is none.
const KeyDef* findKey(const char* key, size_t len, unsigned* index);

// Extracts one field from a CLI get reply into out, as text ("" for a field of a profile that is not set). False
// when the reply does not have the expected shape (an error reply, a refusal).
bool parseField(uint8_t field, const char* reply, char* out, size_t cap, bool* is_null);

// The operations; ctx.user must point to a ConfigHost.
Status opConfigSchema(OpContext& ctx);
Status opConfigGet(OpContext& ctx);
Status opConfigApply(OpContext& ctx);

const unsigned SCHEMA_PAGE = 6;   // key definitions per config.schema page
const unsigned GET_MAX_KEYS = 16; // keys per config.get / config.apply request

}  // namespace Iface
