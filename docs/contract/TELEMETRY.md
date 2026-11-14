# Telemetry catalogue

This page is the one telemetry model. The device interface ([DEVICE_INTERFACE](DEVICE_INTERFACE.md)) carries it over USB, BLE and, on the Heltec boards, MQTT (Companion), and over the console and, on the Heltec boards, MQTT (Repeater, Room server). Every transport gives a stream the same meaning: a transport may limit sizes and rates, never change what a field means. Which streams a device produces depends on its build and board: `hello` lists `stream.<name>` for each one, and `sub.start` on any other answers `invalid` with `unknown-stream`.

## 1. Model

- **Streams.** Telemetry comes in named streams, one per family. A consumer starts one with `sub.start {"stream": <name>, "period_s", "lifetime_s"}` and stops it with `sub.stop`. A stream sends a snapshot at its period and, where the family has them, events when something changes. A subscription ends at its lifetime unless renewed, so a vanished consumer costs nothing.
- **Discovery.** `hello` lists `stream.<name>` in `capabilities` for every stream the build and board can produce. A stream the device cannot produce is absent from the list and `sub.start` answers `invalid` with `unknown-stream`. A device without a sensor, a screen or a battery reading omits that capability. It never sends zeros in its place.
- **Envelope.** Every event is `{"v":1,"ev":<stream>,"seq","epoch","ts","data"}` (DEVICE_INTERFACE section 3). `seq` counts per stream per device, so a gap on one transport is an event dropped there; `epoch` is the boot counter, so `(epoch, seq)` orders events across reboots and a Backend that receives one event over two paths drops the second copy.
- **Absent, null, zero, stale.** Absent: this build or board does not have the field. `null`: the field exists but is unknown right now (a noise floor not yet calibrated, a clock not yet set). `0`: measured zero. Where a value can go stale (a GPS fix, a sensor reading), the event carries its age in seconds beside it. A sensor that fails a read reports its key with `null`, not a missing key.
- **Counters.** Counters are cumulative since boot and restart at 0 with a new `epoch`; a counter reset by command (`clear stats`) is reported as an event. A consumer works out rates from successive snapshots.
- **Limits.** Each stream has a minimum period, a maximum event rate and a bounded per-transport queue. An event over a stream's rate is dropped, and a drop counter in the next event of that stream says how many. An event that finds its transport's queue full is dropped too, leaving a gap in `seq` and `usig_seq` on that transport. A Companion counts these in `status.get` `events_dropped`. *(Since v0.3.6.)* A slow or vanished consumer costs dropped events, never time in the radio loop (DEVICE_INTERFACE section 8). On an nRF52 Companion's USB cable, a frame that finds no room for 20 ms is dropped whole and counted in `status.get` `cable_dropped`. *(Since v0.3.6.)* On a Companion, no event is built while a request is part-way through arriving in frames on any link: building and signing one holds the loop off the port, which on a Heltec V3's UART is long enough to lose part of the request. An event that falls due then goes out once the request is whole, or once the reassembly timeout drops it (5 s at most).
- **Access.** Each stream has the class it needs in the CLI privilege table: `read` for operational state, more for anything private. A subscription is decided like the CLI line its data comes from, with the caller's real context, when `sub.start` arrives: a caller whose class does not allow the stream gets `refused`. `logs` is class `posture`, like `get audit`: the cable and a BLE link paired with a per-device PIN may subscribe, and so may a subscriber whose `sub.start` a Deployment member signed (owner decision of 2026-11-01), over a link that may itself carry private data: a TLS-verified broker session, not an unverified one. The signed request is the ordinary signed envelope ([AUTHORITY](AUTHORITY.md) section 3.3), and the events go to the device's usual `ev/logs` topic. An unsigned broker session, a remote admin and a BLE link on the default PIN may not. The private streams (`location`, inventory entries, `screen`) are class `private`, by the owner's rule of 2026-10-28, "All this should go via the broker to the backend": a TLS-verified broker session may subscribe, as may the cable, a BLE link with the device's own PIN and a signed request. A LoRa caller, the Ethernet console, an unverified broker session and the untrusted links may not ([CLI](../CLI.md)). For every stream beyond `read`, the link that carries it must also be allowed it by itself: a signed `sub.start` on the open TCP port or a default-PIN BLE link sends neither logs nor private data over that link. A link's subscriptions end when it disconnects, with anything still queued for it, so the next client on that link inherits nothing. That needs a link that can tell: BLE and the TCP port do; a USB port behind a UART bridge cannot, so a cable's subscription lasts until its lifetime (at most 600 s), and the cable is trusted by possession. Passwords, keys and secrets are never in any stream.

## 2. Streams


| Stream | Content | Class | Default / minimum period |
|---|---|---|---|
| `health` | uptime, heap and stack, battery, contacts | read | 60 s / 5 s |
| `runtime` | reset reason, abnormal reset, boot times, longest loop pass, heap-low state, a boot event | read | 300 s / 60 s |
| `radio` | packet and error counters, noise floor, last RSSI and SNR, airtime, duty cycle | read | 60 s / 5 s |
| `connectivity` | Wi-Fi, MQTT, BLE and USB link state; changes as events | read | 60 s / 10 s, plus an event at each change |
| `power` | battery voltage, external power, charger, USB and solar detection | read | 300 s / 30 s |
| `environment` | readings of the attached sensors, with units, and the MCU temperature | read | 300 s / 30 s |
| `location` | GNSS position, fix, satellites and age | private | 300 s / 30 s |
| `observations` | each received packet: raw bytes, RSSI, SNR | read | event per packet, at most 10/s (no period) |
| `logs` | each new audit-ring entry: posture changes, refusals, failed logins, resets, mode changes, low memory | posture | event per entry, at most 5/s (no period) |
| `inventory` | contacts, channels, neighbours, clients: counts; on the Companion a paged snapshot of the entries | private | 3600 s / 300 s |
| `rf` | the replies to this Companion's `rf.request`: `{"from","xid","message"}`, as they came. A reply is never dropped silently: one too large for an event once escaped is `{"from","xid","error":"too-large","size"}`; one whose event waits for room on a subscribed link's queue is held up to 10 s (one at a time), with other events on that queue leaving room for it meanwhile, then sent as `"error":"queue-full"` if it still cannot go (since v0.3.6). Companion only | private | event |
| `screen` | framebuffer snapshots of the device's own display | private | 60 s; at most 1 per 5 s |

## 3. Fields

### `health`
| Field | Unit | Source | Roles | Notes |
|---|---|---|---|---|
| `uptime_s` | s | millis | all | |
| `heap.free`, `heap.largest` | bytes | `memStats()` | all | internal RAM; `heap` is `null` where the platform has no heap API. On RP2040 and nRF52 `largest` equals `free` (no largest-block API) |
| `heap.min_free` | bytes | `memStats()` | ESP32 | lowest since boot; `null` where untracked |
| `heap.loop_stack_free` | bytes | `memStats()` | ESP32, nRF52 | least free stack of the loop task since it started (on nRF52 the task's FreeRTOS high-water mark); `null` elsewhere |
| `heap.worker_stack_free` | bytes | `NrfWorker::workerStackFree()` | nRF52, every role | least free stack of the interface worker task, which runs every request on nRF52, since it started (FreeRTOS high-water mark); absent elsewhere, and while no worker exists |
| `heap.psram_free` | bytes | `memStats()` | boards with PSRAM | absent without |
| `battery_mv` | mV | `getBattMilliVolts()` | all | `null` when the board cannot measure it |
| `contacts.count`, `.capacity` | entries | contact store | Companion | absent on the server roles |

### `runtime`
| Field | Unit | Source | Notes |
|---|---|---|---|
| `reset_reason` | text | `getResetReason()` | ESP32 and nRF52; `null` elsewhere. Includes panic, watchdog and brownout on ESP32 |
| `abnormal_reset` | bool | derived from `reset_reason` | true for panic, watchdog, brownout, lockup; `null` where the reason is unknown |
| `shutdown_reason` | text | nRF52 GPREGRET2 | not emitted. The register is kept only where a build enables nRF52 power management (`NRF52_POWER_MANAGEMENT`), and no released board does: neither the T1000-E nor the Wio Tracker L1. Correction 2026-11-08: this row said "not emitted yet", as if it were planned |
| `boot_ms.store`, `.mesh`, `.setup` | ms | `BootTimes` | `null` for an unmarked stage |
| `boot` | bool | the boot event | `true`, on the boot event only (below) |
| `firmware`, `upstream`, `hardware`, `role` | text | as `hello` | the boot event only |
| `loop_gap_ms.max` | ms | the device interface's loop tick | the longest main-loop pass since the previous runtime event; reading it resets it. 0 before two passes |
| `heap_low` | bool | `HeapFloor` | Companion, Repeater and Room server; the low-memory state (on the Companion, the one its display shows). Absent where the build does not check the floor: it is enforced on ESP32 only, because nRF52 has no largest-block figure |
| `heap_lows` | object | `HeapWatch` | Beside `heap_low`, wherever it is present: `{"largest","min_free"}`, each `{"bytes","by","at_s"}` or `null` before the first look. `bytes` is the lowest largest free block, or the lowest free heap, since boot. `by` is what was running then: an operation's name, `request` (the envelope, signature and signed result around it), `stream.<name>`, `mqtt.connect`, `mqtt.request`, `mqtt.publish`, or `idle` (outside all of these). `at_s` is seconds since boot. It says what took the heap down, so a `heap_low` can be traced to its cause. |
| `stale_logins` | count | the login path | Repeater and Room server; remote logins refused since boot because the timestamp was not newer than that client's last accepted one. Absent on the Companion, which takes no remote login |
| `stale_login_last` | hex | the login path | Repeater and Room server; the 4-byte key prefix of the last client refused that way, `null` before the first. A client locked out by a clock that once ran ahead shows here by name, and a restart clears both along with the lockout |
| `gnss.powered` | bool | board (`getGnssHealth`) | T1000-E only. The Heltec V4 builds with a GPS, but its board has no `getGnssHealth` yet, so it reports no `gnss` object. The whole object is absent on a board without a GNSS receiver |
| `gnss.fix` | bool | GNSS receiver | `null` while the receiver is powered off |
| `gnss.sats` | count | GNSS receiver | satellites used in the last fix; 0 while powered off |
| `gnss.fix_age_s` | s | GNSS receiver | since the last GGA/RMC sentence with a valid fix; `null` when there has been none this boot. It keeps counting after the receiver is powered off |

**`gnss` is the receiver's state, never a position.** It carries no latitude, longitude, altitude, speed or heading, rounded or not: those are the private `location` stream. Fix and satellite count also appear there, with the position.

**Boot event.** The first time a runtime subscription starts in an epoch, a second runtime event follows the first snapshot at once: `{"boot": true, "firmware", "upstream", "hardware", "role", "reset_reason"}`. It is sent once per epoch and per device (a later subscription in the same epoch, on any transport, gets snapshots only; `hello` carries the same identity), with the next `seq`, to every transport subscribed at that moment. It has none of the snapshot members.

A crash record (panic backtrace kept across a reset) is **unsupported** in this firmware today: nothing keeps one. It needs an ESP32 core-dump partition or an RTC-retained record and is a separate piece of work.

### `radio`
| Field | Unit | Source | Notes |
|---|---|---|---|
| `rx.flood`, `rx.direct`, `tx.flood`, `tx.direct` | packets | Dispatcher counters | |
| `rx.total`, `tx.total`, `rx.errors` | packets | radio driver counters | |
| `queue_full`, `cad_timeout`, `startrx_timeout` | bool | Dispatcher error flags | set since the last `clear stats` |
| `noise_floor_dbm` | dBm | radio driver | `null` until calibrated |
| `last.rssi_dbm`, `last.snr_db` | dBm, dB | last received packet | `null` before the first packet |
| `airtime.tx_ms`, `airtime.rx_ms` | ms | Dispatcher | cumulative since boot |
| `duty.limit_bp`, `duty.used_ms`, `duty.limit_ms` | 0.01 %, ms | duty window | the trailing hour; seeded at the limit after boot |

### `connectivity`
| Field | Unit | Source | Notes |
|---|---|---|---|
| `mode` | `ble`/`wifi` | connectivity mode | Companion builds with modes |
| `wifi.state`, `.profile`, `.ssid`, `.rssi_dbm`, `.ip`, `.up_s` | | Wi-Fi controller | SSID shown, password never; fields of a link that is not up are `null` |
| `wifi.last_fail` | text | Wi-Fi core | the reason the last join failed |
| `mqtt.state`, `.profile`, `.broker`, `.tls`, `.up_s` | | MQTT controller | `tls` is `plain`, `verified` or `unverified` |
| `mqtt.reconnects` | count | MQTT controller | sessions after the first since boot |
| `mqtt.write_ms_max` | ms | MQTT controller | the longest one publish held the main loop since boot. A TLS write that cannot progress holds it, keepalive pings included, up to the socket timeout |
| `mqtt.slow_writes` | count | MQTT controller | publishes since boot that held the loop for 1 s or more |
| `mqtt.browsing` | bool | MQTT controller | looking for a discovery broker |
| `ble.connected` | bool | BLE interface | Companion builds with BLE; `false` while the BLE stack is not running (Wi-Fi mode, Hardened with a default PIN). Absent on builds without BLE |
| `usb.connected` | bool | the USB serial port | Companion builds with a USB interface; `null` where the board cannot tell (below) |

Events: one on every state change of Wi-Fi, MQTT, BLE or USB, carrying the new state. A build without a network stack omits `wifi` and `mqtt`.

**USB host link.** A port behind a UART bridge chip (the CP210x on the Heltec V3) is open whether or not a host is attached, so `usb.connected` is `null` there. A native USB CDC port (the Heltec V4) can report its line state, but no hardware board enables that yet (it needs a hardware run to confirm what the port reports), so `usb.connected` is `null` on every hardware board today. The simulated boards report their serial client, which is how the event is tested.

### `power`
| Field | Unit | Source | Notes |
|---|---|---|---|
| `battery_mv` | mV | board | `null` when not measurable |
| `external` | bool | board | the nRF52 power manager, or the board's own detect pin (`hasPowerDetect`: the T1000-E); `null` where the board cannot tell |
| `charging` | bool | board | the same; on the T1000-E the charger's status pin. `null` where the board cannot tell |
| `usb_power`, `solar` | bool | board detection | absent where the board has no detection |
| `boot_mv` | mV | nRF52 | battery at boot |

No state of charge is reported: no board here measures it, and a percentage computed from voltage is not a measurement.

### `environment`
`readings`: one entry per attached sensor reading, `{"channel", "quantity", "value", "unit"}`, decoded from the Cayenne LPP the sensor manager already produces for the mesh's own telemetry (environment permission only, never location). Quantities and units: temperature (C), humidity (%), pressure (hPa), voltage (V), current (A), power (W), percentage (%), altitude (m), distance (m), luminosity (lx), energy (kWh). `mcu_temp_c` is present where the board reads its MCU temperature. Sensors are detected at boot; one that is not fitted has no entry, and with nothing to report the stream is absent from `hello`, rechecked once a minute. Not yet: a sensor's age and fault flag (the sensor managers do not report either).

### `observations`
Each received packet: `{"raw": <hex>, "len", "rssi_dbm", "snr_db", "dropped"}`, before parsing, as the radio heard it. The payload is the mesh's own ciphertext; nothing is decrypted for it. At most 10 events per second per device; `dropped` counts the packets not sent since the previous event. `sub.start` takes no `period_s` for it (refused with `no-period`, reported as `null`). On any stream, a `period_s` outside its bounds answers `invalid` `bad-period`, and a `lifetime_s` outside 10..600 answers `bad-lifetime`. On the console, a packet that arrives while a request line is being typed counts as dropped.

### `inventory`
Each count is `{"count", "capacity"}`. Companion: `contacts`, `channels` (a channel with a name). Repeater: `neighbours` (those heard since boot), `clients` (the ACL). Room server: `clients`. The server roles send the counts, one event per period.

**Companion: a paged snapshot.** Each time the stream is due (at `sub.start` and every period after), the Companion sends a snapshot of its channels and contacts as consecutive events, each a page: `{"snapshot", "part", "contacts", "channels", "entries", "last"}`. `snapshot` numbers the snapshot within the boot and `part` the page from 1; the counts are on every page; `last` is `true` on the final page. A Backend keeps the newest complete snapshot.
- A channel entry is `{"kind":"channel","index","name"}`; never the channel's secret.
- A contact entry is `{"kind":"contact","key","name","type","fav","last_advert","hops"}`, with `lat_e6`/`lon_e6` when the contact advertised a position. `key` is the contact's public key (64 hex), `type` one of `chat`, `repeater`, `room`, `sensor`, `none`, `other`, `last_advert` the contact's own clock, `hops` the length of the known path or `-1` for none (flood). Never a shared secret or the path bytes.
- Pages are filled until the next entry would not fit, and each event stays under 1,500 bytes (about 4 contacts a page). A page waits until every subscribed link's queue can take it. One page goes per loop pass, at most 5 a second, so a list of hundreds never holds the RF loop. A snapshot that is due again before it finishes is completed first and then sent once more; it is never restarted, so a caller that keeps subscribing cannot stop a Backend from getting a complete one. Contacts read while a snapshot is under way are read as they are at that moment, so a change mid-snapshot shows up complete in the next one.
- Class `private` (see **Access** above), counts included. Every caller that can reach the device interface today has the same `read` and `private` rights, so no subscriber that had the counts loses them.

### `logs`
One event per new entry of the audit ring, when it is recorded (an event stream: `sub.start` takes no `period_s`). A repeat of an earlier entry is folded in the ring and sent again with its running `count`. Entries loaded from flash at boot are history, not events.

| Field | Notes |
|---|---|
| `ts` | Unix seconds when recorded; `null` while the clock was not set |
| `event` | `posture`, `refused`, `login-fail`, `reset`, `mode`, `low-mem`, `unlock`, `clock-back` |
| `caller` | `cable`, `ble`, `net`, `remote-admin`, `remote-cli`, `button`, `mqtt`, `system` |
| `what` | the command class refused, or the new posture (`normal`, `hardened`) or mode (`ble`, `wifi`) |
| `count` | how many times this (event, caller, what) has been recorded |
| `dropped` | entries not sent since the previous event |

Names, never codes. The audit ring holds no secret, password or command argument, so none can appear here. At most 5 events a second (a burst of 5); an entry over the rate, or one that finds the 8-entry queue full, is dropped and counted in `dropped` of the next event. Available on the Companion and on the Repeater and Room server, over their console and, on the Heltec boards, MQTT. Bounded diagnostics (a general log ring) are not built: its size and retention are set when it is.

### `location`
| Field | Unit | Notes |
|---|---|---|
| `fix` | bool | the receiver has a valid fix |
| `lat_e6`, `lon_e6` | millionths of a degree | integers, negative south and west; `null` without a fix |
| `alt_m` | m | `null` without a fix |
| `sats` | count | satellites the receiver reports |
| `age_s` | s | since the last valid fix; `null` when the receiver cannot tell or has had none this boot |

Integers, never floats: a microdegree is about 0.1 m. The stream is present only on a build whose board has a location provider (the receiver is detected at boot and rechecked once a minute); a board without one omits `stream.location` from `hello`. Class `private` (see **Access** above). No other stream or log entry carries a position. On the T1000-E and other NMEA receivers, `age_s` needs the provider's fix age; a provider that cannot tell reports `null`.

### `screen`
- `screen`: diagnostic telemetry from the device to the manager, never a way to write onto the display. The stream exists on a board whose display can be read: today the SSD1306 of the Heltec V3 and V4 and the SH1106 of the Wio Tracker L1 (both 128x64). A board with another display, or none, does not list `stream.screen`. A copy of the last drawn frame is taken once when the stream is due and sent in parts, one every 200 ms or so. Taking it does not wake or change the display.
- Every part: `{"frame","part","last","width","height","format":"mono1","on"}`. `frame` numbers frames within a boot, `part` counts from 1, `last` marks the final part, and `on` says whether the panel is lit.
- A part with pixels adds `age_s` (seconds since this frame was drawn), `offset` (its first byte in the frame) and `data` (base64, at most 512 bytes before encoding). A frame of 1,024 bytes is two parts. A manager joins them by `offset`. A frame missing a part is discarded, and the next period is the retry: a part is never sent again.
- `mono1`: `width * height / 8` bytes. Each byte is one column of 8 pixels, lowest bit on top. Bytes run left to right, in rows of 8 pixels from the top, and a set bit is a lit pixel.
- A part with no pixels has `last: true` and `withheld`, and no `age_s`, `offset` or `data`: `pairing-pin` when the frame shows the BLE pairing PIN, `no-frame` when nothing has been drawn since boot, `no-memory` when the copy could not be made.
- A frame can show message text. In Hardened the device draws no message text or names ([UI](../UI.md)), and the frame shows what the panel shows.

## Compact reports over LoRa

A small binary subset of the producers above, for the radio, with the same meanings and units. `report.now` sends one, and a [standing instruction](OPERATIONS.md) sends them on a schedule. Nothing else goes over LoRa as telemetry: no subscription, no log, no screen.

**Layout, little-endian:** `0x1E`, version (1), fields, boot u32, seq u32, time u32 (0: the device does not know the time), the fields in bit order, then a 64-byte signature. `tools/mecon_report.py` decodes one.

| Bit | Field | Bytes | Contents |
|---|---|---|---|
| 1 | `health` | 8 | uptime_s u32; heap free in KB u16; battery mV u16 |
| 2 | `radio` | 12 | rx u32; tx u32; error flags u8; noise floor dBm i8; last RSSI dBm i8; last SNR in quarters of a dB i8 |
| 4 | `location` | 14 | fix u8; lat_e6 i32; lon_e6 i32; alt_m i16; sats u8; age_s u16 |

- **Unknown is not zero.** 0xFFFF (u16) and 0x7F (i8) mean unknown. A board that cannot produce a field answers `unsupported-field` when it is asked for; it never sends a made-up value.
- **Provenance.** The signature is the node key's, over `MECON-UPLINK-v1` 0x00 `report` 0x00 and the bytes before it, using the same boot and sequence as every event (AUTHORITY section 3.4), so a repeated or reordered report shows. A boot that cannot sign sends none. A full report is at most 113 bytes.
- **Where it goes.** To a node key as an anonymous datagram, or on a channel. A Repeater or Room server floods the datagram and uses its bound management channel. A Companion sends the datagram by the contact's path, or directly to a neighbour when the node is not a contact, and `slot` must name its bound management channel, never a public one. On a channel the frame carries the sender's 8-byte key prefix and, if named, the target's. A Companion that hears one puts it on its `rf` stream as `{"from","report"}` (base64), for the manager to verify.
- **Private.** `location` needs the right to read it, and never goes on a channel, where every member could read it.
- **Limits.** `report.now` at most once in 30 s (`busy`, `rate-limited`). Every report goes through the duty window like any other transmission.
