#pragma once
// The stored Wi-Fi and broker settings, the same keys on the Companion, the Repeater and the Room server. Each role's
// NodePrefs holds the values and writes these two sections into its prefs file; a file without them loads with the defaults.
#include <helpers/ConfigSerializer.h>
#ifdef ENABLE_WIFI_INTERFACE
  #include <helpers/wifi/WifiProfiles.h>
#endif
#ifdef ENABLE_MQTT
  #include <helpers/mqtt/MqttProfiles.h>
#endif

#ifdef ENABLE_WIFI_INTERFACE
class WifiPrefs : public ConfigSerializer {
  WifiProfiles& _w;
  uint8_t& _enabled;
protected:
  void structure() override {
    WifiProfiles& w = _w;
    def("ssid", w.ssid_[0], sizeof(w.ssid_[0]));   // profile 0 keeps the stock keys
    def("pwd", w.pwd_[0], sizeof(w.pwd_[0]));
    def("enabled", _enabled);
    // Further profiles and every priority; a stock file without them loads with the defaults.
    for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) {
      char key[4] = { 'p', (char)('0' + i), 0, 0 };
      def(key, w.prio_[i]);
      if (i == 0) continue;
      key[0] = 's';
      def(key, w.ssid_[i], sizeof(w.ssid_[i]));
      key[0] = 'w';
      def(key, w.pwd_[i], sizeof(w.pwd_[i]));
    }
  }
public:
  WifiPrefs(WifiProfiles& w, uint8_t& enabled) : _w(w), _enabled(enabled) { }
};
#endif

#ifdef ENABLE_MQTT
class MqttPrefs : public ConfigSerializer {
  MqttProfiles& _m;
  uint8_t& _enabled;
protected:
  void structure() override {
    MqttProfiles& m = _m;
    def("en", _enabled);
    for (uint8_t i = 0; i < MqttProfiles::COUNT; i++) {
      MqttProfile& q = m.p[i];
      char key[4] = { 'k', (char)('0' + i), 0, 0 };
      def(key, q.kind);
      key[0] = 'p'; def(key, q.prio);
      key[0] = 't'; def(key, q.tls);
      key[0] = 'o'; def(key, q.port);
      key[0] = 'h'; def(key, q.host, sizeof(q.host));
      key[0] = 's'; def(key, q.site, sizeof(q.site));
      key[0] = 'a'; def(key, q.anchor, sizeof(q.anchor));
      key[0] = 'u'; def(key, q.user, sizeof(q.user));
      key[0] = 'w'; def(key, q.pwd, sizeof(q.pwd));
      key[0] = 'g'; def(key, q.manage);
      key[0] = 'x'; def(key, q.ws);
      key[0] = 'r'; def(key, q.path, sizeof(q.path));
      // A CA anchor, as base64 in pieces of CA_CHUNK characters (one prefs token holds 127). Only the pieces that
      // exist are written, so a profile without a CA adds nothing to the file. Keys: c<profile><piece, two digits>.
      char ck[6] = { 'c', (char)('0' + i), '0', '0', 0, 0 };
      char piece[MqttProfile::CA_CHUNK + 1];
      for (uint8_t n = 0; n < MqttProfile::CA_CHUNKS; n++) {
        ck[2] = (char)('0' + n / 10); ck[3] = (char)('0' + n % 10);
        if (_context->op() == WRITE) {
          if (!m.caChunk(i, n, piece)) break;
          def(ck, piece, sizeof(piece));
        } else {
          piece[0] = 0;
          def(ck, piece, sizeof(piece));
          if (piece[0]) m.loadCaChunk(i, n, piece);
        }
      }
    }
  }
public:
  MqttPrefs(MqttProfiles& m, uint8_t& enabled) : _m(m), _enabled(enabled) { }
};
#endif
