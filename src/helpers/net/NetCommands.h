#pragma once
// The Wi-Fi and broker settings commands, the same lines on the Companion, the Repeater and the Room server (docs/CLI.md).
// Who may run them is the engine's table: the role's own gate has already decided, before this is called.
#ifdef ENABLE_WIFI_INTERFACE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/wifi/WifiController.h>
#ifdef ENABLE_MQTT
  #include <helpers/mqtt/MqttController.h>
#endif

// True when the line was one of these; reply is the answer. save stores the role's prefs.
template <class Prefs>
inline bool netCommand(Prefs& prefs, void (*save)(void*), void* user, const char* command, char* reply) {
  // Wi-Fi profiles: `set|get wifi.profile(s) ...`; the stock wifi.ssid / wifi.pwd / wifi.clear are profile 0.
  // A password is write-only: no reply below ever carries one.
  {
    WifiProfileChange change;
    if (wifiProfileCommand(prefs.wifi_profiles, wifiController().core(), command, reply, 150, change)) {
      if (change.changed) {
        save(user);
        wifiController().profileChanged(change.index, change.credentials);
      }
      return true;
    }
  }
  if (memcmp(command, "set wifi.ssid ", 14) == 0) {
    StrHelper::strncpy(prefs.wifi_profiles.ssid_[0], &command[14], sizeof(prefs.wifi_profiles.ssid_[0]));
    save(user);
    wifiController().profileChanged(0, true);
    sprintf(reply, "> wifi.ssid is now %s (set wifi.pwd too)", prefs.wifi_profiles.ssid(0));
    return true;
  }
  if (memcmp(command, "set wifi.pwd ", 13) == 0) {
    StrHelper::strncpy(prefs.wifi_profiles.pwd_[0], &command[13], sizeof(prefs.wifi_profiles.pwd_[0]));
    save(user);
    wifiController().profileChanged(0, true);
    strcpy(reply, "> wifi.pwd updated");
    return true;
  }
  if (strcmp(command, "set wifi.clear") == 0) {
    prefs.wifi_profiles.ssid_[0][0] = 0;
    prefs.wifi_profiles.pwd_[0][0] = 0;
    save(user);
    wifiController().profileChanged(0, true);
    strcpy(reply, "> wifi config cleared");
    return true;
  }
  if (strcmp(command, "get wifi.ssid") == 0) {
    sprintf(reply, "> %s", prefs.wifi_profiles.ssid(0)[0] ? prefs.wifi_profiles.ssid(0) : "(not set)");
    return true;
  }
  if (memcmp(command, "set wifi.enabled ", 17) == 0) {
    prefs.wifi_enabled = atoi(&command[17]) ? 1 : 0;
    save(user);
    sprintf(reply, "> wifi.enabled is now %d (reboot to apply)", prefs.wifi_enabled);
    return true;
  }
  if (strcmp(command, "get wifi.enabled") == 0) {
    sprintf(reply, "> %d", prefs.wifi_enabled);
    return true;
  }
  if (strcmp(command, "get wifi.status") == 0) {
    wifiController().formatStatus(reply, 150);
    return true;
  }
  if (strcmp(command, "get wifi.ip") == 0) {
    char ip[20];
    wifiController().formatIp(ip, sizeof(ip));
    if (ip[0]) {
      sprintf(reply, "> %s", ip);
    } else {
      strcpy(reply, "> (not connected)");
    }
    return true;
  }
#ifdef ENABLE_MQTT
  // MQTT broker profiles: `set|get mqtt.profile(s) ...`. Credentials are write-only: no reply carries one.
  {
    MqttProfileChange change;
    if (mqttProfileCommand(prefs.mqtt_profiles, mqttController().started() ? mqttController().core() : nullptr, prefs.posture, command, reply, 150, change)) {
      if (change.changed) {
        save(user);
        mqttController().profileChanged(change.index, change.credentials);
      }
      return true;
    }
  }
  if (memcmp(command, "set mqtt.enabled ", 17) == 0) {
    prefs.mqtt_enabled = atoi(&command[17]) ? 1 : 0;
    save(user);
    mqttController().setEnabled(prefs.mqtt_enabled != 0);
    sprintf(reply, "> mqtt.enabled is now %d%s", prefs.mqtt_enabled, mqttController().started() ? "" : " (reboot to apply)");
    return true;
  }
  if (strcmp(command, "get mqtt.enabled") == 0) {
    sprintf(reply, "> %d", prefs.mqtt_enabled);
    return true;
  }
  if (strcmp(command, "get mqtt.status") == 0) {
    mqttController().formatStatus(reply, 150);
    return true;
  }
#endif
  return false;
}
#endif  // ENABLE_WIFI_INTERFACE
