#pragma once
// The network-profile lifecycle over the device interface (OPERATIONS.md section 5.1): net.profile.clear removes a Wi-Fi
// or broker profile, and mqtt.profile.anchor sets or clears a broker profile's trust anchor (a certificate fingerprint,
// or a CA certificate). Each runs the stock CLI lines as the caller (`set wifi.profile <n> clear`, `set mqtt.profile <n>
// clear|anchor|ca ...`), so the privilege table, the checks and the audit are the CLI's. A caller other than the cable
// may not clear or re-anchor the profile its own session runs on (`in-use`): that would cut its path, with nothing to
// revert to. The cable may.
#include <stddef.h>
#include <stdint.h>
#include "IfaceDispatch.h"

namespace Iface {

const uint8_t NET_NONE = 0xFF;

struct NetProfileHost {
  bool (*run)(void* user, const char* line, char* reply, size_t cap);   // a CLI line as the caller; false: unknown line
  void* user;
  bool cable;                // the caller is the unsigned or signed cable
  uint8_t wifi_count;        // profiles of each kind; 0 when the build has none
  uint8_t mqtt_count;
  uint8_t wifi_in_use;       // the profile joined or joining, NET_NONE when none
  uint8_t mqtt_in_use;       // the broker profile of the running or connecting session, NET_NONE when none
};

// The host for this build: the profile counts and the profiles in use come from the Wi-Fi and MQTT controllers, on the
// builds that have them.
NetProfileHost netProfileHost(bool (*run)(void*, const char*, char*, size_t), void* user, bool cable);
bool netProfileOpsBuilt();   // a build with Wi-Fi or MQTT: the two operations are offered

Status opNetProfileClear(OpContext& c, NetProfileHost& h);   // {"kind":"wifi"|"mqtt","profile":n}
Status opMqttAnchor(OpContext& c, NetProfileHost& h);        // {"profile":n, "fingerprint":"<64 hex>"|"ca":"<base64>"|"clear":true}

}  // namespace Iface
