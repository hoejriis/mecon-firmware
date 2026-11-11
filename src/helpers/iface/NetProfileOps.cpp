#include "NetProfileOps.h"

#include <stdio.h>
#include <string.h>

#include "IfaceJson.h"
#ifdef ENABLE_WIFI_INTERFACE
  #include "../wifi/WifiController.h"
#endif
#ifdef ENABLE_MQTT
  #include "../mqtt/MqttController.h"
#endif

namespace Iface {

namespace {

const size_t LINE = 160;
const size_t PIECE = 96;            // base64 characters per `ca add` line, well inside the console's 160
const size_t CA_B64_MAX = 1920;     // MqttProfile::CA_B64_MAX

bool cliOk(const char* reply) { return strncmp(reply, "OK", 2) == 0 || strncmp(reply, "> ", 2) == 0; }

// Runs one line as the caller; invalid when the CLI rejects it. The operation's own class (write) is checked before the
// handler runs, and every line these operations send has that class: the one stricter profile line, `manage`, is never
// sent. The CLI still authorizes and audits each line as the caller.
Status runLine(OpContext& c, NetProfileHost& h, const char* line) {
  char reply[LINE];
  reply[0] = 0;
  if (!h.run(h.user, line, reply, sizeof(reply)) || !cliOk(reply)) { c.error = "rejected"; return ST_INVALID; }
  return ST_OK;
}

// The profile index, checked against the kind's count, and the in-use rule.
Status profileArg(OpContext& c, NetProfileHost& h, uint8_t count, uint8_t in_use, unsigned* idx) {
  const JsonDoc& d = *c.doc;
  int64_t n;
  if (count == 0) { c.error = "unsupported"; return ST_INVALID; }
  if (!jsonGetInt(d, jsonFind(d, c.req->args, "profile"), 0, count - 1, &n)) { c.error = "bad-profile"; return ST_INVALID; }
  if (!h.cable && in_use != NET_NONE && (unsigned)n == in_use) { c.error = "in-use"; return ST_REFUSED; }
  *idx = (unsigned)n;
  return ST_OK;
}

}  // namespace

NetProfileHost netProfileHost(bool (*run)(void*, const char*, char*, size_t), void* user, bool cable) {
  NetProfileHost h{run, user, cable, 0, 0, NET_NONE, NET_NONE};
#ifdef ENABLE_WIFI_INTERFACE
  WifiController::Snapshot ws;
  wifiController().snapshot(&ws);
  h.wifi_count = WifiProfiles::COUNT;
  h.wifi_in_use = ws.profile;   // WifiCore::NONE is NET_NONE
#endif
#ifdef ENABLE_MQTT
  MqttController::Snapshot ms;
  mqttController().snapshot(&ms);
  h.mqtt_count = MqttProfiles::COUNT;
  h.mqtt_in_use = ms.profile;   // MqttCore::NONE is NET_NONE
#endif
  return h;
}

bool netProfileOpsBuilt() {
#if defined(ENABLE_WIFI_INTERFACE) || defined(ENABLE_MQTT)
  return true;
#else
  return false;
#endif
}

Status opNetProfileClear(OpContext& c, NetProfileHost& h) {
  const JsonDoc& d = *c.doc;
  char kind[8];
  if (c.req->args < 0 || !jsonGetString(d, jsonFind(d, c.req->args, "kind"), kind, sizeof(kind), CS_NAME, 1)) return ST_INVALID;
  const bool wifi = strcmp(kind, "wifi") == 0;
  if (!wifi && strcmp(kind, "mqtt") != 0) { c.error = "bad-kind"; return ST_INVALID; }
  unsigned idx;
  Status st = profileArg(c, h, wifi ? h.wifi_count : h.mqtt_count, wifi ? h.wifi_in_use : h.mqtt_in_use, &idx);
  if (st != ST_OK) return st;
  char line[LINE];
  snprintf(line, sizeof(line), wifi ? "set wifi.profile %u clear" : "set mqtt.profile %u clear", idx);
  st = runLine(c, h, line);
  if (st != ST_OK) return st;
  JsonWriter& w = *c.out;
  w.key("kind"); w.str(kind);
  w.key("profile"); w.unum(idx);
  return ST_OK;
}

Status opMqttAnchor(OpContext& c, NetProfileHost& h) {
  const JsonDoc& d = *c.doc;
  if (c.req->args < 0) return ST_INVALID;
  unsigned idx;
  Status st = profileArg(c, h, h.mqtt_count, h.mqtt_in_use, &idx);
  if (st != ST_OK) return st;
  const int fp = jsonFind(d, c.req->args, "fingerprint"), ca = jsonFind(d, c.req->args, "ca"), cl = jsonFind(d, c.req->args, "clear");
  if ((fp >= 0) + (ca >= 0) + (cl >= 0) != 1) { c.error = "one-of"; return ST_INVALID; }
  char line[LINE];
  const char* anchor;
  if (cl >= 0) {
    bool yes;
    if (!jsonGetBool(d, cl, &yes) || !yes) return ST_INVALID;
    snprintf(line, sizeof(line), "set mqtt.profile %u anchor clear", idx);
    if ((st = runLine(c, h, line)) != ST_OK) return st;
    anchor = "unset";   // the CLI's word: `anchor=unset`
  } else if (fp >= 0) {
    char hex[65];
    if (!jsonGetString(d, fp, hex, sizeof(hex), CS_NAME, 64)) { c.error = "bad-fingerprint"; return ST_INVALID; }
    snprintf(line, sizeof(line), "set mqtt.profile %u anchor %s", idx, hex);
    if ((st = runLine(c, h, line)) != ST_OK) return st;
    anchor = "fingerprint";
  } else {
    // The base64 is sent to the CLI in pieces straight from the request: a CA is up to 1,920 characters, more than a
    // copy on the loop's stack should take. Base64 has no escapes, so the token's own bytes are the value.
    if (d.tok[ca].type != JSMN_STRING) return ST_INVALID;
    const char* b64 = d.str(ca);
    const size_t n = (size_t)d.tlen(ca);
    if (n == 0 || n > CA_B64_MAX || memchr(b64, '\\', n)) { c.error = "bad-ca"; return ST_INVALID; }
    snprintf(line, sizeof(line), "set mqtt.profile %u ca begin", idx);
    if ((st = runLine(c, h, line)) != ST_OK) return st;
    for (size_t at = 0; at < n; at += PIECE) {
      const size_t k = n - at < PIECE ? n - at : PIECE;
      snprintf(line, sizeof(line), "set mqtt.profile %u ca add %.*s", idx, (int)k, b64 + at);
      if ((st = runLine(c, h, line)) != ST_OK) return st;
    }
    snprintf(line, sizeof(line), "set mqtt.profile %u ca end", idx);   // checks the certificate whole; refuses a damaged one
    if ((st = runLine(c, h, line)) != ST_OK) return st;
    anchor = "ca";
  }
  JsonWriter& w = *c.out;
  w.key("profile"); w.unum(idx);
  w.key("anchor"); w.str(anchor);
  return ST_OK;
}

}  // namespace Iface
