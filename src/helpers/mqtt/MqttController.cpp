#include "MqttController.h"
#include <stdio.h>
#include <string.h>

MqttController& mqttController() {
  static MqttController c;
  return c;
}

namespace {
const uint32_t OPEN_TIMEOUT_MS = 6000;         // DNS aside: the TCP connect and the TLS handshake
const uint32_t STATUS_REPUBLISH_MS = 300000;   // the retained status is refreshed this often
const uint16_t KEEPALIVE_S = 20;
const uint16_t SOCKET_TIMEOUT_S = 6;           // CONNACK wait
const uint32_t CLI_SETTLE_MS = 1500;           // after subscribing, requests are ignored: see onMessage()
const uint16_t PACKET_BUFFER = 512;            // one request or response (docs/CLI.md); PubSubClient's default is 256

bool parseIPv4(const char* s, uint32_t& out) {
  uint32_t v = 0;
  int parts = 0;
  const char* p = s;
  while (*p) {
    if (*p < '0' || *p > '9') return false;
    unsigned n = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; if (++digits > 3) return false; }
    if (n > 255) return false;
    v = (v << 8) | n;
    parts++;
    if (*p == '.') { p++; if (!*p) return false; }
    else if (*p) return false;
  }
  if (parts != 4) return false;
  out = v;
  return true;
}
}  // namespace

void MqttController::begin(MqttProfiles* profiles, MqttNet* net, const uint8_t* posture, const char* node_id,
                           const MqttTiming& timing) {
  _profiles = profiles;
  _net = net;
  _posture = posture;
  snprintf(_client_id, sizeof(_client_id), "mecon-%s", node_id);
  snprintf(_topic, sizeof(_topic), "mecon/%s/status", node_id);
  Cli::Mqtt::requestTopic(_req_topic, sizeof(_req_topic), node_id);
  Cli::Mqtt::responseTopic(_resp_topic, sizeof(_resp_topic), node_id);
  _net->init(node_id);
  _core.configure(profiles, timing);
  _psc.setKeepAlive(KEEPALIVE_S);
  _psc.setSocketTimeout(SOCKET_TIMEOUT_S);
  _psc.setBufferSize(PACKET_BUFFER);
  _psc.setCallback(&MqttController::trampoline);
  _started = true;
  _enabled = true;
  _core.setPosture(*_posture);
  _core.setLink(millis(), _net->linkUp());
  _core.begin(millis());
}

void MqttController::setEnabled(bool enabled) {
  if (!_started || enabled == _enabled) return;
  _enabled = enabled;
  if (enabled) {
    _core.setPosture(*_posture);
    _core.begin(millis());
  } else {
    closeSession();
    _core.stop();
  }
}

void MqttController::profileChanged(uint8_t index, bool credentials) {
  if (_started) _core.profileChanged(millis(), index, credentials);
}

// PubSubClient keeps a pointer to the transport, so it is only touched while a transport is open.
void MqttController::dropTransport() {
  _net->close();
  _open = false;
}

void MqttController::closeSession() {
  if (_open && _psc.connected()) _psc.disconnect();   // a clean DISCONNECT, so the broker does not publish the last will
  dropTransport();
}

void MqttController::publishStatus() {
  char payload[96];
  uint8_t cur = _core.current();
  const char* tls = !_profiles->p[cur].useTls() ? "plain" : (_core.verified() ? "verified" : "unverified");
  snprintf(payload, sizeof(payload), "{\"online\":true,\"profile\":%u,\"tls\":\"%s\"}", (unsigned)cur, tls);
  _psc.publish(_topic, payload, true);
  _next_status = millis() + STATUS_REPUBLISH_MS;
}

void MqttController::attempt(const MqttAction& a) {
  const uint32_t now = millis();
  const MqttProfile& q = _profiles->p[a.profile];
  uint32_t addr = 0;
  uint16_t port = 0;
  if (q.kind == MqttProfile::Static) {
    // Resolved on every attempt, so a dynamic-DNS name keeps working when its address changes.
    if (!parseIPv4(q.host, addr) && !_net->resolve(q.host, addr)) {
      _core.onConnectFailed(millis(), MqttFail::Dns);
      return;
    }
    port = q.effectivePort();
  } else {
    MqttCandidate c;
    if (!_core.candidate(a.cand, c)) { _core.onConnectFailed(now, MqttFail::Lost); return; }
    addr = c.addr;
    port = c.port;
  }
  const bool tls = q.useTls();
  const char* anchor = (tls && q.hasAnchor()) ? q.anchor : nullptr;
  MqttOpen r = _net->open(tls, addr, port, anchor, OPEN_TIMEOUT_MS);
  if (r != MqttOpen::Ok) {
    dropTransport();
    _core.onConnectFailed(millis(), r == MqttOpen::Pin ? MqttFail::Pin : (r == MqttOpen::Tls ? MqttFail::Tls : MqttFail::Tcp));
    return;
  }
  _open = true;
  _psc.setClient(_net->client());
  const char* user = q.user[0] ? q.user : nullptr;
  const char* pwd = (user && q.pwd[0]) ? q.pwd : nullptr;
  if (!_psc.connect(_client_id, user, pwd, _topic, 1, true, "{\"online\":false}")) {
    int st = _psc.state();
    dropTransport();
    _core.onConnectFailed(millis(), (st == MQTT_CONNECT_BAD_CREDENTIALS || st == MQTT_CONNECT_UNAUTHORIZED) ? MqttFail::Auth
                                    : (st == MQTT_CONNECTION_TIMEOUT ? MqttFail::Timeout : MqttFail::Tcp));
    return;
  }
  _cur_addr = addr;
  _cur_port = port;
  _core.onConnected(millis(), tls && anchor != nullptr);
  if (_cli_fn) {
    _psc.subscribe(_req_topic);   // before the status, so a client that sees the status can send at once
    _cli_open_at = millis() + CLI_SETTLE_MS;
  }
  publishStatus();
}

void MqttController::trampoline(char* topic, uint8_t* payload, unsigned int len) {
  mqttController().onMessage(topic, payload, len);
}

// One request from the broker. The session decides the authentication (a TLS connection whose certificate matched
// the pinned anchor is "verified"; plain TCP and TLS without an anchor are not), the current profile decides whether
// the broker may manage the device in Hardened, and the engine's table decides the rest. A request is answered in
// every case: a refusal is a response, and the engine has already audited it.
//
// A broker hands a *retained* message to every new subscriber, and PubSubClient does not tell the callback which
// messages are retained. A retained request would then run again at every reconnect, a retained "reboot" at every
// boot. So requests that arrive in the first moments after the subscription are dropped: that is where retained
// messages arrive, and a client that sent a live request in that window simply gets no answer and sends it again.
void MqttController::onMessage(const char* topic, const uint8_t* payload, unsigned len) {
  if (!_cli_fn || strcmp(topic, _req_topic) != 0 || _core.state() != MqttState::Connected) return;
  if ((int32_t)(millis() - _cli_open_at) < 0) return;
  char out[440];
  char reply[160];
  Cli::Mqtt::Request req;
  size_t n;
  if (!Cli::Mqtt::parseRequest(payload, len, req)) {
    n = Cli::Mqtt::formatResponse(out, sizeof(out), "", Cli::Mqtt::S_BAD_REQUEST, "ERR: bad request");
  } else {
    const uint8_t cur = _core.current();
    const bool manages = cur != MqttCore::NONE && _profiles->p[cur].manage != 0;
    const Cli::Context ctx = Cli::Mqtt::sessionContext(_core.verified(), *_posture, manages);
    reply[0] = 0;
    const uint8_t status = _cli_fn(_cli_user, ctx, req.cmd, reply, sizeof(reply));
    n = Cli::Mqtt::formatResponse(out, sizeof(out), req.id, status, reply);
  }
  if (n) _psc.publish(_resp_topic, (const uint8_t*)out, (unsigned)n, false);
}

void MqttController::loop() {
  if (!_started || !_enabled) return;
  uint32_t now = millis();
  _core.setPosture(*_posture);
  _core.setLink(now, _net->linkUp());

  if (_core.state() == MqttState::Connected) {
    if (!_psc.loop()) {
      dropTransport();
      _core.onSessionLost(now, MqttFail::Dropped);
    } else if ((int32_t)(now - _next_status) >= 0) {
      publishStatus();
    }
  }

  if (_core.browsing()) {
    MqttResponder found[8];
    int n = _net->browsePoll(found, 8);
    if (n >= 0) {
      for (int k = 0; k < n; k++) _core.browseObserve(found[k].addr, found[k].port, found[k].secure, found[k].site);
      _core.browseDone(now);
    }
  }

  MqttAction a = _core.poll(now);
  switch (a.kind) {
    case MqttAction::Browse:
      _net->browseBegin();
      break;
    case MqttAction::Connect:
      attempt(a);
      break;
    case MqttAction::Disconnect:
      closeSession();
      break;
    default:
      break;
  }
}

void MqttController::formatStatus(char* out, size_t cap) {
  if (!_started || !_enabled) { snprintf(out, cap, "> off"); return; }
  uint32_t now = millis();
  MqttState st = _core.state();
  uint8_t cur = _core.current();
  if (st == MqttState::Connected && cur != MqttCore::NONE) {
    const MqttProfile& q = _profiles->p[cur];
    const char* tls = !q.useTls() ? "plain" : (_core.verified() ? "verified" : "unverified");
    snprintf(out, cap, "> connected profile=%u broker=%u.%u.%u.%u:%u tls=%s up=%lus", (unsigned)cur,
             (unsigned)(_cur_addr >> 24), (unsigned)((_cur_addr >> 16) & 255), (unsigned)((_cur_addr >> 8) & 255),
             (unsigned)(_cur_addr & 255), (unsigned)_cur_port, tls, (unsigned long)(_core.connectedMs(now) / 1000));
  } else if (st == MqttState::Connecting && cur != MqttCore::NONE) {
    snprintf(out, cap, "> connecting profile=%u", (unsigned)cur);
  } else {
    snprintf(out, cap, "> %s%s", MqttCore::stateName(st), _core.browsing() ? " browsing" : "");
  }
}
