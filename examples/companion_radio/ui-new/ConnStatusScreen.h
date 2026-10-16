#pragma once
// Platform glue of the connectivity status screen (src/helpers/connmode/ConnStatus.h holds the wording): reads the
// live state of the Wi-Fi controller, the MQTT controller and the BLE interface into the plain struct the formatter
// takes. The struct has no field for a password, key or PIN digit, so nothing read here can reach the screen.
#ifdef ENABLE_CONNMODE
#include <helpers/connmode/ConnStatus.h>
#include <helpers/wifi/WifiController.h>
#ifdef ENABLE_MQTT
  #include <helpers/mqtt/MqttController.h>
#endif
#include <WiFi.h>

inline void fillConnStatus(ConnMode::ConnStatus& s, AbstractUITask* task, NodePrefs* prefs) {
  s.mode = the_mesh.getConnectivityMode();
  s.posture = prefs->posture;
  s.ble_pin_default = ConnMode::pinIsDefault(the_mesh.getBLEPin());
  s.ble_connected = task->isBleConnected();

  WifiController& w = wifiController();
  s.wifi_has_profile = prefs->wifi_profiles.configuredCount() > 0;
  s.wifi_running = w.started();
  s.wifi_connected = w.connected();
  if (w.started()) {
    s.wifi_state = WifiCore::stateName(w.core()->state());
    const uint8_t cur = w.core()->current();
    if (cur != WifiCore::NONE) {
      s.wifi_profile = (int8_t)cur;
      snprintf(s.ssid, sizeof(s.ssid), "%s", prefs->wifi_profiles.ssid(cur));
    }
    if (s.wifi_connected) {
      s.rssi = (int)WiFi.RSSI();
      w.formatIp(s.ip, sizeof(s.ip));
    }
  }

#ifdef ENABLE_MQTT
  MqttController& m = mqttController();
  s.mqtt_has_profile = prefs->mqtt_profiles.configuredCount() > 0;
  s.mqtt_running = m.started() && prefs->mqtt_enabled;
  if (s.mqtt_running) {
    s.mqtt_state = MqttCore::stateName(m.core()->state());
    const uint8_t cur = m.core()->current();
    s.mqtt_connected = m.core()->state() == MqttState::Connected && cur != MqttCore::NONE;
    if (cur != MqttCore::NONE) {
      s.mqtt_profile = (int8_t)cur;
      const MqttProfile& q = prefs->mqtt_profiles.p[cur];
      s.mqtt_tls = !q.useTls() ? ConnMode::TLS_PLAIN : (m.core()->verified() ? ConnMode::TLS_VERIFIED : ConnMode::TLS_UNVERIFIED);
    }
  }
#endif
}
#endif
