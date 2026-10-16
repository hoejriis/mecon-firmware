# MQTT broker profiles

The Companion `_wifi` builds can keep one MQTT session open to a broker. This is **Stage A**: transport and broker management only, standalone and independent of any MECON service. The controller sits on the Wi-Fi controller's link ([Wi-Fi profiles](WIFI.md)), holds up to **six broker profiles**, keeps **one session at a time**, fails over by priority and returns to a better profile without flapping. The only thing it publishes is a retained status message. The unified CLI over MQTT and the MECON topics ([MQTT/network contract](contract/MQTT_PROTOCOL.md)) are separate work.

What is verified, and how: the decision rules by unit tests against a fake network and by the simulated-board suite against real Mosquitto brokers, plain and TLS, with a simulated mDNS responder and DNS ([Simulated-board test harness](SIMULATION.md)). **Not verified on hardware:** the ESP32 mDNS query, the `WiFiClientSecure` fingerprint check and the TLS heap figure below. They are written from the platform sources and compiled for the Heltec V3 and V4, but no board has joined a network and no real broker has been reached. **RP2040 is not supported** (see Platforms).

## The MQTT client library

`PubSubClient` 2.8.0 (`knolleary/PubSubClient`, MIT), pinned to that exact version in the Heltec V3 and V4 Wi-Fi builds and in the two simulated Companion builds.

- **Heap.** One 256-byte packet buffer allocated once; no other allocation, no task, no queue. The connection object belongs to the platform, so the TLS context exists only while a session is open.
- **TLS and portability.** It speaks MQTT 3.1.1 through the Arduino `Client` interface, so the same code runs over `WiFiClient` and over `WiFiClientSecure` (mbedTLS) on ESP32, and compiles natively for the simulated board with three small shims (`Client.h`, `IPAddress.h`, two macros in `Arduino.h`) and a socket-and-OpenSSL `Client`. Nothing in the library knows about TLS, so the certificate check is ours and identical in the simulator and on the device.
- **Maintenance.** The last release is 2.8 (2020). The library is small, stable and very widely deployed; its protocol subset (3.1.1, QoS 0 and 1, no MQTT 5) is all this stage needs. If it ever has to be replaced, the controller touches it in three calls (`connect`, `loop`, `publish`).
- **Alternatives considered.** `esp-mqtt` (ESP-IDF) brings its own task, its own TLS stack and no native build, and does not exist on RP2040. `AsyncMqttClient` is ESP-only and depends on AsyncTCP. `arduino-mqtt` (lwmqtt) is comparable and more actively maintained, with a larger footprint and no advantage here.
- **Known limits of the library.** `connect()` blocks until CONNACK or the socket timeout (6 s here); there is no MQTT 5; a received message above the 256-byte buffer is dropped (nothing is subscribed in this stage).

## Profiles

Up to six, each with a priority (0 to 255, default 50; higher is tried first, equal priorities go to the lower profile number) and optional credentials. A profile has one of two kinds.

| Kind | Fields | Rule |
|---|---|---|
| **Static** | host name or IPv4 address, port (default 8883 with TLS, 1883 without), TLS on or off (default on), optional anchor | The name is resolved again on **every** connect attempt, so a dynamic-DNS name keeps working when its address changes. With TLS and an anchor the broker is authenticated; with TLS and no anchor the link is encrypted but **unauthenticated** and status says `tls=unverified`; without TLS status says `tls=plain`. |
| **Discovery** (an enrolled LAN broker) | site ID, anchor | The device browses mDNS for `_secure-mqtt._tcp` and `_mqtt._tcp`. A responder qualifies **only** if its TXT record carries `site=<site ID>` **and** the TLS certificate it presents matches the anchor. Always TLS. **Plain MQTT is never used for a discovered broker:** a responder that advertises `_mqtt._tcp` is recorded as `last=plain` and never contacted. |

The **anchor** is the SHA-256 fingerprint of the broker's certificate (the DER bytes of the certificate it presents; 64 hex digits, colons and capitals accepted). It pins that certificate: no CA chain is validated, so replacing the broker's certificate means updating the anchor. An mDNS answer is unauthenticated; the fingerprint check is what makes a broker enrolled, so a rogue broker on the same LAN that copies the site ID cannot capture the device. Pinning a CA certificate instead of a leaf is not implemented.

## Selection, failover and return

Timings are `MqttTiming` defaults in `MqttCore.h`.

- The device uses the highest-priority profile that is available. A static profile is available unless it is cooling down. A discovery profile is available while a responder with its site ID is visible and not cooling down.
- It **falls back** on a failed connect (DNS, TCP, TLS, certificate not matching the anchor, credentials refused, timeout), on a session that drops, on a responder that disappears from a browse, and when the posture forbids the profile in use.
- A failed profile **cools down**: 15 s after a failed connect, doubling per consecutive failure up to 5 min; 2 min doubling to 1 h after refused credentials; 5 min doubling to 1 h for a certificate that does not match. A session that lasted at least 30 s and then dropped is retried after 5 s; one that died younger counts as a failure. A candidate that fails the certificate check cools down **on its own**, so a rogue broker carrying the right site ID does not shut out the real one.
- **Return to a better profile** only when the current session is at least the **hold-down (60 s)** old and the better profile is not cooling down; a discovery responder must also have been seen in every browse for the whole hold-down (one missed browse restarts the clock). That bounds the cost of a flapping better broker at one interruption per hold-down. The first connect after boot or after a lost link is immediate. An edit you make with the CLI skips the hold-down, once.
- **Discovery runs only while the device is not on the best available profile.** On the best profile there is no browse at all. Otherwise one browse starts at link-up (before any lower-priority fallback is chosen) and then every 60 s. The browse is asynchronous (the ESP-IDF mDNS query, polled from the main loop) and never blocks it; the mDNS responder is started once and kept initialised between browses. A browse that never reports is abandoned after 10 s.
- A lost Wi-Fi link ends the session without penalty to the profile; the same profile is used when the link returns.

## Credentials and postures

- **Passwords are write-only** on every transport: no reply, status, log line or topic carries one, and the user name is shown only as `set` / `unset`. The only stored copy is the prefs file. The simulated-board suite scans every file of the node directory and its log for the password and the user name it set.
- **Normal** allows plain MQTT on a static profile (shown as `tls=plain`). **Hardened** refuses it twice: `set mqtt.profile <n> tls off` is refused, and a stored plain profile (set while Normal) is never used; if the posture rises while a plain session is up, the session is closed. Discovery profiles are unchanged in Hardened (TLS-pinned already).
- In Hardened the Wi-Fi link is kept only when MQTT has a profile Hardened permits (TLS static or discovery); the TCP companion server stays off either way ([Standalone security postures](STANDALONE_POSTURES.md)). Without such a profile Hardened starts no Wi-Fi, as before.

## Commands

All of them run on a trusted local transport; the TCP companion port refuses the CLI, and a remote CLI caller may run the `get` forms only.

| Command | Effect |
|---|---|
| `set mqtt.profile <n> type static\|discovery` | `n` is 0 to 5 |
| `set mqtt.profile <n> host <name-or-ipv4>` | static only; 1 to 63 characters: letters, digits, `.` `-` `_` |
| `set mqtt.profile <n> port <0-65535>` | 0 means the default for the TLS setting |
| `set mqtt.profile <n> tls on\|off` | static only; `off` is refused in Hardened |
| `set mqtt.profile <n> anchor <64 hex>\|clear` | the SHA-256 fingerprint of the broker certificate |
| `set mqtt.profile <n> site <id>` | discovery only; 1 to 23 characters |
| `set mqtt.profile <n> user <name>` / `pwd <secret>` | with no value, cleared; write-only |
| `set mqtt.profile <n> priority <0-255>` | higher is tried first |
| `set mqtt.profile <n> clear` | removes the profile and its credentials |
| `get mqtt.profile <n>` | `<n>: static <host>:<port> tls=on\|off anchor=set\|unset prio=<p> user=set\|unset pwd=set\|unset last=<reason>`, or the discovery form |
| `get mqtt.profiles [from]` | every configured profile, paged with `... next:<n>` |
| `get mqtt.status` | `connected profile=<n> broker=<ip>:<port> tls=verified\|unverified\|plain up=<s>`, or the state: `idle`, `connecting`, `backoff`, `no-link`, `off`, with ` browsing` while a browse runs |
| `set mqtt.enabled 0\|1`, `get mqtt.enabled` | stop or resume without losing the profiles |

`last=` is the last failure of that profile: `none`, `dns`, `tcp`, `tls` (the TCP or TLS connect failed), `pin` (the certificate does not match the anchor), `auth`, `timeout`, `dropped`, `lost` (the responder vanished), `posture` (Hardened forbids it) or `plain` (a discovery profile saw only a plain responder). A profile edit applies at once (a session on it is redone) with no reboot, as long as the controller is running; the controller starts at boot when Wi-Fi does.

## Status topic

On every connect, and again every 5 minutes, the device publishes a retained message (QoS 0) on `mecon/<node id>/status`, where the node id is the first four bytes of the public key as 8 hex digits, for example `{"online":true,"profile":1,"tls":"verified"}` with `tls` one of `verified`, `unverified`, `plain`. The broker publishes the last will `{"online":false}` on the same topic, retained, if the session ends without a DISCONNECT. The client id is `mecon-<node id>`; the session is clean. This topic is a Stage A placeholder: it is not part of the MECON contract and carries no secret.

## Storage

The profiles are saved in the Companion prefs file under `mqtt` (`en` and, per profile `i`, the keys `k<i>` kind, `p<i>` priority, `t<i>` TLS, `o<i>` port, `h<i>` host, `s<i>` site, `a<i>` anchor, `u<i>` user, `w<i>` password), so they use the same atomic save. A prefs file without them loads with no profile. RAM: 1,536 bytes for six profiles (256 bytes each) plus about 300 bytes for the controller state, in builds that have MQTT.

## Memory

Measured by compiling the Heltec V3 and V4 Wi-Fi builds at this change and at its base (the Wi-Fi controller):

| Build | RAM before | RAM with MQTT | Flash before | Flash with MQTT |
|---|---|---|---|---|
| Heltec V3 (`Heltec_v3_companion_radio_wifi`) | 174,056 B | 179,360 B (+5,304 B) | 1,117,761 B | 1,284,273 B (+166,512 B, 38.4 % of the app partition) |
| Heltec V4 (`heltec_v4_companion_radio_wifi`) | 174,020 B | 179,340 B (+5,320 B) | 1,116,089 B | 1,282,497 B (+166,408 B, 19.6 % of the app partition) |

RAM is the static figure PlatformIO reports (data and bss): the profiles (1.5 KB), the controller and PubSubClient, and mbedTLS' static state. Flash grows by about 163 KB, almost all of it the TLS client and the mDNS component, which the Wi-Fi builds did not link before.

**TLS heap while a session is open (estimate, not measured on hardware).** The pinned Arduino core (2.0.17, ESP-IDF 4.4 libraries) is built with `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` off and `CONFIG_MBEDTLS_DYNAMIC_BUFFER` off, so mbedTLS keeps a 16 KiB input and a 16 KiB output record buffer (about 33 KB) for as long as the session lives, plus roughly 10 to 15 KB of handshake state that is released after the handshake (an ECDSA P-256 server certificate; an RSA-2048 certificate costs more during the handshake). The Espressif mbedTLS documentation ("Reducing heap usage") gives the same order of magnitude, 40 to 50 KB at the peak. The controller allocates the TLS client only when a session opens and frees it when it ends, so a device on a plain profile pays nothing. The cost of reducing the record buffers (`CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN`) needs a different core build and is not done here. Whether the free heap of the Companion Wi-Fi build leaves room for this at runtime next to the Wi-Fi stack is exactly what the hardware run has to show.

## Platforms

- **ESP32 family** (Heltec V3 and V4 Wi-Fi builds): implemented in `MqttNetEsp32.cpp` over `WiFiClient` / `WiFiClientSecure` (the fingerprint is checked with `WiFiClientSecure::verify` after a handshake that skips chain validation) and the ESP-IDF `mdns_query_async_*` calls. Other ESP32 Wi-Fi builds opt in with `-D ENABLE_MQTT`, `+<helpers/mqtt/*.cpp>` and the library line.
- **RP2040: unsupported in this change.** The Companion builds for it keep working as before. The platform adapter would be a `MqttNet` over `WiFiClientSecure` (BearSSL, `setFingerprint`) and the core's LEAmDNS service query; neither is written, and the RP2040 mDNS responder cannot browse asynchronously the way this controller needs.
- Simulated boards: `sim_companion_wifi` and `sim_companion_wifi_usb`.

## Known limits

- A connect attempt (DNS, TCP, TLS handshake, CONNACK) blocks the main loop, each part bounded (about 6 s for the transport and 6 s for CONNACK). The mDNS browse never blocks.
- IPv4 only. A discovery responder is identified by address and port; at most six candidates are tracked, so a LAN that advertises many responders with the right site ID can delay discovery (each failed candidate cools down alone) but cannot make the device connect to one whose certificate does not match.
- A lost discovery responder is noticed by a browse, which runs only while a better discovery profile exists; on the best profile the session dropping is the signal.
- No SNI is sent. No client certificates. No CA store.
- Only the retained status is published; nothing is subscribed.
- Hardware: nothing here has run on a board.
