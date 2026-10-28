# Telemetry catalogue

> **Status: catalogue recorded before the code. Built: `health`, `radio`, `power`, `runtime` (with the boot event; `heap_low` on the Companion), `observations`, `environment`, `inventory` counts and `logs` (every role with the device interface) and `connectivity` (Companion builds, with BLE and USB link state). `location`, `screen` and inventory entries wait for the Deployment authority stage.** Each family below says what is implemented today. This page is the one telemetry model: the device interface ([DEVICE_INTERFACE](DEVICE_INTERFACE.md)) carries it over MQTT, USB and BLE (Companion) and the console (Repeater, Room server), with the same meaning on every transport. Where [CORE_DEVICE_CONTRACT](CORE_DEVICE_CONTRACT.md) or [MQTT_PROTOCOL](MQTT_PROTOCOL.md) describe telemetry differently, this page is the newer decision.

## 1. Model

- **Streams.** Telemetry comes in named streams, one per family. A consumer starts one with `sub.start {"stream": <name>, "period_s", "lifetime_s"}` and stops it with `sub.stop`. A stream sends a snapshot at its period and, where the family has them, events when something changes. A subscription ends at its lifetime unless renewed, so a vanished consumer costs nothing.
- **Discovery.** `hello` lists `stream.<name>` in `capabilities` for every stream the build and board can produce. A stream the device cannot produce is absent from the list and `sub.start` answers `invalid` with `unknown-stream`. A device without a sensor, a screen or a battery reading omits that capability. It never sends zeros in its place.
- **Envelope.** Every event is `{"v":1,"ev":<stream>,"seq","epoch","ts","data"}` (DEVICE_INTERFACE section 3). `seq` counts per stream per device, so a gap on one transport is an event dropped there; `epoch` is the boot counter, so `(epoch, seq)` orders events across reboots and a Backend that receives one event over two paths drops the second copy.
- **Absent, null, zero, stale.** Absent: this build or board does not have the field. `null`: the field exists but is unknown right now (a noise floor not yet calibrated, a clock not yet set). `0`: measured zero. Where a value can go stale (a GPS fix, a sensor reading), the event carries its age in seconds beside it. A sensor that fails a read reports its key with `null` and the sensor's `fault` flag, not a missing key.
- **Counters.** Counters are cumulative since boot and restart at 0 with a new `epoch`; a counter reset by command (`clear stats`) is reported as an event. A consumer works out rates from successive snapshots.
- **Limits.** Each stream has a minimum period, a maximum event rate and a bounded per-transport queue. When a queue is full the event is dropped and a drop counter in the next event of that stream says how many. A slow or vanished consumer costs dropped events, never time in the radio loop (DEVICE_INTERFACE section 8).
- **Access.** Each stream has the class it needs in the CLI privilege table: `read` for operational state, more for anything private. A subscription is decided like the CLI line its data comes from, with the caller's real context, when `sub.start` arrives: a caller whose class does not allow the stream gets `refused`. `logs` is class `posture`, like `get audit`: the cable and a BLE link paired with a per-device PIN may subscribe; a broker session, a remote admin and a BLE link on the default PIN may not. Passwords, keys and secrets are never in any stream.

## 2. Streams

Status column: **built** is in the firmware; **next** is being built in this stage; **after trust** waits for the Deployment authority stage (its access rule needs a Deployment-signed subscriber); **unsupported** is what the hardware cannot provide.

| Stream | Content | Class | Default / minimum period | Status |
|---|---|---|---|---|
| `health` | uptime, heap and stack, battery, contacts | read | 60 s / 5 s | built |
| `runtime` | reset reason, abnormal reset, boot times, longest loop pass, heap-low state, a boot event | read | 300 s / 60 s | built |
| `radio` | packet and error counters, noise floor, last RSSI and SNR, airtime, duty cycle | read | 60 s / 5 s | built |
| `connectivity` | Wi-Fi, MQTT, BLE and USB link state; changes as events | read | 60 s / 10 s, plus an event at each change | built on Companion builds: Wi-Fi and MQTT where the build has a network, BLE and USB link state |
| `power` | battery voltage, external power, charger, USB and solar detection | read | 300 s / 30 s | built |
| `environment` | readings of the attached sensors, with units, and the MCU temperature | read | 300 s / 30 s | built |
| `location` | GNSS position, fix, satellites and age | location (private) | 300 s / 30 s | after trust |
| `observations` | each received packet: raw bytes, RSSI, SNR | read | event per packet, at most 10/s (no period) | built |
| `logs` | each new audit-ring entry: posture changes, refusals, failed logins, resets, mode changes, low memory | posture | event per entry, at most 5/s (no period) | built (audit events; bounded diagnostics after trust) |
| `inventory` | contacts, channels, neighbours, clients: counts (entries later) | read for counts; private for entries | 3600 s / 300 s | counts built; entries after trust |
| `screen` | framebuffer snapshots of the device's own display | private | on request, at most 1 per 5 s | after trust |

## 3. Fields

### `health` (built)
| Field | Unit | Source | Roles | Notes |
|---|---|---|---|---|
| `uptime_s` | s | millis | all | |
| `heap.free`, `heap.largest` | bytes | `memStats()` | all | internal RAM; `heap` is `null` where the platform has no heap API. On RP2040 and nRF52 `largest` equals `free` (no largest-block API) |
| `heap.min_free` | bytes | `memStats()` | ESP32 | lowest since boot; `null` where untracked |
| `heap.loop_stack_free` | bytes | `memStats()` | ESP32 | least free stack of the loop task since it started |
| `heap.psram_free` | bytes | `memStats()` | boards with PSRAM | absent without |
| `battery_mv` | mV | `getBattMilliVolts()` | all | `null` when the board cannot measure it |
| `contacts.count`, `.capacity` | entries | contact store | Companion | absent on the server roles |

### `runtime` (built)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `reset_reason` | text | `getResetReason()` | ESP32 and nRF52; `null` elsewhere. Includes panic, watchdog and brownout on ESP32 |
| `abnormal_reset` | bool | derived from `reset_reason` | true for panic, watchdog, brownout; `null` where the reason is unknown |
| `shutdown_reason` | text | nRF52 GPREGRET2 | nRF52 only |
| `boot_ms.store`, `.mesh`, `.setup` | ms | `BootTimes` | `null` for an unmarked stage |
| `boot` | bool | the boot event | `true`, on the boot event only (below) |
| `firmware`, `upstream`, `hardware`, `role` | text | as `hello` | the boot event only |
| `loop_gap_ms.max` | ms | the device interface's loop tick | the longest main-loop pass since the previous runtime event; reading it resets it. 0 before two passes |
| `heap_low` | bool | `HeapFloor` | Companion; the low-memory state the display shows. Absent where the build does not check the floor (it is enforced on ESP32 only) |
| `gnss.powered` | bool | board (`getGnssHealth`) | T1000-E; the whole `gnss` object is absent on a board without a GNSS receiver |
| `gnss.fix` | bool | GNSS receiver | `null` while the receiver is powered off |
| `gnss.sats` | count | GNSS receiver | satellites used in the last fix; 0 while powered off |
| `gnss.fix_age_s` | s | GNSS receiver | since the last GGA/RMC sentence with a valid fix; `null` when there has been none this boot. It keeps counting after the receiver is powered off |

**`gnss` is the receiver's state, never a position.** It carries no latitude, longitude, altitude, speed or heading, rounded or not: those are the private `location` stream. Fix and satellite count also appear there, with the position.

**Boot event.** The first time a runtime subscription starts in an epoch, a second runtime event follows the first snapshot at once: `{"boot": true, "firmware", "upstream", "hardware", "role", "reset_reason"}`. It is sent once per epoch and per device (a later subscription in the same epoch, on any transport, gets snapshots only; `hello` carries the same identity), with the next `seq`, to every transport subscribed at that moment. It has none of the snapshot members.

A crash record (panic backtrace kept across a reset) is **unsupported** in this firmware today: nothing keeps one. It needs an ESP32 core-dump partition or an RTC-retained record and is a separate piece of work.

### `radio` (built)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `rx.flood`, `rx.direct`, `tx.flood`, `tx.direct` | packets | Dispatcher counters | |
| `rx.total`, `tx.total`, `rx.errors` | packets | radio driver counters | |
| `queue_full`, `cad_timeout`, `startrx_timeout` | bool | Dispatcher error flags | set since the last `clear stats` |
| `noise_floor_dbm` | dBm | radio driver | `null` until calibrated |
| `last.rssi_dbm`, `last.snr_db` | dBm, dB | last received packet | `null` before the first packet |
| `airtime.tx_ms`, `airtime.rx_ms` | ms | Dispatcher | cumulative since boot |
| `duty.limit_bp`, `duty.used_ms`, `duty.limit_ms` | 0.01 %, ms | duty window | the trailing hour; seeded at the limit after boot |

### `connectivity` (built)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `mode` | `ble`/`wifi` | connectivity mode | Companion builds with modes |
| `wifi.state`, `.profile`, `.ssid`, `.rssi_dbm`, `.ip`, `.up_s` | | Wi-Fi controller | SSID shown, password never; fields of a link that is not up are `null` |
| `wifi.last_fail` | text | Wi-Fi core | the reason the last join failed |
| `mqtt.state`, `.profile`, `.broker`, `.tls`, `.up_s` | | MQTT controller | `tls` is `plain`, `verified` or `unverified` |
| `mqtt.reconnects` | count | MQTT controller | sessions after the first since boot |
| `mqtt.browsing` | bool | MQTT controller | looking for a discovery broker |
| `ble.connected` | bool | BLE interface | Companion builds with BLE; `false` while the BLE stack is not running (Wi-Fi mode, Hardened with a default PIN). Absent on builds without BLE |
| `usb.connected` | bool | the USB serial port | Companion builds with a USB interface; `null` where the board cannot tell (below) |

Events: one on every state change of Wi-Fi, MQTT, BLE or USB, carrying the new state. A build without a network stack omits `wifi` and `mqtt`.

**USB host link.** A port behind a UART bridge chip (the CP210x on the Heltec V3) is open whether or not a host is attached, so `usb.connected` is `null` there. A native USB CDC port (the Heltec V4) can report its line state, but no hardware board enables that yet (it needs a hardware run to confirm what the port reports), so `usb.connected` is `null` on every hardware board today. The simulated boards report their serial client, which is how the event is tested.

### `power` (built)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `battery_mv` | mV | board | `null` when not measurable |
| `external` | bool | board | the nRF52 power manager, or the board's own detect pin (`hasPowerDetect`: the T1000-E); `null` where the board cannot tell |
| `charging` | bool | board | the same; on the T1000-E the charger's status pin. `null` where the board cannot tell |
| `usb_power`, `solar` | bool | board detection | absent where the board has no detection |
| `boot_mv` | mV | nRF52 | battery at boot |

No state of charge is reported: no board here measures it, and a percentage computed from voltage is not a measurement.

### `environment` (built)
`readings`: one entry per attached sensor reading, `{"channel", "quantity", "value", "unit"}`, decoded from the Cayenne LPP the sensor manager already produces for the mesh's own telemetry (environment permission only, never location). Quantities and units: temperature (C), humidity (%), pressure (hPa), voltage (V), current (A), power (W), percentage (%), altitude (m), distance (m), luminosity (lx), energy (kWh). `mcu_temp_c` is present where the board reads its MCU temperature. Sensors are detected at boot; one that is not fitted has no entry, and with nothing to report the stream is absent from `hello`, rechecked once a minute. Not yet: a sensor's age and fault flag (the sensor managers do not report either).

### `observations` (built)
Each received packet: `{"raw": <hex>, "len", "rssi_dbm", "snr_db", "dropped"}`, before parsing, as the radio heard it. The payload is the mesh's own ciphertext; nothing is decrypted for it. At most 10 events per second per device; `dropped` counts the packets not sent since the previous event. `sub.start` takes no `period_s` for it (refused with `no-period`, reported as `null`). On the console, a packet that arrives while a request line is being typed counts as dropped.

### `inventory` counts (built)
Each count is `{"count", "capacity"}`. Companion: `contacts`, `channels` (a channel with a name). Repeater: `neighbours` (those heard since boot), `clients` (the ACL). Room server: `clients`.

### `logs` (built)
One event per new entry of the audit ring, when it is recorded (an event stream: `sub.start` takes no `period_s`). A repeat of an earlier entry is folded in the ring and sent again with its running `count`. Entries loaded from flash at boot are history, not events.

| Field | Notes |
|---|---|
| `ts` | Unix seconds when recorded; `null` while the clock was not set |
| `event` | `posture`, `refused`, `login-fail`, `reset`, `mode`, `low-mem` |
| `caller` | `cable`, `ble`, `net`, `remote-admin`, `remote-cli`, `button`, `mqtt`, `system` |
| `what` | the command class refused, or the new posture (`normal`, `hardened`) or mode (`ble`, `wifi`) |
| `count` | how many times this (event, caller, what) has been recorded |
| `dropped` | entries not sent since the previous event |

Names, never codes. The audit ring holds no secret, password or command argument, so none can appear here. At most 5 events a second (a burst of 5); an entry over the rate, or one that finds the 8-entry queue full, is dropped and counted in `dropped` of the next event. Available on the Companion and on the Repeater and Room server console. Bounded diagnostics (a general log ring) are not built: its size and retention are set when it is.

### `location`, `inventory` entries, `screen` (after trust)
- `location`: latitude, longitude (degrees), altitude (m), satellites, fix, `age_s`. Location is private: its class is decided with the Deployment authority stage.
- `inventory` entries: contact and neighbour entries. Counts are `read`; entries are private.
- `screen`: a snapshot of the device's own framebuffer, `{"width","height","format":"mono1","on","age_s","data"}`, fragmented to the transport's size. The Heltec boards' SSD1306 (128x64, 1,024 bytes a frame) exposes its buffer; e-ink and TFT drivers are reported per board when built. The display is never woken or changed by a capture. A frame can show messages and the BLE pairing PIN, so its access rule is the strictest of any stream and is set with the Deployment authority stage.

## 4. Per board and role

Filled in as each stream is built and measured: implemented, unavailable on that board, or unsupported, for the Heltec V3 and V4 Companion standalone builds, the Heltec V3 Repeater and Room server, and the simulated boards.
