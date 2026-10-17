# MQTT broker profiles

The Companion `_wifi` builds can keep one MQTT session open to a broker. This is **Stage A**: transport and broker management only, standalone and independent of any MECON service. The controller sits on the Wi-Fi controller's link ([Wi-Fi profiles](WIFI.md)), holds up to **six broker profiles**, keeps **one session at a time**, fails over by priority and returns to a better profile without flapping. It publishes a retained status message and carries the command line over MQTT ([Command-line engine](CLI.md)). The MECON topics ([MQTT/network contract](contract/MQTT_PROTOCOL.md)) are separate work.

What is verified, and how: the decision rules by unit tests against a fake network and by the simulated-board suite against real Mosquitto brokers, plain and TLS, with a simulated mDNS responder and DNS ([Simulated-board test harness](SIMULATION.md)). **Not verified on hardware:** the ESP32 mDNS query, the `WiFiClientSecure` fingerprint check, the CA-anchor verification and SNI on the ESP32, and the TLS heap figure below. They are written from the platform sources and compiled for the Heltec V3 and V4, but no board has joined a network and no real broker has been reached. **RP2040 is not supported** (see Platforms).

## The MQTT client library

`PubSubClient` 2.8.0 (`knolleary/PubSubClient`, MIT), pinned to that exact version in the Heltec V3 and V4 Wi-Fi builds and in the two simulated Companion builds.

- **Heap.** One packet buffer allocated once (512 bytes since the command line over MQTT, 256 before) and, on ESP32, the connect worker's 10 KiB stack, allocated when the first connect attempt starts and kept (see "The connect runs on a worker"). No queue. The connection object belongs to the platform, so the TLS context exists only while a session is open.
- **TLS and portability.** It speaks MQTT 3.1.1 through the Arduino `Client` interface, so the same code runs over `WiFiClient` and over `WiFiClientSecure` (mbedTLS) on ESP32, and compiles natively for the simulated board with three small shims (`Client.h`, `IPAddress.h`, two macros in `Arduino.h`) and a socket-and-OpenSSL `Client`. Nothing in the library knows about TLS, so the certificate check is ours and identical in the simulator and on the device.
- **Maintenance.** The last release is 2.8 (2020). The library is small, stable and very widely deployed; its protocol subset (3.1.1, QoS 0 and 1, no MQTT 5) is all this stage needs. If it ever has to be replaced, the controller touches it in three calls (`connect`, `loop`, `publish`).
- **Alternatives considered.** `esp-mqtt` (ESP-IDF) brings its own task, its own TLS stack and no native build, and does not exist on RP2040. `AsyncMqttClient` is ESP-only and depends on AsyncTCP. `arduino-mqtt` (lwmqtt) is comparable and more actively maintained, with a larger footprint and no advantage here.
- **Known limits of the library.** `connect()` blocks until CONNACK or the socket timeout (6 s here), which is why the controller runs it on a worker; there is no MQTT 5; a received message above the packet buffer is dropped (512 bytes since the command line over MQTT; 256 before).

## The connect runs on a worker

DNS, the TCP connect, the TLS handshake and the MQTT CONNECT/CONNACK wait are each bounded (about 6 s for the transport and 6 s for the CONNACK) and, run inline, held the main loop for the whole attempt: no mesh packet was processed and the console did not answer. They now run as one job on a worker the platform provides, and the main loop only starts the job, polls for it and takes its result.

- **ESP32.** One FreeRTOS task (`mqttconn`), created when the first attempt starts and reused for every later one, pinned to the core that does not run the Arduino loop, priority 1 (the loop task's). It sleeps on a task notification between jobs. **Stack: 10,240 bytes**, an estimate, not a measurement: the same sequence ran before on the loop task's 8 KiB stack, the mbedTLS handshake is its deepest part and its large buffers are on the heap, so 8 KiB plus 25 %. The task's stack high-water mark is printed after every job in builds with `MESH_DEBUG` (`mqtt worker stack: N of 10240 bytes unused at its deepest`) so the hardware run can replace the estimate. The stack is internal heap taken at the first attempt and never freed; if the task cannot be created the attempt fails (cools down like a failed connect) and the next attempt tries again.
- **Simulated board.** One thread with the same shape (created on first use, reused, one job at a time), so the asynchrony is testable ([Simulated-board test harness](SIMULATION.md)).
- **Hand-over.** The job works on a copy of the profile taken when it starts (host, port, TLS, anchor, credentials), so a profile edited meanwhile is never read by it. While it runs it owns the transport, `PubSubClient` and the name lookup; the main loop touches none of them. It publishes its result through a release store that `MqttNet::asyncIdle()` reads with acquire. A successful job leaves the transport open and `PubSubClient` connected; the main loop then subscribes and publishes the status, and from then on `PubSubClient::loop()`, every publish and the command line over MQTT stay on the main loop as before. Never two attempts at once: nothing new is started while a job is unfinished.
- **Cancellation.** A profile edit that changes credentials, a lost Wi-Fi link, `set mqtt.enabled 0` and a posture that now forbids the profile being connected all raise a flag; the job reads it between its steps (after the lookup, after the transport opens, before CONNECT), closes what it opened and stops. It is never interrupted inside a blocking call, so no client is freed under it and none is leaked, but a cancelled attempt can take up to the remaining time of the call it is in (about 6 s) to wind down, and nothing new starts until it has. A result nobody wants any more (the core has left Connecting, or the attempt was cancelled) is dropped, and a session it did open is closed with a clean DISCONNECT. A posture cancel costs the profile no cooldown.
- **What is still blocking.** `WiFi.hostByName`, the TLS handshake and CONNECT block the worker, not the loop. The mDNS browse was already asynchronous. Publishing from the main loop to a broker that has stopped reading can still block on the socket write for the library's socket timeout; that was true before and is unchanged.

## Profiles

Up to six, each with a priority (0 to 255, default 50; higher is tried first, equal priorities go to the lower profile number) and optional credentials. A profile has one of two kinds.

| Kind | Fields | Rule |
|---|---|---|
| **Static** | host name or IPv4 address, port (default 8883 with TLS, 1883 without), TLS on or off (default on), optional anchor | The name is resolved again on **every** connect attempt, so a dynamic-DNS name keeps working when its address changes. With TLS and an anchor the broker is authenticated; with TLS and no anchor the link is encrypted but **unauthenticated** and status says `tls=unverified`; without TLS status says `tls=plain`. |
| **Discovery** (an enrolled LAN broker) | site ID, anchor | The device browses mDNS for `_secure-mqtt._tcp` and `_mqtt._tcp`. A responder qualifies **only** if its TXT record carries `site=<site ID>` **and** the TLS certificate it presents satisfies the anchor. Always TLS. **Plain MQTT is never used for a discovered broker:** a responder that advertises `_mqtt._tcp` is recorded as `last=plain` and never contacted. |

### The anchor

A profile has at most one **anchor**, of one of two kinds. `get mqtt.profile` shows `anchor=fingerprint`, `anchor=ca` or `anchor=unset`; setting one kind replaces the other.

| Kind | What the broker must present | Renewing the broker certificate |
|---|---|---|
| **Fingerprint** | the one certificate whose SHA-256 fingerprint is the anchor (the DER bytes of the certificate it presents; 64 hex digits, colons and capitals accepted). No chain is validated and no name is checked. | Every device must be reconfigured with the new fingerprint. |
| **CA certificate** | a certificate chaining to the CA certificate stored in the profile (the broker may send intermediates; the CA itself is the only trust anchor). | Nothing to do: any certificate from the same CA is accepted. |

An mDNS answer is unauthenticated; the anchor check is what makes a broker enrolled, so a rogue broker on the same LAN that copies the site ID cannot capture the device. Either kind refuses it, and a refused candidate cools down on its own (see below).

**What a CA anchor checks.**

- The chain from the presented certificate to the stored CA: signatures, validity dates, and the CA constraints.
- **The host name, for a static profile whose host is a name.** The certificate must carry that name (a DNS subject alternative name, or the common name when it has none; a leading wildcard label is honoured). A certificate for another name from the right CA is refused, so a broker with a valid certificate for a different service cannot stand in for this one.
- **Chain only, with no name check, for a static profile whose host is an IPv4 literal and for every discovery profile.** A literal has no name to check against (the ESP32's TLS library does not match an IP subject alternative name). A discovery profile learns the address from an mDNS answer, and the instance and host names in that answer are chosen by whoever answers: checking the certificate against them would prove nothing. What the profile relies on instead is the private CA and the site ID. The consequence is real and should be planned for: **any certificate issued by that CA is accepted by a discovery profile**, so use a CA dedicated to the site's brokers, not one that also issues other certificates.
- An expired or not-yet-valid certificate is refused. **On the ESP32, a device whose clock has never been set (before 2025) cannot judge validity dates, so for it only the date check is skipped**; the chain, the signatures and the name are still checked. Once the clock is set the dates count.

**SNI.** The host name is sent as the TLS server name with every TLS static profile that has one, whatever the anchor (none, fingerprint or CA), so a broker that serves several names behind one address presents the right certificate. A literal address and a discovery profile send none. A fingerprint anchor still checks no name: pinning the certificate is the whole decision.

**A refusal is reported as `last=pin`** for both kinds (the certificate does not satisfy the anchor: wrong fingerprint, wrong CA, wrong name, expired, or a stored CA that does not parse), and cools down for 5 minutes doubling to 1 hour, as a fingerprint mismatch always did.

**Limits of the stored CA.** One certificate per profile (not a bundle). At most 1,920 base64 characters, which is 1,440 DER bytes: enough for an RSA-4096 root (about 1,400 bytes; the test suite generates one) and every EC and RSA-2048 CA; a certificate larger than that is refused at `ca end`. It is kept in the heap only for profiles that have one (about 600 bytes for an EC P-256 CA, up to 1.9 KB for the largest) and a copy for the connect worker lives only while an attempt runs. A CA certificate is public material, not a secret; the listing prints its kind and not the body only to stay short.

### Setting a CA anchor over the console

A CA certificate is 600 to 1,900 characters and the console line takes 79, so it is sent in pieces and installed whole. `tools/mqtt_ca_commands.py <profile> <ca.pem>` prints the commands from a PEM file (the certificate itself, not the key; `openssl x509 -in ca.pem` extracts it from a bundle): a `ca begin`, one `ca add <48 base64 characters>` per piece (about 15 to 40), and a `ca end`. Paste or send them line by line.

- `ca begin` allocates a staging buffer (1.9 KB, heap) for that profile; `ca add` appends to it. Nothing is saved, and the profile's current anchor is untouched, until `ca end`.
- `ca end` decodes the text and installs it only if it is one complete DER certificate (a SEQUENCE whose declared length is exactly what arrived), so a damaged or truncated paste is refused with `ERR: ca is not one complete base64 DER certificate; the profile is unchanged`. It then saves the prefs and replaces any fingerprint anchor. That check proves the shape; it does not prove the certificate is the right one or parses as X.509, which the connect finds out (and reports as `pin`).
- A pasted `-----BEGIN CERTIFICATE-----` line is skipped, so the lines of a PEM file can be sent as they are. `ca begin` starts again; `ca clear` and `anchor clear` remove a CA; `set mqtt.profile <n> clear` removes the profile and its CA and drops its staging buffer. A second profile may stage only after the first finishes: there is one staging buffer.

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

All of them run on a trusted local transport; the TCP companion port refuses the CLI, and a remote CLI caller may run the `get` forms only. A broker may run them over MQTT only as the [Command-line engine](CLI.md) allows.

| Command | Effect |
|---|---|
| `set mqtt.profile <n> type static\|discovery` | `n` is 0 to 5 |
| `set mqtt.profile <n> host <name-or-ipv4>` | static only; 1 to 63 characters: letters, digits, `.` `-` `_` |
| `set mqtt.profile <n> port <0-65535>` | 0 means the default for the TLS setting |
| `set mqtt.profile <n> tls on\|off` | static only; `off` is refused in Hardened |
| `set mqtt.profile <n> anchor <64 hex>\|clear` | the SHA-256 fingerprint of the broker certificate; replaces a CA anchor; `clear` removes either kind |
| `set mqtt.profile <n> ca begin` / `ca add <base64>` / `ca end` / `ca clear` | a CA certificate as the anchor, in pieces ([above](#setting-a-ca-anchor-over-the-console)); replaces a fingerprint |
| `set mqtt.profile <n> site <id>` | discovery only; 1 to 23 characters |
| `set mqtt.profile <n> user <name>` / `pwd <secret>` | with no value, cleared; write-only |
| `set mqtt.profile <n> priority <0-255>` | higher is tried first |
| `set mqtt.profile <n> manage on\|off` | lets this broker manage the device over MQTT even in Hardened ([Command-line engine](CLI.md)); only a local caller sets it, off by default, shown in the listing only when on |
| `set mqtt.profile <n> clear` | removes the profile and its credentials |
| `get mqtt.profile <n>` | `<n>: static <host>:<port> tls=on\|off anchor=fingerprint\|ca\|unset prio=<p> user=set\|unset pwd=set\|unset last=<reason>`, or the discovery form |
| `get mqtt.profiles [from]` | every configured profile, paged with `... next:<n>` |
| `get mqtt.status` | `connected profile=<n> broker=<ip>:<port> tls=verified\|unverified\|plain up=<s>`, or the state: `idle`, `connecting`, `backoff`, `no-link`, `off`, with ` browsing` while a browse runs |
| `set mqtt.enabled 0\|1`, `get mqtt.enabled` | stop or resume without losing the profiles |

`last=` is the last failure of that profile: `none`, `dns`, `tcp`, `tls` (the TCP or TLS connect failed), `pin` (the certificate does not satisfy the anchor), `auth`, `timeout`, `dropped`, `lost` (the responder vanished), `posture` (Hardened forbids it) or `plain` (a discovery profile saw only a plain responder). A profile edit applies at once (a session on it is redone) with no reboot, as long as the controller is running; the controller starts at boot when Wi-Fi does.

## Status topic

On every connect, and again every 5 minutes, the device publishes a retained message (QoS 0) on `mecon/<node id>/status`, where the node id is the first four bytes of the public key as 8 hex digits, for example `{"online":true,"profile":1,"tls":"verified"}` with `tls` one of `verified`, `unverified`, `plain`. The broker publishes the last will `{"online":false}` on the same topic, retained, if the session ends without a DISCONNECT. The client id is `mecon-<node id>`; the session is clean. This topic is a Stage A placeholder: it is not part of the MECON contract and carries no secret.

## Storage

The profiles are saved in the Companion prefs file under `mqtt` (`en` and, per profile `i`, the keys `k<i>` kind, `p<i>` priority, `t<i>` TLS, `o<i>` port, `h<i>` host, `s<i>` site, `a<i>` fingerprint anchor, `u<i>` user, `w<i>` password, and for a CA anchor `c<i><nn>` its base64 in pieces of 96 characters, only the pieces that exist, because one prefs token holds 127 characters), so they use the same atomic save. A prefs file without them loads with no profile. The whole prefs file is 955 bytes with no MQTT profile and 2,419 bytes with six profiles of the longest allowed host, site ID, anchor, user name and password (measured by the unit test). **A CA anchor adds its base64 text plus about 9 bytes of key and quotes per 96 characters**: 664 bytes for an EC P-256 CA (450 DER bytes), and 2.1 KB for the largest CA the profile accepts (1,440 DER bytes); six of the largest add 12.5 KB (measured: 13,435 bytes in all). A profile without a CA adds nothing to the file, so a device that does not use the feature keeps the sizes above. RAM: 1,536 bytes for six profiles (256 bytes each) plus about 300 bytes for the controller state, in builds that have MQTT.

## Memory

Measured by compiling the Heltec V3 and V4 Wi-Fi builds at this change and at its base (the Wi-Fi controller):

| Build | RAM before | RAM with MQTT | Flash before | Flash with MQTT |
|---|---|---|---|---|
| Heltec V3 (`Heltec_v3_companion_radio_wifi`) | 174,056 B | 179,360 B (+5,304 B) | 1,117,761 B | 1,284,301 B (+166,540 B, 38.4 % of the app partition) |
| Heltec V4 (`heltec_v4_companion_radio_wifi`) | 174,020 B | 179,340 B (+5,320 B) | 1,116,089 B | 1,282,541 B (+166,452 B, 19.6 % of the app partition) |

**CA anchors and SNI, measured the same way against the base of this change:** Heltec V3 +112 B RAM and +3,220 B flash (1,302,481 to 1,305,701 B); Heltec V4 +112 B RAM and +3,236 B flash (RAM 114,020 to 114,132 B, flash 1,300,753 to 1,303,989 B). The static RAM is the connect job's host-name field and CA pointer; the CA text itself is heap, allocated only for a profile that has one (strlen + 1, 600 B to 1.9 KB), plus a copy of it for the duration of each connect attempt, plus 1.9 KB while a `ca begin` is open, plus about 1.5 KB briefly on the worker's heap for the decoded DER and the parsed CA during the chain check (the worker's stack is untouched by it). The effect on the TLS handshake's peak heap is not measured on hardware.

RAM is the static figure PlatformIO reports (data and bss): the profiles (1.5 KB), the controller and PubSubClient, and mbedTLS' static state. Flash grows by about 163 KB, almost all of it the TLS client and the mDNS component, which the Wi-Fi builds did not link before.

**TLS heap while a session is open (estimate, not measured on hardware).** The pinned Arduino core (2.0.17, ESP-IDF 4.4 libraries) is built with `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` off and `CONFIG_MBEDTLS_DYNAMIC_BUFFER` off, so mbedTLS keeps a 16 KiB input and a 16 KiB output record buffer (about 33 KB) for as long as the session lives, plus roughly 10 to 15 KB of handshake state that is released after the handshake (an ECDSA P-256 server certificate; an RSA-2048 certificate costs more during the handshake). The Espressif mbedTLS documentation ("Reducing heap usage") gives the same order of magnitude, 40 to 50 KB at the peak. The controller allocates the TLS client only when a session opens and frees it when it ends, so a device on a plain profile pays nothing. The cost of reducing the record buffers (`CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN`) needs a different core build and is not done here. Whether the free heap of the Companion Wi-Fi build leaves room for this at runtime next to the Wi-Fi stack is exactly what the hardware run has to show.

## Platforms

- **ESP32 family** (Heltec V3 and V4 Wi-Fi builds): implemented in `MqttNetEsp32.cpp` over `WiFiClient` / `WiFiClientSecure` (both anchors are checked after a handshake that skips the library's own validation: a fingerprint with `WiFiClientSecure::verify`, a CA with `mbedtls_x509_crt_verify` on the peer's chain, with the host name when there is one; the connect uses `WiFiClientSecure::connect(ip, port, host, ...)` to reach the resolved address while presenting the name as SNI; the whole sequence runs on the worker task described above) and the ESP-IDF `mdns_query_async_*` calls. Other ESP32 Wi-Fi builds opt in with `-D ENABLE_MQTT`, `+<helpers/mqtt/*.cpp>` and the library line.
- **RP2040: unsupported in this change.** The Companion builds for it keep working as before. The platform adapter would be a `MqttNet` over `WiFiClientSecure` (BearSSL, `setFingerprint`) and the core's LEAmDNS service query; neither is written, and the RP2040 mDNS responder cannot browse asynchronously the way this controller needs.
- Simulated boards: `sim_companion_wifi` and `sim_companion_wifi_usb`.

## Known limits

- A connect attempt (DNS, TCP, TLS handshake, CONNACK) takes up to about 12 s on the worker; the main loop is not blocked and `get mqtt.status` reads `connecting` meanwhile. Cancelling it (see above) waits for the worker to reach its next step.
- IPv4 only. A discovery responder is identified by address and port; at most six candidates are tracked, so a LAN that advertises many responders with the right site ID can delay discovery (each failed candidate cools down alone) but cannot make the device connect to one whose certificate does not match.
- A lost discovery responder is noticed by a browse, which runs only while a better discovery profile exists; on the best profile the session dropping is the signal.
- No client certificates. One CA certificate per profile, not a CA store or bundle (see "The anchor"). The CA anchor's validity-date check is skipped on an ESP32 whose clock is unset.
- Besides the retained status, the device subscribes to its own command-line request topic and answers on its response topic ([Command-line engine](CLI.md)); it subscribes to nothing else.
- Hardware: nothing here has run on a board. In particular the worker task (its stack figure, its core pinning, the Wi-Fi and mbedTLS calls from a task other than the loop task) is verified only in the simulator.
