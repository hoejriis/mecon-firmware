#pragma once
// Settings over the device interface (docs/contract/DEVICE_INTERFACE.md section 5): config.schema, config.get and
// config.apply, built on the CLI's own get/set lines so there is one store, one validation and one audit trail. A key
// exists here only if the CLI has a line for it. The CLI and the authorization check are injected, so the logic runs
// in the native unit tests against a fake CLI.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"
#include "ConfigSnap.h"
#include "JobService.h"

namespace Iface {

enum KeyType : uint8_t { K_STRING, K_INT, K_BOOL, K_ENUM };

enum KeyFlags : uint8_t {
  KF_WRITABLE = 1,
  KF_WRITE_ONLY = 2,   // a secret: get reports "set" or "unset", never the value
  KF_REBOOT = 4,       // takes effect after a reboot
  KF_RESTARTS = 8,     // the device restarts itself shortly after the write (connectivity.mode)
  KF_ONOFF = 16,       // K_BOOL: the CLI takes "on"/"off" for this key (others take "1"/"0")
  KF_CONFIRM = 32      // can cut its caller's path: applied tentatively, reverted unless confirmed (OPERATIONS.md 3.4)
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

// A string value with a fixed shape: "freq,bw,sf,cr" for radio (MHz, kHz, spreading factor 5-12, coding rate 5-8);
// "latitude" and "longitude": a signed decimal within +-90 and +-180.
bool checkFormat(const char* format, const char* value);

// The Companion's keys, the default when a host names no table.
extern const KeyDef KEYS[];
extern const size_t NKEYS;
// The Repeater's and Room server's keys: their shared CLI (CommonCLI, CommonRadioPrefs).
extern const KeyDef SERVER_KEYS[];
extern const size_t N_SERVER_KEYS;

// Runs one CLI line as the current caller and writes its reply; false when the line is unknown.
typedef bool (*CliRunFn)(void* user, const char* line, char* reply, size_t cap);
// Whether the current caller may run this line (the CLI privilege table), without running it.
typedef bool (*CliAllowFn)(void* user, const char* line);

// Commit-or-revert (OPERATIONS.md 3.4), provided by the role. A host without it offers no confirmation, and a caller
// other than the unsigned cable cannot apply a KF_CONFIRM key at all.
struct ConfirmHost {
  JobService* jobs;
  SnapStore* snap;
  // Whether the caller of this request is the unsigned cable, which does not lose its path and may apply without
  // confirmation.
  bool (*cable)(void* user);
  // The CLI class of a line for this caller's role, so the job records every class it changed.
  uint8_t (*line_class)(void* user, const char* line);
  // A write-only value read natively for the snapshot only ("" when unset); false: it cannot be read, so the request
  // is refused. Null: the role has no write-only keys.
  bool (*read_secret)(void* user, const KeyDef& k, unsigned idx, char* out, size_t cap);
  // Records the caller of this request into the job (its CLI context), and runs a line as the caller a job recorded.
  void (*record_caller)(void* user, Job& j);
  bool (*run_as)(void* user, const Job& j, const char* line, char* reply, size_t cap);
  // The job's identity: the request id, or "#<issuer>:<job_id>" for a signed request.
  bool (*job_id)(void* user, const Request& req, char* out, size_t cap);
};

struct ConfigHost {
  CliRunFn run;
  CliAllowFn allow;
  void* user;
  const KeyDef* keys;   // this role's table; nullptr means the Companion's (KEYS)
  size_t nkeys;
  const ConfirmHost* confirm;
};

// The key definition for a concrete key ("wifi.profile.3.ssid") in a table, with its index; nullptr when there is
// none. The two-argument form looks in the Companion's table.
const KeyDef* findKey(const KeyDef* table, size_t n, const char* key, size_t len, unsigned* index);
const KeyDef* findKey(const char* key, size_t len, unsigned* index);

// Extracts one field from a CLI get reply into out, as text ("" for a field of a profile that is not set). False
// when the reply does not have the expected shape (an error reply, a refusal).
bool parseField(uint8_t field, const char* reply, char* out, size_t cap, bool* is_null);

// The operations; ctx.user must point to a ConfigHost.
Status opConfigSchema(OpContext& ctx);
Status opConfigGet(OpContext& ctx);
Status opConfigApply(OpContext& ctx);

// The revert of a config.apply job: replays its snapshot as the caller who applied it and removes the snapshot. True
// when every line ran; *reboot says whether a restored key needs a reboot to take effect.
bool configRevert(const ConfigHost& h, const Job& j, bool* reboot);
// Its confirmation: the snapshot goes, the change stays.
void configConfirmed(const ConfigHost& h);
// At boot, after the job table is loaded: a snapshot that belongs to no job awaiting confirmation is removed.
void configSnapBoot(const ConfigHost& h);

const unsigned SCHEMA_PAGE = 6;   // key definitions per config.schema page
const unsigned GET_MAX_KEYS = 16; // keys per config.get / config.apply request

}  // namespace Iface
