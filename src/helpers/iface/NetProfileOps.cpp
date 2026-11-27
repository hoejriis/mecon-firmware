#include "NetProfileOps.h"

#include <stdio.h>
#include <string.h>

#include "IfaceJson.h"

namespace Iface {

namespace {

const size_t LINE = 160;
const size_t PIECE = 96;            // base64 characters per `ca add` line, well inside the console's 160
const size_t CA_B64_MAX = 1920;     // MqttProfile::CA_B64_MAX
const size_t CA_DER_MAX = CA_B64_MAX / 4 * 3;   // 1,440 DER bytes
const size_t CA_ADD_MAX = 1024;     // base64 characters in one ca_add; over a broker a signed piece of about 950 is the most that fits, 768 is advised

bool cliOk(const char* reply) { return strncmp(reply, "OK", 2) == 0 || strncmp(reply, "> ", 2) == 0; }

// Runs one line as the caller; invalid when the CLI rejects it. The operation's own class (write) is checked before the
// handler runs, and every line these operations send has that class: the one stricter profile line, `manage`, is never
// sent. The CLI still authorizes and audits each line as the caller.
Status runLine(OpContext& c, NetProfileHost& h, const char* line) {
  char reply[LINE];
  reply[0] = 0;
  if (!h.run(h.user, line, reply, sizeof(reply)) || !cliOk(reply)) {
    // The staged-anchor refusals are named, so a manager can resume (MQTT.md, "A CA in pieces"): no stage for this profile
    // (never begun, ended, timed out, a reboot, or another profile's), an offset that is not the staged length, and an end
    // whose certificate is not the size and SHA-256 that begin gave.
    if (strstr(reply, "ca begin first")) c.error = "not-staged";
    else if (strstr(reply, "is not the staged length")) c.error = "offset";
    else if (strstr(reply, "not the size and SHA-256")) c.error = "ca-mismatch";
    else c.error = "rejected";
    return ST_INVALID;
  }
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

bool b64Text(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const char ch = s[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' || ch == '=')) return false;
  }
  return true;
}

// The chunked form: ca_begin, ca_add and ca_end are the console's `ca begin`, `ca add` and `ca end` (one staging buffer,
// the whole-certificate check at the end), each in a request small enough for a broker. `ca_begin` also names the size
// and the SHA-256 that `ca end` checks.
Status caBegin(OpContext& c, NetProfileHost& h, unsigned idx, int tok) {
  const JsonDoc& d = *c.doc;
  int64_t size;
  char hex[65];
  if (!isObject(d, tok)) return ST_INVALID;
  if (!jsonGetInt(d, jsonFind(d, tok, "size"), 1, (int64_t)CA_DER_MAX, &size)) { c.error = "bad-size"; return ST_INVALID; }
  if (!jsonGetString(d, jsonFind(d, tok, "sha256"), hex, sizeof(hex), CS_NAME, 64)) { c.error = "bad-sha256"; return ST_INVALID; }
  char line[LINE];
  snprintf(line, sizeof(line), "set mqtt.profile %u ca begin %u %s", idx, (unsigned)size, hex);
  Status st = runLine(c, h, line);
  if (st != ST_OK) return st;
  JsonWriter& w = *c.out;
  w.key("profile"); w.unum(idx);
  w.key("staged"); w.unum(0);
  return ST_OK;
}

Status caAdd(OpContext& c, NetProfileHost& h, unsigned idx, int tok) {
  const JsonDoc& d = *c.doc;
  int64_t off;
  if (!isObject(d, tok)) return ST_INVALID;
  if (!jsonGetInt(d, jsonFind(d, tok, "offset"), 0, (int64_t)CA_B64_MAX, &off)) { c.error = "bad-offset"; return ST_INVALID; }
  const int dt = jsonFind(d, tok, "data");
  if (dt < 0 || d.tok[dt].type != JSMN_STRING) return ST_INVALID;
  const char* b64 = d.str(dt);
  const size_t n = (size_t)d.tlen(dt);
  // Checked whole before a character is sent: a piece is appended whole or not at all.
  if (n == 0 || n > CA_ADD_MAX || (size_t)off + n > CA_B64_MAX || !b64Text(b64, n)) { c.error = "bad-ca"; return ST_INVALID; }
  char line[LINE];
  for (size_t at = 0; at < n; at += PIECE) {
    const size_t k = n - at < PIECE ? n - at : PIECE;
    // The console refuses an offset that is not the staged length, so a gap or a repeat appends nothing.
    snprintf(line, sizeof(line), "set mqtt.profile %u ca add @%u %.*s", idx, (unsigned)(off + at), (int)k, b64 + at);
    Status st = runLine(c, h, line);
    if (st != ST_OK) return st;
  }
  JsonWriter& w = *c.out;
  w.key("profile"); w.unum(idx);
  w.key("staged"); w.unum((uint64_t)off + n);
  return ST_OK;
}

}  // namespace

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
  const int cb = jsonFind(d, c.req->args, "ca_begin"), cadd = jsonFind(d, c.req->args, "ca_add"), ce = jsonFind(d, c.req->args, "ca_end");
  if ((fp >= 0) + (ca >= 0) + (cl >= 0) + (cb >= 0) + (cadd >= 0) + (ce >= 0) != 1) { c.error = "one-of"; return ST_INVALID; }
  if (cb >= 0) return caBegin(c, h, idx, cb);
  if (cadd >= 0) return caAdd(c, h, idx, cadd);
  char line[LINE];
  const char* anchor;
  if (ce >= 0) {
    bool yes;
    if (!jsonGetBool(d, ce, &yes) || !yes) return ST_INVALID;
    snprintf(line, sizeof(line), "set mqtt.profile %u ca end", idx);   // checks the certificate whole, its size and its SHA-256
    st = runLine(c, h, line);
    if (st != ST_OK) return st;
    anchor = "ca";
  } else if (cl >= 0) {
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
