// The network-profile host of this build (NetProfileOps.h): the profile counts and the profiles in use, read from the
// Wi-Fi and MQTT controllers. Apart from the operations so that their logic builds and tests without the controllers.
#include "NetProfileOps.h"

#ifdef ENABLE_WIFI_INTERFACE
  #include "../wifi/WifiController.h"
#endif
#ifdef ENABLE_MQTT
  #include "../mqtt/MqttController.h"
#endif

namespace Iface {

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

}  // namespace Iface
