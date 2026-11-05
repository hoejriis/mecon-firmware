#pragma once
// Wi-Fi and the broker on a Repeater or Room server build (docs/WIFI.md, docs/MQTT.md): the start-up and the command line
// over MQTT, the same for both roles. The links are the same controllers as the Companion's; what differs is that a
// server has no TCP port for an app, so Wi-Fi serves the broker only. Native repeating does not depend on any of this.
#if defined(ENABLE_WIFI_INTERFACE) && defined(ENABLE_MECON_IFACE)
#include <stdio.h>
#include <string.h>
#include <helpers/AuditLog.h>
#include <helpers/Posture.h>
#include <helpers/cli/CliCaller.h>
#include <helpers/cli/CliEngine.h>
#include <helpers/iface/ConsoleIface.h>
#include <helpers/net/NetTelemetry.h>
#include <helpers/wifi/WifiController.h>
#ifdef ENABLE_MQTT
  #include <helpers/mqtt/MqttController.h>
#endif

namespace ServerNet {

// One command line over the broker: run as the console runs it (timestamp 0), decided and audited as the session.
template <class Mesh>
uint8_t runCli(void* user, const Cli::Context& ctx, const char* cmd, char* reply, size_t cap) {
  static char line[160], out[160];   // the loop task's stack has no room for these next to a handler; loop only
  snprintf(line, sizeof(line), "%s", cmd);
  out[0] = 0;
  const Cli::NetLineScope scope(ctx);
  const Cli::Resolved r = Cli::resolve(line, Cli::ROLE_REPEATER);
  const bool allowed = Cli::decide(Cli::withServerCaller(Cli::Context(Cli::T_USB, Cli::A_CABLE, ctx.posture)), r.cls, r.flags).allow;
  static_cast<Mesh*>(user)->handleCommand(0, line, out);
  snprintf(reply, cap, "%s", out);
  if (!allowed) return Cli::Mqtt::S_REFUSED;
  return strncmp(out, "??", 2) == 0 || strcmp(out, "Unknown command") == 0 ? Cli::Mqtt::S_UNKNOWN : Cli::Mqtt::S_OK;
}

inline size_t runIface(void* user, const Cli::Context& ctx, const uint8_t* msg, size_t len, char* out, size_t cap) {
  return static_cast<Iface::ConsoleIface*>(user)->onMqtt(ctx, msg, len, out, cap);
}

// A write-only setting, for a confirm-or-revert snapshot only: never into a result, an event or a log.
template <class Prefs>
bool readSecret(Prefs& p, const Iface::KeyDef& k, unsigned idx, char* out, size_t cap) {
  const char* v = nullptr;
  if (strcmp(k.key, "wifi.profile.{n}.pwd") == 0 && idx < WifiProfiles::COUNT) v = p.wifi_profiles.pwd((uint8_t)idx);
#ifdef ENABLE_MQTT
  if (idx < MqttProfiles::COUNT) {
    if (strcmp(k.key, "mqtt.profile.{n}.user") == 0) v = p.mqtt_profiles.p[idx].user;
    if (strcmp(k.key, "mqtt.profile.{n}.pwd") == 0) v = p.mqtt_profiles.p[idx].pwd;
  }
#endif
  if (!v || strlen(v) >= cap) return false;
  strcpy(out, v);
  return true;
}

// Whether the links run, as on the Companion: with a profile, and in Hardened only for a broker that posture permits.
// Returns whether Wi-Fi started.
template <class Mesh, class Prefs, class Board>
bool begin(Mesh& mesh, Prefs& p, Board& board, Iface::ConsoleIface& iface, uint32_t (*clock)()) {
  bool on = p.wifi_enabled && p.wifi_profiles.configuredCount() > 0;
  bool mqtt_may_run = false;
#ifdef ENABLE_MQTT
  mqtt_may_run = p.mqtt_enabled && p.mqtt_profiles.anyPermitted(p.posture);
#endif
  if (on && Posture::isHardened(p.posture) && !mqtt_may_run) {
    Posture::audit().addOnce(clock(), Posture::EV_REFUSED, Posture::CALLER_NET, Posture::W_NET_START);
    on = false;
  }
  iface.setConnectivity(Iface::writeNetConnectivity, Iface::netConnSignature);   // reported even while Wi-Fi is off
  if (!on) return false;
#if defined(ESP32)
  board.setInhibitSleep(true);   // no light sleep under a Wi-Fi link
#endif
  wifiController().begin(&p.wifi_profiles);
#ifdef ENABLE_MQTT
  if (p.mqtt_enabled) {
    char node_id[9];
    for (int k = 0; k < 4; k++) sprintf(&node_id[k * 2], "%02x", mesh.self_id.pub_key[k]);
    mqttController().setAuditClock(clock);
    mqttController().setCliHandler(&runCli<Mesh>, &mesh);
    mqttController().setIfaceHandler(&runIface, &iface, iface.deviceIdHex());
    iface.setRolesListener([](const uint8_t* t, uint8_t n, void*) { mqttController().setBrokerTiers(t, n); }, nullptr);
    iface.setMqttEvents(
        [](const char* stream, const char* payload, size_t len) { return mqttController().publishIfaceEvent(stream, payload, len); },
        []() { return mqttController().sessionUp(); });
    mqttController().begin(&p.mqtt_profiles, &mqttPlatformNet(), &p.posture, node_id);
  }
#endif
  return true;
}

inline void loop() {
  wifiController().loop();
#ifdef ENABLE_MQTT
  mqttController().loop();
#endif
}

}  // namespace ServerNet
#endif
