#include "MqttController.h"
#include <stdio.h>
#include <stdlib.h>
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
const uint16_t PACKET_BUFFER = 512;
const uint16_t IFACE_PACKET_BUFFER = 1600;     // an interface result page (up to 1,536 bytes) and the topic            // one request or response (docs/CLI.md); PubSubClient's default is 256

void strlcpy_(char* dst, const char* src, size_t cap) {
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

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
  if (_iface_fn) {   // one status topic per build (DEVICE_INTERFACE.md section 7): the interface's, under device_id
    snprintf(_topic, sizeof(_topic), "mecon/%s/status", _device_id);
    snprintf(_iface_req, sizeof(_iface_req), "mecon/%s/req", _device_id);
    snprintf(_iface_resp, sizeof(_iface_resp), "mecon/%s/resp", _device_id);
  } else {
    snprintf(_topic, sizeof(_topic), "mecon/%s/status", node_id);
  }
  Cli::Mqtt::requestTopic(_req_topic, sizeof(_req_topic), node_id);
  Cli::Mqtt::responseTopic(_resp_topic, sizeof(_resp_topic), node_id);
  _net->init(node_id);
  _core.configure(profiles, timing);
  _psc.setKeepAlive(KEEPALIVE_S);
  _psc.setSocketTimeout(SOCKET_TIMEOUT_S);
  _psc.setBufferSize(_iface_fn ? IFACE_PACKET_BUFFER : PACKET_BUFFER);
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
  if (_job_active) {   // the job owns the transport and PubSubClient: ask it to stop and let takeResult() tidy up
    _job_cancel = true;
    return;
  }
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

// Start one connect attempt: copy what the job needs and hand it to the worker. Nothing here blocks.
void MqttController::attempt(const MqttAction& a) {
  const uint32_t now = millis();
  if (_job_active) return;   // never two attempts at once (the core does not ask for one)
  const MqttProfile& q = _profiles->p[a.profile];
  if (q.useTls() && !MqttProfiles::anchorUsable(q)) {
    // The profile is pinned but its anchor is damaged (a CA that no longer decodes, a malformed fingerprint). Connecting
    // anyway would be the unverified TLS the anchor exists to prevent: fail closed, without touching the network.
    Posture::audit().addOnce(_audit_now ? _audit_now() : 0, Posture::EV_REFUSED, Posture::CALLER_MQTT, Posture::W_MQTT_SESSION);
    _core.onConnectFailed(now, MqttFail::Pin);
    return;
  }
  ConnectJob& j = _job;
  memset(&j, 0, sizeof(j));
  if (q.kind == MqttProfile::Static) {
    // A name is resolved again on every attempt (by the job), so a dynamic-DNS name keeps working when its address
    // changes. An address literal needs no lookup.
    j.resolve = !parseIPv4(q.host, j.addr);
    if (j.resolve) { strlcpy_(j.host, q.host, sizeof(j.host)); strlcpy_(j.name, q.host, sizeof(j.name)); }
    j.port = q.effectivePort();
  } else {
    MqttCandidate c;
    if (!_core.candidate(a.cand, c)) { _core.onConnectFailed(now, MqttFail::Lost); return; }
    j.addr = c.addr;
    j.port = c.port;
  }
  j.tls = q.useTls();
  j.has_anchor = j.tls && q.hasAnchor();
  if (j.tls && q.hasCa()) {
    j.ca = (char*)malloc(strlen(q.ca) + 1);
    if (!j.ca) { _core.onConnectFailed(now, MqttFail::Tls); return; }
    strcpy(j.ca, q.ca);
  } else if (j.has_anchor) {
    strlcpy_(j.anchor, q.anchor, sizeof(j.anchor));
  }
  if (!j.tls) j.name[0] = 0;
  if (q.user[0]) {
    strlcpy_(j.user, q.user, sizeof(j.user));
    if (q.pwd[0]) strlcpy_(j.pwd, q.pwd, sizeof(j.pwd));
  }
  _job_cancel = false;
  _job_profile = a.profile;
  _job_active = true;
  if (!_net->asyncStart(&MqttController::connectEntry, this)) {
    // No worker (it could not be created, or the previous job is somehow still running): this attempt fails like a
    // connect that could not open, and the profile cools down before the next try.
    _job_active = false;
    free(j.ca);
    memset(&j, 0, sizeof(j));
    _core.onConnectFailed(now, MqttFail::Tcp);
  }
}

void MqttController::connectEntry(void* self) { static_cast<MqttController*>(self)->runConnect(); }

// The job, on the worker: resolve, open the transport, MQTT CONNECT. It checks the cancel flag between the steps and
// closes whatever it opened on every way out except success; on success the open transport and the connected
// PubSubClient are handed to the main loop. It uses only the request copy in `_job`, the network and PubSubClient.
void MqttController::runConnect() {
  ConnectJob& j = _job;
  if (j.resolve && !_net->resolve(j.host, j.addr)) { j.result = ConnectJob::Dns; return; }
  if (_job_cancel) { j.result = ConnectJob::Cancelled; return; }
  MqttOpenArgs oa;
  oa.tls = j.tls;
  oa.addr = j.addr;
  oa.port = j.port;
  oa.anchor_hex = (j.has_anchor && !j.ca) ? j.anchor : nullptr;
  oa.ca_b64 = j.ca;
  oa.name = j.name[0] ? j.name : nullptr;
  oa.timeout_ms = OPEN_TIMEOUT_MS;
  const MqttOpen r = _net->open(oa);
  if (r != MqttOpen::Ok) {
    _net->close();
    j.result = r == MqttOpen::Pin ? ConnectJob::Pin : (r == MqttOpen::Tls ? ConnectJob::Tls : ConnectJob::Tcp);
    return;
  }
  if (_job_cancel) { _net->close(); j.result = ConnectJob::Cancelled; return; }
  _psc.setClient(_net->client());
  const char* user = j.user[0] ? j.user : nullptr;
  const char* pwd = (user && j.pwd[0]) ? j.pwd : nullptr;
  if (!_psc.connect(_client_id, user, pwd, _topic, 1, true, "{\"online\":false}")) {
    const int st = _psc.state();
    _net->close();
    j.result = (st == MQTT_CONNECT_BAD_CREDENTIALS || st == MQTT_CONNECT_UNAUTHORIZED) ? ConnectJob::Auth
               : (st == MQTT_CONNECTION_TIMEOUT ? ConnectJob::Timeout : ConnectJob::Tcp);
    return;
  }
  j.result = ConnectJob::Ok;
}

// The attempt in flight is no longer wanted. The job is never interrupted: it notices the flag at its next step (the
// blocking calls are each bounded) and takeResult() discards what it produced.
void MqttController::cancelAttempt() { _job_cancel = true; }

// The finished job's result, on the main loop. A result nobody wants any more (cancelled, or the core has since left
// Connecting: profile edited, link lost, disabled) is dropped, closing the session if the job got that far.
void MqttController::takeResult() {
  _job_active = false;
  ConnectJob& j = _job;
  free(j.ca);   // the CA copy is the job's: nothing holds it once the job is over
  j.ca = nullptr;
  const bool wanted = !_job_cancel && _core.state() == MqttState::Connecting;
  if (j.result == ConnectJob::Ok) {
    _open = true;
    if (!wanted) {
      memset(j.pwd, 0, sizeof(j.pwd));
      closeSession();
      return;
    }
  } else if (!wanted) {
    memset(j.pwd, 0, sizeof(j.pwd));
    return;
  }
  memset(j.pwd, 0, sizeof(j.pwd));
  const uint32_t now = millis();
  switch (j.result) {
    case ConnectJob::Ok: break;
    case ConnectJob::Dns: _core.onConnectFailed(now, MqttFail::Dns); return;
    case ConnectJob::Tcp: _core.onConnectFailed(now, MqttFail::Tcp); return;
    case ConnectJob::Tls: _core.onConnectFailed(now, MqttFail::Tls); return;
    case ConnectJob::Pin: _core.onConnectFailed(now, MqttFail::Pin); return;
    case ConnectJob::Auth: _core.onConnectFailed(now, MqttFail::Auth); return;
    case ConnectJob::Timeout: _core.onConnectFailed(now, MqttFail::Timeout); return;
    case ConnectJob::Cancelled: _core.onConnectCancelled(); return;
  }
  _cur_addr = j.addr;
  _cur_port = j.port;
  _core.onConnected(now, j.tls && j.has_anchor);
  if (_cli_fn) _psc.subscribe(_req_topic);   // before the status, so a client that sees the status can send at once
  if (_iface_fn) _psc.subscribe(_iface_req);
  if (_cli_fn || _iface_fn) _cli_open_at = millis() + CLI_SETTLE_MS;
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
Cli::Context MqttController::sessionContext() const {
  const uint8_t cur = _core.current();
  const bool manages = cur != MqttCore::NONE && _profiles->p[cur].manage != 0;
  return Cli::Mqtt::sessionContext(_core.verified(), *_posture, manages);
}

// One device-interface request (DEVICE_INTERFACE.md section 7). Same session context and the same retained-message
// guard as the CLI topic; the result envelope goes to .../resp, never retained.
void MqttController::onIfaceMessage(const uint8_t* payload, unsigned len) {
  char* out = static_cast<char*>(malloc(IFACE_PACKET_BUFFER));
  if (!out) return;
  const size_t n = _iface_fn(_iface_user, sessionContext(), payload, len, out, IFACE_PACKET_BUFFER - 64);
  if (n) _psc.publish(_iface_resp, (const uint8_t*)out, (unsigned)n, false);
  free(out);
}

bool MqttController::publishIfaceEvent(const char* stream, const char* payload, size_t len) {
  if (!_iface_fn || !sessionUp()) return false;
  char topic[64];
  snprintf(topic, sizeof(topic), "mecon/%s/ev/%s", _device_id, stream);
  return _psc.publish(topic, (const uint8_t*)payload, (unsigned)len, false);
}

void MqttController::onMessage(const char* topic, const uint8_t* payload, unsigned len) {
  if (_core.state() != MqttState::Connected || (int32_t)(millis() - _cli_open_at) < 0) return;
  if (_iface_fn && strcmp(topic, _iface_req) == 0) { onIfaceMessage(payload, len); return; }
  if (!_cli_fn || strcmp(topic, _req_topic) != 0) return;
  char out[440];
  char reply[160];
  Cli::Mqtt::Request req;
  size_t n;
  if (!Cli::Mqtt::parseRequest(payload, len, req)) {
    n = Cli::Mqtt::formatResponse(out, sizeof(out), "", Cli::Mqtt::S_BAD_REQUEST, "ERR: bad request");
  } else {
    const Cli::Context ctx = sessionContext();
    reply[0] = 0;
    const uint8_t status = _cli_fn(_cli_user, ctx, req.cmd, reply, sizeof(reply));
    n = Cli::Mqtt::formatResponse(out, sizeof(out), req.id, status, reply);
  }
  if (n) _psc.publish(_resp_topic, (const uint8_t*)out, (unsigned)n, false);
}

void MqttController::loop() {
  if (!_started) return;
  if (!_enabled) {
    // A finished attempt is taken even while disabled: a cancelled one must still have its transport closed.
    if (_job_active && _net->asyncIdle()) takeResult();
    return;
  }
  uint32_t now = millis();
  _core.setPosture(*_posture);
  _core.setLink(now, _net->linkUp());

  // Before the result is taken, so an attempt that finishes in the same pass as the posture change is still dropped:
  // a plain session is never announced under Hardened.
  if (_job_active && !_job_cancel && !MqttProfiles::permitted(_profiles->p[_job_profile], *_posture)) {
    // The posture was raised and now forbids the profile being connected: stop that attempt, with no penalty.
    cancelAttempt();
    _core.onConnectCancelled();
  }
  if (_job_active && _net->asyncIdle()) takeResult();

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

  // While a cancelled job is still winding down, nothing new starts: its transport is not free yet.
  if (_job_active && _job_cancel) return;
  MqttAction a = _core.poll(now);
  switch (a.kind) {
    case MqttAction::Browse:
      _net->browseBegin();
      break;
    case MqttAction::Connect:
      attempt(a);
      break;
    case MqttAction::Disconnect:
      closeSession();   // with an attempt in flight this cancels it
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
