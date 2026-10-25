# Telemetry catalogue

> **Status: catalogue recorded before the code. Built: `health`, `radio`, `power`, `runtime`, `observations` (every role with the device interface) and `connectivity` (Companion builds with a network). The rest is being built.** Each family below says what is implemented today. This page is the one telemetry model: the device interface ([DEVICE_INTERFACE](DEVICE_INTERFACE.md)) carries it over MQTT, USB and BLE (Companion) and the console (Repeater, Room server), with the same meaning on every transport. Where [CORE_DEVICE_CONTRACT](CORE_DEVICE_CONTRACT.md) or [MQTT_PROTOCOL](MQTT_PROTOCOL.md) describe telemetry differently, this page is the newer decision.

## 1. Model

- **Streams.** Telemetry comes in named streams, one per family. A consumer starts one with `sub.start {"stream": <name>, "period_s", "lifetime_s"}` and stops it with `sub.stop`. A stream sends a snapshot at its period and, where the family has them, events when something changes. A subscription ends at its lifetime unless renewed, so a vanished consumer costs nothing.
- **Discovery.** `hello` lists `stream.<name>` in `capabilities` for every stream the build and board can produce. A stream the device cannot produce is absent from the list and `sub.start` answers `invalid` with `unknown-stream`. A device without a sensor, a screen or a battery reading omits that capability. It never sends zeros in its place.
- **Envelope.** Every event is `{"v":1,"ev":<stream>,"seq","epoch","ts","data"}` (DEVICE_INTERFACE section 3). `seq` counts per stream per device, so a gap on one transport is an event dropped there; `epoch` is the boot counter, so `(epoch, seq)` orders events across reboots and a Backend that receives one event over two paths drops the second copy.
- **Absent, null, zero, stale.** Absent: this build or board does not have the field. `null`: the field exists but is unknown right now (a noise floor not yet calibrated, a clock not yet set). `0`: measured zero. Where a value can go stale (a GPS fix, a sensor reading), the event carries its age in seconds beside it. A sensor that fails a read reports its key with `null` and the sensor's `fault` flag, not a missing key.
- **Counters.** Counters are cumulative since boot and restart at 0 with a new `epoch`; a counter reset by command (`clear stats`) is reported as an event. A consumer works out rates from successive snapshots.
- **Limits.** Each stream has a minimum period, a maximum event rate and a bounded per-transport queue. When a queue is full the event is dropped and a drop counter in the next event of that stream says how many. A slow or vanished consumer costs dropped events, never time in the radio loop (DEVICE_INTERFACE section 8).
- **Access.** Each stream has the class it needs in the CLI privilege table: `read` for operational state, more for anything private. A subscription is decided like the CLI line its data comes from, with the caller's real context. Passwords, keys and secrets are never in any stream.

## 2. Streams

Status column: **built** is in the firmware; **next** is being built in this stage; **after trust** waits for the Deployment authority stage (its access rule needs a Deployment-signed subscriber); **unsupported** is what the hardware cannot provide.

| Stream | Content | Class | Default / minimum period | Status |
|---|---|---|---|---|
| `health` | uptime, heap and stack, battery, contacts | read | 60 s / 5 s | built |
| `runtime` | reset reason, abnormal reset, boot times, longest loop pass | read | 300 s / 60 s | built |
| `radio` | packet and error counters, noise floor, last RSSI and SNR, airtime, duty cycle | read | 60 s / 5 s | built |
| `connectivity` | Wi-Fi, MQTT, BLE and USB link state; changes as events | read | 60 s / 10 s, plus an event at each change | built for Wi-Fi and MQTT on Companion builds with a network (BLE and USB link state: next) |
| `power` | battery voltage, external power, charger, USB and solar detection | read | 300 s / 30 s | built |
| `environment` | readings of the attached sensors, with units and sensor identity | read | 300 s / 30 s | next |
| `location` | GNSS position, fix, satellites and age | location (private) | 300 s / 30 s | after trust |
| `observations` | each received packet: raw bytes, RSSI, SNR | read | event per packet, at most 10/s (no period) | built |
| `logs` | audit events and bounded diagnostics, with severity and category | posture | events | after trust |
| `inventory` | contacts, neighbours, channels: counts and entries | read for counts; private for entries | 3600 s / 300 s | counts next; entries after trust |
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

### `runtime` (built; `heap_low` and the boot event next)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `reset_reason` | text | `getResetReason()` | ESP32 and nRF52; `null` elsewhere. Includes panic, watchdog and brownout on ESP32 |
| `abnormal_reset` | bool | derived from `reset_reason` | true for panic, watchdog, brownout; `null` where the reason is unknown |
| `shutdown_reason` | text | nRF52 GPREGRET2 | nRF52 only |
| `boot_ms.store`, `.mesh`, `.setup` | ms | `BootTimes` | `null` for an unmarked stage |
| `firmware`, `upstream`, `hardware`, `role` | text | as `hello` | next: a boot event; today in `hello` only |
| `loop_gap_ms.max` | ms | the device interface's loop tick | the longest main-loop pass since the previous runtime event; reading it resets it. 0 before two passes |
| `heap_low` | bool | `HeapFloor` | Companion; the low-memory state the display shows |

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

### `connectivity` (built for Wi-Fi and MQTT)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `mode` | `ble`/`wifi` | connectivity mode | Companion builds with modes |
| `wifi.state`, `.profile`, `.ssid`, `.rssi_dbm`, `.ip`, `.up_s` | | Wi-Fi controller | SSID shown, password never; fields of a link that is not up are `null` |
| `wifi.last_fail` | text | Wi-Fi core | the reason the last join failed |
| `mqtt.state`, `.profile`, `.broker`, `.tls`, `.up_s` | | MQTT controller | `tls` is `plain`, `verified` or `unverified` |
| `mqtt.reconnects` | count | MQTT controller | sessions after the first since boot |
| `mqtt.browsing` | bool | MQTT controller | looking for a discovery broker |
| `ble.connected` | bool | BLE interface | Companion BLE builds |
| `usb.connected` | bool | serial interface | where the host link is detectable |

Events: one on every state change of Wi-Fi, MQTT or BLE, carrying the new state.

### `power` (built)
| Field | Unit | Source | Notes |
|---|---|---|---|
| `battery_mv` | mV | board | `null` when not measurable |
| `external` | bool | board | `null` where the board cannot tell |
| `charging` | bool | board | `null` where the board cannot tell |
| `usb_power`, `solar` | bool | board detection | absent where the board has no detection |
| `boot_mv` | mV | nRF52 | battery at boot |

No state of charge is reported: no board here measures it, and a percentage computed from voltage is not a measurement.

### `environment` (next)
One entry per attached sensor reading: `{"sensor": <type>, "quantity": <name>, "value", "unit", "age_s", "fault"}`. Quantities: temperature (°C), relative humidity (%), pressure (hPa), voltage (V), current (A), power (W), soil moisture (%), distance (mm), and the MCU temperature (°C) where the board reads it. Altitude derived from pressure is reported as derived. Sensors are detected at boot; one that is not fitted is absent.

### `observations` (built)
Each received packet: `{"raw": <hex>, "len", "rssi_dbm", "snr_db", "dropped"}`, before parsing, as the radio heard it. The payload is the mesh's own ciphertext; nothing is decrypted for it. At most 10 events per second per device; `dropped` counts the packets not sent since the previous event. `sub.start` takes no `period_s` for it (refused with `no-period`, reported as `null`). On the console, a packet that arrives while a request line is being typed counts as dropped.

### `location`, `logs`, `inventory` entries, `screen` (after trust)
- `location`: latitude, longitude (degrees), altitude (m), satellites, fix, `age_s`. Location is private: its class is decided with the Deployment authority stage.
- `logs`: the audit ring's events (posture changes, refusals, failed logins, resets, mode changes, low memory) and bounded diagnostics, each with severity, category and caller. A general log ring does not exist yet; its size and retention are set when it is built.
- `inventory` entries: contact and neighbour entries. Counts are `read`; entries are private.
- `screen`: a snapshot of the device's own framebuffer, `{"width","height","format":"mono1","on","age_s","data"}`, fragmented to the transport's size. The Heltec boards' SSD1306 (128x64, 1,024 bytes a frame) exposes its buffer; e-ink and TFT drivers are reported per board when built. The display is never woken or changed by a capture. A frame can show messages and the BLE pairing PIN, so its access rule is the strictest of any stream and is set with the Deployment authority stage.

## 4. Per board and role

Filled in as each stream is built and measured: implemented, unavailable on that board, or unsupported, for the Heltec V3 and V4 Companion standalone builds, the Heltec V3 Repeater and Room server, and the simulated boards.
