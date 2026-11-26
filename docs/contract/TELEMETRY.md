# Telemetry catalogue

This page is the one telemetry model. The device interface ([DEVICE_INTERFACE](DEVICE_INTERFACE.md)) carries it over USB, BLE and, on the Heltec boards, MQTT (Companion), and over the console and, on the Heltec boards, MQTT (Repeater, Room server). Every transport gives a stream the same meaning: a transport may limit sizes and rates, never change what a field means. Which streams a device produces depends on its build and board: `hello` lists `stream.<name>` for each one, and `sub.start` on any other answers `invalid` with `unknown-stream`.

## 1. Model

- **Streams.** Telemetry comes in named streams, one per family. A consumer starts one with `sub.start {"stream": <name>, "period_s", "lifetime_s"}` and stops it with `sub.stop`. A stream sends a snapshot at its period and, where the family has them, events when something changes. A subscription ends at its lifetime unless renewed, so a vanished consumer costs nothing.
- **Discovery.** `hello` lists `stream.<name>` in `capabilities` for every stream the build and board can produce. A stream the device cannot produce is absent from the list and `sub.start` answers `invalid` with `unknown-stream`. A device without a sensor, a screen or a battery reading omits that capability. It never sends zeros in its place.
- **Envelope.** Every event is `{"v":1,"ev":<stream>,"seq","epoch","ts","data"}` (DEVICE_INTERFACE section 3). `seq` counts per stream per device, so a gap on one transport is an event dropped there; `epoch` is the boot counter, so `(epoch, seq)` orders events across reboots and a Backend that receives one event over two paths drops the second copy.
- **Absent, null, zero, stale.** Absent: this build or board does not have the field. `null`: the field exists but is unknown right now (a noise floor not yet calibrated, a clock not yet set). `0`: measured zero. Where a value can go stale (a GPS fix, a sensor reading), the event carries its age in seconds beside it. A sensor that fails a read reports its key with `null`, not a missing key.
- **Counters.** The new `radio` counters (`rx.dup`, `rx.dropped`, `rx.unparsable`, `tx.failed`, `queue_full_count`, `cad_busy_retries`, `duty_holds`) are plain 32-bit integers kept whether or not anyone subscribes; they wrap at 2^32 and `clear stats` zeroes them with the rest. Counters are cumulative since boot and restart at 0 with a new `epoch`; a counter reset by command (`clear stats`) is reported as an event. A consumer works out rates from successive snapshots.
- **Limits.** Each stream has a minimum period, a maximum event rate and a bounded per-transport queue. An event over a stream's rate is dropped, and a drop counter in the next event of that stream says how many. An event that finds its transport's queue full is dropped too, leaving a gap in `seq` and `usig_seq` on that transport. A Companion counts these in `status.get` `events_dropped`. *(Since v0.3.6.)* A slow or vanished consumer costs dropped events, never time in the radio loop (DEVICE_INTERFACE section 8). On an nRF52 Companion's USB cable, a frame that finds no room for 20 ms is dropped whole and counted in `status.get` `cable_dropped`. *(Since v0.3.6.)* On a Companion, no event is built while a request is part-way through arriving in frames on any link: building and signing one holds the loop off the port, which on a Heltec V3's UART is long enough to lose part of the request. An event that falls due then goes out once the request is whole, or once the reassembly timeout drops it (5 s at most).
- **Access.** Each stream has the class it needs in the CLI privilege table: `read` for operational state, more for anything private. A subscription is decided like the CLI line its data comes from, with the caller's real context, when `sub.start` arrives: a caller whose class does not allow the stream gets `refused`. `logs` is class `posture`, like `get audit`: the cable and a BLE link paired with a per-device PIN may subscribe, and so may a subscriber whose `sub.start` a Deployment member signed (a design decision of 2026-11-01), over a link that may itself carry private data: a TLS-verified broker session, not an unverified one. The signed request is the ordinary signed envelope ([AUTHORITY](AUTHORITY.md) section 3.3), and the events go to the device's usual `ev/logs` topic. An unsigned broker session, a remote admin and a BLE link on the default PIN may not. The private streams (`location`, inventory entries, `screen`) are class `private`, by a design rule of 2026-10-28 (private data goes via the broker to the backend): a TLS-verified broker session may subscribe, as may the cable, a BLE link with the device's own PIN and a signed request. A LoRa caller, the Ethernet console, an unverified broker session and the untrusted links may not ([CLI](../CLI.md)). For every stream beyond `read`, the link that carries it must also be allowed it by itself: a signed `sub.start` on the open TCP port or a default-PIN BLE link sends neither logs nor private data over that link. A link's subscriptions end when it disconnects, with anything still queued for it, so the next client on that link inherits nothing. That needs a link that can tell: BLE and the TCP port do; a USB port behind a UART bridge cannot, so a cable's subscription lasts until its lifetime (at most 600 s), and the cable is trusted by possession. Passwords, keys and secrets are never in any stream.

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
| `observations` | each received packet: raw bytes, fingerprint, RSSI, SNR | read | event per packet, at most 10/s (no period) |
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
| `heap.min_free` | bytes | `memStats()` | ESP32, nRF52 | lowest since boot; `null` where untracked. ESP32: the heap allocator's own low-water. nRF52: the least `heap.free` seen at a look (once a second, and at the end of each named piece of work), on the same heap, so a peak allocated and freed between two looks is missed |
| `heap.loop_stack_free` | bytes | `memStats()` | ESP32, nRF52 | least free stack of the loop task since it started (on nRF52 the task's FreeRTOS high-water mark); `null` elsewhere |
| `heap.worker_stack_free` | bytes | `NrfWorker::workerStackFree()` | nRF52, every role | least free stack of the interface worker task, which runs every request on nRF52, since it started (FreeRTOS high-water mark); absent elsewhere, and while no worker exists |
| `heap.psram_free` | bytes | `memStats()` | boards with PSRAM | absent without |
| `battery_mv` | mV | `getBattMilliVolts()` | all | `null` when the board cannot measure it |
| `battery_level` | text | `BatteryLevel` | all | *(since v0.3.12.)* `ok`, `low`, `critical` or `unknown`: the level of the mean of the valid battery readings of 60 s (a reading every 5 s). It is the observed voltage level of a single Li-ion or LiPo cell, not a state of charge. `low` is a mean below 3550 mV and `critical` below 3400 mV; a level goes down as soon as a window's mean is below its threshold and comes back up only 100 mV above the threshold it crossed (3650 mV to leave `low` or `critical` for `ok`, 3500 mV to leave `critical` for `low`). A dip shorter than the window, such as the sag while the radio transmits, does not move it. `unknown` is a value, not `ok`: it is reported before the first window is full (the first 60 s after boot), while the board cannot measure, and when fewer than 8 of the 12 readings of a window are valid (a reading of 0 or outside 2000 to 4600 mV is not a reading). A missing or invalid reading is never `critical`. A USB-powered board with no cell can read the supply, not a cell: the level is of the voltage read. The level only reports: nothing sleeps, shuts down or changes the radio because of it. The thresholds are 3550 and 3400 mV with a 100 mV margin unless the `battery.guard` setting changes them (since v0.3.12; [OPERATIONS](OPERATIONS.md)) |
| `battery_level_method` | text | firmware | all | *(since v0.3.12.)* How `battery_level` was derived: `mv_mean_60s` |
| `battery_level_changes` | count | `BatteryLevel` | all | *(since v0.3.12.)* Times `battery_level` changed since boot, the first change after boot (from `unknown` to a level) and every change to or from `unknown` included. A 16-bit counter that wraps at 65536 and restarts at 0 with a new `epoch`; `0` is a measured zero. A level that flaps shows as a count that keeps rising |
| `power_source` | text | board | all | *(since v0.3.12.)* `external` or `battery` on a board that senses USB power (the Wio Tracker L1: the nRF52 USB regulator's VBUS flag, so `battery` means no USB power, not that a cell is fitted or that no other supply is connected), otherwise `unknown`. The Heltec V3 and V4 have no way to sense the supply, so they report `unknown`: a voltage is never taken for a source, and a USB-powered V3 with no cell, reading the rail at about 4.2 V, reports `ok` and `unknown`. The T1000-E reports `unknown` here too: its power-detect pins feed the `power` stream's `external`, but their polarity has not been confirmed on a board, so `power_source` does not rely on them |
| `battery_guard` | object | `battery.guard` setting | all | *(Since v0.3.12.)* `{enabled, supported, effective}`: `enabled` is the stored setting, `supported` whether this board and role have the guard (the Heltec V3 and V4 voltage only, the Wio Tracker L1 and the T1000-E, as a Companion or a Repeater; `false` on the Room server and the Sensor, and on any other board), and `effective` is `report-only` in this release: **nothing sleeps or shuts down, whatever `enabled` says.** In the health stream only, not in `status.get`, whose whole answer must fit one signed reply over LoRa |
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
| `heap_low` | bool | `HeapFloor` | Companion, Repeater and Room server; the low-memory state (on the Companion, the one its display shows). Absent where the build does not check the floor: it is enforced on ESP32 only, because nRF52 has no largest-block figure (`heap_lows` is still sent there) |
| `heap_lows` | object | `HeapWatch` | Beside `heap_low`, wherever it is present, and on nRF52 without it: `{"largest","min_free"[,"loop_stack_free"]}`, each `{"bytes","by","at_s"}` or `null` before the first look. `bytes` is the lowest largest free block, or the lowest free heap, since boot. `by` is what was running then: an operation's name, `request` (the envelope, signature and signed result around it), `stream.<name>`, `mqtt.connect`, `mqtt.request`, `mqtt.publish`, or `idle` (outside all of these). `at_s` is seconds since boot. It says what took the heap down, so a `heap_low` can be traced to its cause. `loop_stack_free` (nRF52 only, once seen) is the same triple for the loop task's least free stack: `bytes` is that least free stack and `by` is the work running since the stack's high-water mark was last looked at (a scope's start or end, or the once-a-second look), `idle` when it fell between scopes. |
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
| `rx.dup` | packets | Mesh | *(Since v0.3.9.)* Packets whose hash had been seen before, at each point where a packet is checked against the seen table. Counts hearings of the same packet by any route, so it says nothing about who sent it |
| `rx.dropped` | packets | Mesh | *(Since v0.3.9.)* Direct packets that reached this node but name another node as the next hop (or are not to be forwarded by it): the one drop the routing code decides by rule. A flood a role merely does not forward is not counted. No reason code per packet |
| `rx.unparsable` | frames | Dispatcher | *(Since v0.3.9.)* Frames received that were not a valid packet: an unsupported version, the reserved path mode, a path or payload that does not fit. They still reach `observations` as heard |
| `tx.failed` | packets | Dispatcher | *(Since v0.3.9.)* Packets dropped unsent: the radio refused or timed out the send, or the packet alone needs more airtime than the duty limit allows |
| `queue_full`, `cad_timeout`, `startrx_timeout` | bool | Dispatcher error flags | set since the last `clear stats` |
| `queue_full_count` | count | Dispatcher | *(Since v0.3.9.)* Times no packet was free, to receive into or to build; the flag above says only that it happened |
| `cad_busy_retries` | count | Dispatcher | *(Since v0.3.9.)* Sends put off for 200 ms because the channel was busy (one count per retry; a send forced through after 4 s of busy is the `cad_timeout` flag) |
| `duty_holds` | count | Dispatcher | *(Since v0.3.9.)* Sends in the radio queue put back to wait for airtime under the duty limit, counted at each hold. A boot starts the window at the limit, so the first sends after a boot are held. A `packet.transmit` waiting for its budget is not counted here: it waits before the radio queue, and its job reports `duty_held` |
| `queue.depth`, `queue.free` | packets | packet pool | *(Since v0.3.9.)* Packets waiting to send (including those waiting for their time) and unused packets in the pool. Their sum is the pool size, a fixed number per role. A reading at the moment of the event, not a history |
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
| `usb_power`, `solar` | bool | board detection | absent where the board has no detection. *(`usb_power` on the Wio Tracker L1 since v0.3.12: the USB regulator's VBUS flag.)* |
| `boot_mv` | mV | nRF52 | battery at boot |

No state of charge is reported: no board here measures it, and a percentage computed from voltage is not a measurement.

### `environment`
`readings`: one entry per attached sensor reading, `{"channel", "quantity", "value", "unit"}`, decoded from the Cayenne LPP the sensor manager already produces for the mesh's own telemetry (environment permission only, never location). Quantities and units: temperature (C), humidity (%), pressure (hPa), voltage (V), current (A), power (W), percentage (%), altitude (m), distance (m), luminosity (lx), energy (kWh). `mcu_temp_c` is present where the board reads its MCU temperature. Sensors are detected at boot; one that is not fitted has no entry, and with nothing to report the stream is absent from `hello`, rechecked once a minute. Not yet: a sensor's age and fault flag (the sensor managers do not report either).

### `observations`
Each received packet: `{"raw": <hex>, "len", "type", "route", "path_len", "fp", "freq_mhz", "bw_khz", "sf", "cr", "outcome", "rssi_dbm", "snr_db", "dropped"}`, as the radio heard it. *(`type`, `route` and `path_len` since v0.3.9.)* They are read from the frame's own bytes, with no packet parsing: `type` is the payload type (0 to 15), `route` is `flood`, `direct`, `tflood` or `tdirect` (`t`: with transport codes), and `path_len` is the number of hops recorded in the path, or `null` when the frame ends before its path-length byte or uses the reserved path mode. They say what the frame claims, not that it is valid (a frame counted in `rx.unparsable` still carries them), and not who sent it or which node last transmitted it: a path is a list of hashes of relays, and a relayed reception is not evidence of a direct neighbour. *(`fp` since v0.3.10.)* `fp` is the packet's fingerprint: 16 hex characters, the first 8 bytes of the packet hash the mesh itself uses to recognise a packet it has seen, taken from the payload type and the payload (and, for a trace, the path-length field as heard, two bytes little-endian). It is the same for one packet whatever path it took, so a consumer can match the receptions of one packet at several devices; it is not a secret and not a signature, and two different packets can in principle share one. It is absent when the frame is not a valid packet (those counted in `rx.unparsable`) and when no packet buffer was free to read it into. It is class `read`, like the raw bytes it is computed from. *(`freq_mhz`, `bw_khz`, `sf` and `cr` since v0.3.10.)* They are the radio settings in force when the packet was handled: the frequency in MHz and the bandwidth in kHz as last applied to the radio, and the spreading factor (5 to 12) and coding rate (the denominator of 4/5 to 4/8, so 5 to 8) the firmware last applied with them, a temporary radio change included. They are settings, not measurements: the radio drivers report none of the four per frame, and a frame is heard with whatever the radio was set to. They are absent before the radio has been configured at boot. *(`outcome` since v0.3.10.)* `outcome` says what handling the packet came to, where the code that decided it knows, and is `null` where it does not. It is one of:
- `forwarded`: queued to be sent on (a flood packet repeated, or a direct packet passed to its next hop). It says the packet was queued, not that it went out: a send can still wait for airtime or fail (`radio` `tx`).
- `duplicate`: the packet's hash had already been seen, so it was not handled again (the same count as `rx.dup`).
- `not-next-hop`: a direct packet whose next hop is another node (counted in `rx.dropped`).
- `filtered`: not forwarded because this node's rules do not allow it: the role does not repeat (a Companion without repeating, a Repeater with repeating off), a hop limit or a region rule says no, or it is a direct packet in this node's turn that may not be forwarded. A node that does not repeat reports every flood packet it hears that is not a duplicate as `filtered`, whether or not the packet was also delivered to it.
- `no-route`: a flood packet whose path has no room left for this node's hash.
- `unparsable`: the frame is not a valid packet (counted in `rx.unparsable`).

`null` is any other result: the packet was delivered here or ignored for another reason, and a flood packet that the receive delay holds back is `null` too, because it is observed when it is heard and handled later. The first reason found is the one reported (a duplicate that is also not for this node's hop is a `duplicate`). The tag adds nothing to the airtime and changes nothing about what is forwarded. An observation is sent once the packet has been handled, so a packet this device forwards is reported with its path as heard, not as forwarded. The payload is the mesh's own ciphertext; nothing is decrypted for it. At most 10 events per second per device; `dropped` counts the packets not sent since the previous event. `sub.start` takes no `period_s` for it (refused with `no-period`, reported as `null`). On any stream, a `period_s` outside its bounds answers `invalid` `bad-period`, and a `lifetime_s` outside 10..600 answers `bad-lifetime`. On the console, a packet that arrives while a request line is being typed counts as dropped.

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

## 4. Observation catalogue: what is present, missing, unsupported and deferred

What the firmware observes about received packets, packet handling, the radio and its queue, against what a Backend may want. The firmware records facts; correlation, topology and any judgement of health belong to the consumer. Nothing here is kept per packet unless an authorised `observations` subscription is active, and nothing in this list adds airtime.

| Family | Item | State | Where / why |
|---|---|---|---|
| Per-packet receive | raw bytes, length, RSSI, SNR | present | `observations`, class `read`, bytes as heard and never decrypted |
| | observation identity | present | the event's `epoch` and `seq`, per device and stream; `dropped` and gaps in `seq` show what was missed |
| | timestamp and clock confidence | present | the envelope `ts`, `null` while the clock is not set |
| | payload type, route, hop count | present | `type`, `route`, `path_len` *(Since v0.3.9)* |
| | fingerprint (packet hash) | present | `fp` *(Since v0.3.10)*, class `read`, absent for a frame that is not a valid packet |
| | frequency, bandwidth, spreading factor, coding rate of the reception | present | `freq_mhz`, `bw_khz`, `sf`, `cr` *(Since v0.3.10)*: the settings last applied to the radio, not measured per frame |
| | origin and last transmitter | unsupported | the on-air format carries relay hashes, never the identity of the node that transmitted last; the firmware does not guess one |
| Handling outcomes | per-packet outcome (forwarded, duplicate, not-next-hop, filtered, no-route, unparsable) | present | `outcome` *(Since v0.3.10)*; `null` where the code does not know; the counters below remain |
| | duplicates, drops, unparsable frames | present | `radio` `rx.dup`, `rx.dropped`, `rx.unparsable` *(Since v0.3.9)* |
| Radio statistics | received and sent counts, errors, noise floor, airtime, duty use | present | `radio` |
| | failed sends, busy-channel retries, duty holds, pool exhaustion | present | `radio` `tx.failed`, `cad_busy_retries`, `duty_holds`, `queue_full_count` *(Since v0.3.9)* |
| | queue depth and free packets | present | `radio` `queue` *(Since v0.3.9)* |
| | queue age and pressure over time | deferred | needs a timestamp per queued packet; a Backend can derive pressure from successive `queue` readings |
| | receiver reset and fault state | unsupported | the radio drivers report no such state, and the firmware does not infer one |
| Neighbour evidence | per-neighbour sample counts and RSSI/SNR aggregates | unsupported | the only identifier on the air is a relay hash, which does not identify a direct neighbour; the Repeater's `inventory` `neighbours` is a count |
| Device context | reset epochs, reset reason, processing errors, link state changes | present | `runtime`, `connectivity`, `logs` |
| LoRa | compact counters in a report | present | the `counters` field of the compact report *(Since v0.3.10)*, only when asked for by name; the `health`, `radio` and `location` fields are as before |

*Deferred* means feasible and not built; *unsupported* means the firmware cannot observe it truthfully on this hardware or protocol.

## Compact reports over LoRa

A small binary subset of the producers above, for the radio, with the same meanings and units. `report.now` sends one, and a [standing instruction](OPERATIONS.md) sends them on a schedule. Nothing else goes over LoRa as telemetry: no subscription, no log, no screen.

**Layout, little-endian:** `0x1E`, version (1), fields, boot u32, seq u32, time u32 (0: the device does not know the time), the fields in bit order, then a 64-byte signature. `tools/mecon_report.py` decodes one.

| Bit | Field | Bytes | Contents |
|---|---|---|---|
| 1 | `health` | 8 | uptime_s u32; heap free in KB u16; battery mV u16 |
| 2 | `radio` | 12 | rx u32; tx u32; error flags u8; noise floor dBm i8; last RSSI dBm i8; last SNR in quarters of a dB i8 |
| 4 | `location` | 14 | fix u8; lat_e6 i32; lon_e6 i32; alt_m i16; sats u8; age_s u16 |
| 8 | `counters` | 16 | *(Since v0.3.10.)* rx duplicates u16; rx dropped u16; rx unparsable u16; tx failed u16; queue-full count u16; busy-channel retries u16; duty holds u16; queue depth u8; free packets u8 |

- **`counters`.** The same counters as the `radio` stream's `rx.dup`, `rx.dropped`, `rx.unparsable`, `tx.failed`, `queue_full_count`, `cad_busy_retries` and `duty_holds`, and its `queue` `depth` and `free`, with the same meanings. Each counter is the low 16 bits of the device's 32-bit count (since boot or the last `clear stats`), so a count of 65,536 reads as 0 and the difference between two reports is taken modulo 65,536; a counter has no unknown value, so 0xFFFF is a count. The two queue figures saturate at 255. Every board that sends reports produces the field. **It is sent only when a request names it** (`report.now` or a standing instruction with `"counters"` among its `fields`): a report asked for with `health`, `radio` and `location` alone is byte for byte what it was, so a reader that does not know bit 8 is not affected unless it asks for it. A reader that meets the bit without knowing it cannot find the signature, because the field has no length marker: it must treat the report as one it cannot read.
- **Unknown is not zero.** 0xFFFF (u16) and 0x7F (i8) mean unknown. A board that cannot produce a field answers `unsupported-field` when it is asked for; it never sends a made-up value.
- **Provenance.** The signature is the node key's, over `MECON-UPLINK-v1` 0x00 `report` 0x00 and the bytes before it, using the same boot and sequence as every event (AUTHORITY section 3.4), so a repeated or reordered report shows. A boot that cannot sign sends none. A full report (all four fields) is at most 129 bytes; without `counters` at most 113.
- **Where it goes.** To a node key as an anonymous datagram, or on a channel. A Repeater or Room server floods the datagram and uses its bound management channel. A Companion sends the datagram by the contact's path, or directly to a neighbour when the node is not a contact, and `slot` must name its bound management channel, never a public one. On a channel the frame carries the sender's 8-byte key prefix and, if named, the target's. A Companion that hears one puts it on its `rf` stream as `{"from","report"}` (base64), for the manager to verify.
- **Private.** `location` needs the right to read it, and never goes on a channel, where every member could read it.
- **Limits.** `report.now` at most once in 30 s (`busy`, `rate-limited`). Every report goes through the duty window like any other transmission.

**On the air.** A 99-byte report (`health` and `radio`) travels as a 149-byte anonymous datagram. At the EU Narrow preset (SF8, BW 62.5 kHz, CR 4/8, 32-symbol preamble) that is about 1.43 s, and a full 113-byte report (a 165-byte packet) about 1.56 s, and one with `counters` too (129 bytes, a 181-byte packet) about 1.7 s (computed from the same figures, not measured). One report every 900 s takes about 1.6% of a 10% band's hourly budget. Measured on a Wio Tracker L1 and a SenseCAP T1000-E, 2026-11-15.


### Reports kept for later

> **Status: built, on main and in no release yet.** Version 0 (2026-11-23), agreed with the reference Backend's coder.

A report sent while no receiver listens is lost today. A standing instruction may ask the device to keep what it sends,
so a manager can collect it later over any link.

- **Opt in, per instruction.** `standing.put` takes `"keep": true`. Only reports from such an instruction are kept; a
  `report.now` is never kept, since a manager asking for it is there to hear it. A device cannot know whether a
  datagram was heard (it carries no acknowledgement), so every report of a keeping instruction is kept, heard or not.
- **What is kept.** The report's exact bytes, signature included, as they were sent. Nothing is re-signed or rebuilt,
  so a kept report reads exactly like the live one: its boot, seq and time date it. A report from an earlier boot
  carries that boot's number, and its time is 0 if the device did not know the time then.
- **What a receiver does with one.** It drops a report only when it already holds one with the exact same (node key,
  boot, seq). It never refuses a report for being older than one it holds: a kept report is older than the live ones by
  design, so a high-water-mark check would refuse every one of them.
- **Where, and how many.** A ring in flash of the last 64 reports (at most 129 bytes each, about 8.5 KB), kept across a
  reboot together with its cursor counter, so a cursor is never reused after a reboot. The oldest goes first. One write
  per report: at one report every 900 s, 96 writes a day. A board whose flash cannot hold the ring answers `keep` with
  `unsupported`. The size is fixed in this version.
  *How it is built.* The cursors are cut into segments of 16, each its own file that is only ever appended to, five at most
  (about 11 KB). A report is one append. When a sixth segment starts, the oldest file, wholly outside the last 64, is removed:
  nothing is copied to make room and no file is rewritten for a report. Each record carries its CRC; a record a power cut left
  half written is not a report, and the file is rewritten without it. A new segment is not started with less than 6 KB of the
  file system free, so the reports cannot starve the settings and contacts of room; a report that cannot be written is counted
  in `dropped`. A small meta file, replaced atomically only at a `report.ack`, holds the next cursor and the acknowledged mark.
  *Flash cost.* On ESP32 and every other board, a kept report is one append: 96 appends a day at one report every 900 s. On the
  nRF52 the InternalFS costs about 5 page erases per save, mostly on one directory page that Nordic rates at 10,000 erases at the
  least. One append per report would be about 480 erases a day on that page (the rated minimum in about three weeks). So on
  nRF52 only, reports wait in RAM, up to 8, and go in one write when 8 wait, when an hour has passed since the oldest waiting,
  before any `report.list` or `report.ack` answers (a list always shows them) and before a reboot or power-off. At a 900 s period
  the hour comes first, so a write holds 4 reports: about 24 writes a day, about 120 erases a day (the rated minimum in about
  three months); at a period of 450 s or less a write holds 8. These are estimates from the measured 5 erases a save, not
  measured on a board. **On nRF52, up to 8 kept reports or 1 hour of them may be lost on a power cut** (the cursors they took are
  then given out again, and nothing counts them in `dropped`, since nothing on the device can tell).
  On nRF52 a kept instruction wears one flash page. At a 900 s period that page reaches the chip's rated minimum (10,000 erases, an estimate from the measured 5 erases per save) in about three months. At 3,600 s, about a year. Use the longest period the purpose allows, and prefer an ESP32 board for long-running kept instructions. The rating is a minimum, not a failure point.
- **Collecting.** `report.list {"after": <cursor>, "limit": n}` (`limit` 1 to 8, 8 by default) answers
  `{"reports": [{"cursor", "boot", "seq", "report": <base64>}], "more", "dropped", "withheld"}`, oldest first, paged
  like `inbox.list` (a page stops where the answer would not fit its link: 1,100 bytes of result, 760 over LoRa; the next
  page starts after the last `cursor` listed). `report.ack {"upto": <cursor>}` releases everything up to that cursor and
  answers `{"acked", "released", "remaining"}`; a cursor the device has not given is `failed`, `unknown-cursor`, and an ack of a
  cursor already released changes nothing. Any number of callers may read; the acknowledgement belongs to the manager that set the
  keeping instruction (the member that signed it on an enrolled device, the cable on one without a record), as
  `inbox.ack` belongs to one reader. Nothing is ever re-sent
  unasked: a device spends no airtime on kept reports unless a manager asks for a page.
- **Gaps.** `dropped` counts reports pushed out of a full ring since the last `report.ack`, so a manager sees how many it
  missed, and the reports that could not be written. Cursors only grow, so a gap also shows in them.
- **Authority.** `report.list` is a read. A kept report carrying `location` is listed only to a caller allowed to read
  the location (`location`, section 3). It is never cut out of a report, because the signature covers it:
  it is left out of the page and counted in `withheld` (every such report above `after`, not only those on the page).
  `report.ack` is a write, and it never releases a report its caller could not list: an ack whose range holds a withheld
  report is `refused` with the error `location-withheld`, and releases nothing. An ack whose range holds a report made by
  another member's instruction is `refused` with `not-owner`, and releases nothing.
- **Links.** USB, BLE, a broker, and LoRa point to point. Not on a LoRa channel, where every member would read it: there both operations answer `unknown-op`. Over
  LoRa a page holds what fits one 1,024-byte message: about four kept reports.

**Settled with the reference Backend's coder (review of 2026-11-23):** the ring is 64 reports, fixed in this version;
`keep` is not offered on `report.now`; a report collected later carries no marker, since the receiver records how it
got each report.
