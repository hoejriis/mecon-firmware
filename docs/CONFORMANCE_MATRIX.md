# Conformance matrix: board, role, transport, capability

This page lays [hardware evidence](HARDWARE_VERIFIED.md) out by board and role, so you can see at a glance what has run on which of the four test boards and what has not. HARDWARE_VERIFIED is the record: where this page and that one disagree, that one wins, and this page is the bug. The roles and transports come from [supported hardware](SUPPORTED_HARDWARE.md), [availability](AVAILABILITY.md), [operations](contract/OPERATIONS.md) and the [device interface](contract/DEVICE_INTERFACE.md).

## How to read a cell

| Cell | Meaning |
|---|---|
| `✓ 2026-11-08 a32` | Run on this board, in this role, on real hardware. The date is the row's date in HARDWARE_VERIFIED. The code points to the row in the evidence key at the end. A `*` after the code means the row's build was a test build, not a release. |
| `sim` | Passes in the simulated-board suite only, as HARDWARE_VERIFIED's "Not yet verified on hardware" list says. Not hardware evidence. A cell is `sim` only where that list names the capability. Any other capability with no hardware row is `—`. |
| `fam` | Covered by the chip family, not a pass on this board: the other board of the same family (ESP32: Heltec V3 and V4; nRF52: Wio Tracker L1 and T1000-E) has a `✓` in this role, and the two run the same code path. Conformance counts a capability once per chip family and role. Rows for board-specific hardware (BLE, the screen, the battery, serial DFU) never take this mark: each board that has the hardware needs its own row. |
| `n/a` | The board or role does not have it. The reason is under the tables. |
| `—` | Nothing recorded on this board and role. This is not a pass. |

A `✓` is never taken from a simulator, from another board or from another role. Where a row names two boards or roles, each gets its own mark only if the row names it. A `✓` shows that the named check ran; the row in HARDWARE_VERIFIED says what it covered and what it left out. A build pass proves compilation only.

Rows are grouped by capability. Boards are the Heltec V3, Heltec V4, Seeed Wio Tracker L1 and SenseCAP T1000-E. Roles are Companion, Repeater and Room server, each in its own table. Companion cells mean the standalone build on the Heltec boards and the USB or BLE build on the nRF52 boards.

## Companion

| Capability | Heltec V3 | Heltec V4 | Wio L1 | T1000-E |
|---|---|---|---|---|
| **Identity and enrolment** | | | | |
| First provisioning | ✓ 2026-11-08 a32 | ✓ 2026-10-28 b20 | ✓ 2026-11-05 m7 | ✓ 2026-11-06 d1 |
| Signed request accepted | ✓ 2026-11-06 a46 | ✓ 2026-10-28 b20 | ✓ 2026-11-05 m7 | ✓ 2026-11-06 d1 |
| Unsigned request refused, enrolled | ✓ 2026-11-12 a55 | ✓ 2026-10-28 b20 | ✓ 2026-11-05 m7 | ✓ 2026-11-06 d1 |
| Replay refused, stored result returned | fam | ✓ 2026-10-28 b20 | fam | ✓ 2026-11-23 d24 |
| Signed `deployment.reset` | fam | ✓ 2026-10-28 b20 | ✓ 2026-11-05 m7 | ✓ 2026-11-06 d1 |
| Signed `identity.rotate` | ✓ 2026-11-06 a46 | ✓ 2026-11-06 b28 | fam | ✓ 2026-11-23 d25 |
| Management-channel slot bind, rotate, unbind | fam | ✓ 2026-11-23 b52 | fam | ✓ 2026-11-06 d1 |
| Hardened posture, locked reads | ✓ 2026-10-23 a10 | ✓ 2026-11-07 b39 | fam | ✓ 2026-11-24 d29 |
| Corrupt Deployment record refused | fam | ✓ 2026-10-28 b21 | fam | ✓ 2026-10-31 d5 |
| **Device interface by transport** | | | | |
| USB cable, request fixtures | ✓ 2026-10-24 a23 | ✓ 2026-10-24 b17 | ✓ 2026-11-06 c5 | ✓ 2026-11-06 d6 |
| BLE, request fixtures | ✓ 2026-10-24 a23 | ✓ 2026-10-24 b17 | — | ✓ 2026-11-08 d4 |
| MQTT over TLS broker | ✓ 2026-11-05 m8 | ✓ 2026-10-24 b17 | n/a | n/a |
| LoRa point to point, Companion as target | — | — | fam | ✓ 2026-11-24 d27 |
| LoRa management channel, Companion as target | — | — | fam | ✓ 2026-11-24 d28 |
| BLE PIN pairing, default-PIN refusals | ✓ 2026-10-24 a18 | ✓ 2026-10-21 b6 | — | ✓ 2026-11-08 d4 |
| Wi-Fi join, profile priority, failover | ✓ 2026-10-24 a19 | ✓ 2026-10-23 b9 | n/a | n/a |
| Connectivity mode (BLE or Wi-Fi) | ✓ 2026-10-24 a11 | ✓ 2026-10-21 b7 | n/a | n/a |
| **Telemetry streams** | | | | |
| Paged inventory stream over USB | ✓ 2026-10-28 a38 | fam | fam | ✓ 2026-11-23 d20 |
| Signed `logs` stream over MQTT | ✓ 2026-11-04 a24 | fam | n/a | n/a |
| Screen stream (frames, PIN withheld) | ✓ 2026-11-06 a41 | ✓ 2026-11-12 a42 | — | n/a |
| Battery, LoRa counters in `status.get` | — | ✓ 2026-11-23 b51 | — | ✓ 2026-11-08 d4 |
| **Compact reports and standing instructions over LoRa** | | | | |
| Compact report over LoRa, Companion as target | — | — | ✓ 2026-11-15 m25 | fam |
| Standing instruction over LoRa, Companion as target | — | — | ✓ 2026-11-15 m25 | fam |
| **Signed updates, by transport** | | | | |
| USB serial or DFU, signed | ✓ 2026-11-11 a53 | ✓ 2026-11-08 b37 | ✓ 2026-11-08 c8 | ✓ 2026-11-11 d13 |
| Bad key and bad signature refused | ✓ 2026-11-11 a53 | ✓ 2026-11-08 b37 | fam | ✓ 2026-11-11 d13 |
| BLE, signed | ✓ 2026-11-08 a32 | ✓ 2026-11-06 b30 | n/a | n/a |
| Broker push, signed | ✓ 2026-11-06 a39 | fam | n/a | n/a |
| HTTPS download, signed | fam | ✓ 2026-11-05 m10 | n/a | n/a |
| **Native messaging operations** | | | | |
| `message.send` | ✓ 2026-11-04 a25 | fam | fam | ✓ 2026-11-23 d23 |
| `inbox.list`, `inbox.ack` | ✓ 2026-11-05 a26 | fam | fam | ✓ 2026-11-24 d26 |
| `message.deliver` | ✓ 2026-11-05 m2 | ✓ 2026-11-14 m20 | — | — |
| Contacts: capacity and storage | ✓ 2026-10-22 a2 | fam | ✓ 2026-10-21 c1 | ✓ 2026-10-31 d2 |
| Contact reads: `contact.capacity`, `audit.list` | ✓ 2026-11-12 a55 | fam | ✓ 2026-11-12 c10 | ✓ 2026-11-12 d14 |
| DM over LoRa, ACK confirmed | ✓ 2026-10-23 a14 | ✓ 2026-11-14 m19 | fam | ✓ 2026-11-14 m19 |
| `rf.request` | — | — | ✓ 2026-11-09 c9 | fam |
| **Server-role settings and actions, `packet.transmit`** | | | | |
| Neighbour, `packet.transmit`, `room.post`, server actions | n/a | n/a | n/a | n/a |
| **Continuity package, recovery, measurements** | | | | |
| `deployment.continuity.export` | fam | ✓ 2026-11-23 b53 | fam | ✓ 2026-11-18 d17 |
| `device.reset` scopes | fam | ✓ 2026-11-23 b54 | fam | ✓ 2026-11-23 d22 |
| `ble.bonds`, `ble.pin.get`, key export, `device.reset` bonds | sim | sim | sim | sim |
| Signed `posture.set` over a broker or LoRa | sim | sim | sim | sim |
| Factory reset by rescue-console `erase` | ✓ 2026-10-24 a13 | fam | — | — |
| Upgrade keeps identity and contacts | ✓ 2026-10-24 a15 | ✓ 2026-10-24 b10 | ✓ 2026-11-06 c6 | ✓ 2026-11-06 m14 |
| Clock unset until a real source sets it | ✓ 2026-11-05 m3 | fam | ✓ 2026-11-05 m4 | fam |
| `clock.back` (cable) | ✓ 2026-11-05 a27 | fam | fam | ✓ 2026-11-23 d19 |
| Heap and loop stack measured | ✓ 2026-10-24 a22 | ✓ 2026-10-24 b16 | ✓ 2026-11-09 c9 | ✓ 2026-11-01 d3 |
| Cable host that stops reading | fam | ✓ 2026-11-14 m21 | ✓ 2026-11-14 c14 | ✓ 2026-11-14 d15 |
| Large requests over the cable | ✓ 2026-11-13 a59 | fam | fam | ✓ 2026-11-23 d21 |
| Duty-cycle floor by band | ✓ 2026-11-05 m1 | fam | fam | ✓ 2026-11-23 d18 |

## Repeater

| Capability | Heltec V3 | Heltec V4 | Wio L1 | T1000-E |
|---|---|---|---|---|
| **Identity and enrolment** | | | | |
| First provisioning | fam | ✓ 2026-11-08 m16 | ✓ 2026-11-14 c15 | ✓ 2026-11-06 d10 |
| Signed request accepted | ✓ 2026-11-05 m11 | ✓ 2026-11-05 m11 | ✓ 2026-11-14 c15 | ✓ 2026-11-14 d16 |
| Unsigned request refused | ✓ 2026-11-05 m12 | ✓ 2026-11-12 b44 | ✓ 2026-11-14 c15 | ✓ 2026-11-14 d16 |
| Signed `deployment.reset` | fam | ✓ 2026-11-23 b57 | ✓ 2026-10-29 c2* | ✓ 2026-11-06 d10 |
| Management-channel slot, `identity.rotate` | ✓ 2026-11-05 m11 | ✓ 2026-11-05 m11 | — | — |
| **Device interface by transport** | | | | |
| USB console, request fixtures | ✓ 2026-10-25 a29 | ✓ 2026-10-25 b18 | ✓ 2026-11-06 c5 | ✓ 2026-11-06 d6 |
| BLE | n/a | n/a | n/a | n/a |
| MQTT over TLS broker | ✓ 2026-11-05 m12 | ✓ 2026-11-05 m12 | n/a | n/a |
| LoRa point to point | fam | ✓ 2026-11-08 m16 | ✓ 2026-11-14 c15 | ✓ 2026-11-14 d16 |
| LoRa management channel | fam | ✓ 2026-11-08 m16 | — | — |
| Stock remote-admin CLI over LoRa | ✓ 2026-10-22 a7 | fam | — | — |
| Long console lines (to 1,600 B) | fam | ✓ 2026-11-04 b27 | — | — |
| **Telemetry streams** | | | | |
| Signed `logs` stream over broker | ✓ 2026-11-05 m12 | ✓ 2026-11-05 m12 | n/a | n/a |
| `runtime` stream, heap floor flag | fam | ✓ 2026-11-11 b42 | — | — |
| `radio` stream counters over MQTT | fam | ✓ 2026-11-12 b47 | n/a | n/a |
| Screen stream | ✓ 2026-11-06 a41 | — | — | n/a |
| **Compact reports and standing instructions over LoRa** | | | | |
| Signed compact report over LoRa | fam | ✓ 2026-11-08 m16 | ✓ 2026-11-14 c15 | ✓ 2026-11-14 d16 |
| `standing.put`, `standing.remove` over LoRa | fam | ✓ 2026-11-08 m16 | fam | ✓ 2026-11-14 d16 |
| Scheduled report runs, survives reboot | fam | ✓ 2026-11-08 m15 | — | — |
| **Signed updates, by transport** | | | | |
| USB console, signed | fam | ✓ 2026-11-11 b41 | — | — |
| BLE | n/a | n/a | n/a | n/a |
| Broker push, signed | — | — | n/a | n/a |
| HTTPS download, signed (offer over broker) | fam | ✓ 2026-11-12 b45 | n/a | n/a |
| Serial DFU by the bootloader, signed | n/a | n/a | — | — |
| **Native messaging operations** | | | | |
| `message.send`, inbox, `message.deliver`, contacts | n/a | n/a | n/a | n/a |
| **Server-role settings and actions, `packet.transmit`** | | | | |
| `packet.transmit` | fam | ✓ 2026-11-14 b48 | ✓ 2026-11-14 m18 | ✓ 2026-11-14 m18 |
| Repeating beside a manager packet | — | — | ✓ 2026-11-14 m19 | fam |
| `neighbour.list`, `discover.neighbors` | fam | ✓ 2026-11-12 b43 | ✓ 2026-11-06 c6 | fam |
| Server reads: `board.get`, `sensor.list`, `gps.get`, `audit.list` | fam | ✓ 2026-11-12 b43 | fam | ✓ 2026-11-25 d32 |
| Signed `config.apply` of flood limits | fam | ✓ 2026-11-12 b46 | fam | ✓ 2026-11-25 d32 |
| Signed `stats.clear` over MQTT | fam | ✓ 2026-11-12 b47 | n/a | n/a |
| Other v0.3.3 server settings and actions, signed | sim | sim | sim | sim |
| Unsigned server writes refused (`log.*`, `clock.reset`, ...) | fam | ✓ 2026-11-12 b44 | fam | ✓ 2026-11-25 d32 |
| Stale remote-admin login refused, audited | fam | ✓ 2026-11-08 m6 | — | — |
| Duty-cycle floor | ✓ 2026-10-17 a8 | fam | fam | ✓ 2026-11-25 d32 |
| **Continuity package, recovery, measurements** | | | | |
| `deployment.continuity.export` | fam | ✓ 2026-11-23 b59 | fam | ✓ 2026-11-25 d31 |
| `device.reset` scopes, factory reset | fam | ✓ 2026-11-23 b58 | fam | ✓ 2026-11-25 d30 |
| Heap and loop stack with a verified session | ✓ 2026-11-05 m13 | ✓ 2026-11-05 m13 | fam | ✓ 2026-11-06 d10 |
| Role installed by serial DFU over a Companion's store | n/a | n/a | — | ✓ 2026-11-07 d7 |

## Room server

| Capability | Heltec V3 | Heltec V4 | Wio L1 | T1000-E |
|---|---|---|---|---|
| **Identity and enrolment** | | | | |
| First provisioning | fam | ✓ 2026-11-08 m16 | fam | ✓ 2026-11-06 d8 |
| Signed request accepted | ✓ 2026-11-05 m11 | ✓ 2026-11-05 m11 | ✓ 2026-11-15 m22 | ✓ 2026-11-06 d8 |
| Unsigned request refused | ✓ 2026-11-05 m12 | ✓ 2026-11-12 b44 | fam | ✓ 2026-11-15 m23 |
| Signed `deployment.reset` | fam | ✓ 2026-11-23 b57 | fam | ✓ 2026-11-06 d8 |
| Management-channel slot, `identity.rotate` | ✓ 2026-11-05 m11 | ✓ 2026-11-05 m11 | — | — |
| **Device interface by transport** | | | | |
| USB console, request fixtures | fam | ✓ 2026-10-25 b19 | ✓ 2026-11-06 c5 | ✓ 2026-11-06 d8 |
| BLE | n/a | n/a | n/a | n/a |
| MQTT over TLS broker | ✓ 2026-11-05 m12 | ✓ 2026-11-05 m12 | n/a | n/a |
| LoRa point to point | fam | ✓ 2026-11-08 m16 | — | — |
| LoRa management channel | fam | ✓ 2026-11-08 m16 | — | — |
| Stock remote-admin CLI over LoRa | — | — | — | — |
| **Telemetry streams** | | | | |
| Signed `logs` stream over broker | ✓ 2026-11-05 m12 | ✓ 2026-11-05 m12 | n/a | n/a |
| Screen stream | — | — | — | n/a |
| **Compact reports and standing instructions over LoRa** | | | | |
| Signed compact report over LoRa | fam | ✓ 2026-11-08 m16 | — | — |
| `standing.put`, `standing.remove` over LoRa | fam | ✓ 2026-11-08 m16 | — | — |
| **Signed updates, by transport** | | | | |
| USB console, signed | fam | ✓ 2026-11-23 b55 | — | — |
| BLE | n/a | n/a | n/a | n/a |
| Broker push, signed | — | — | n/a | n/a |
| HTTPS download, signed | — | — | n/a | n/a |
| Serial DFU by the bootloader, signed | n/a | n/a | — | — |
| **Native messaging operations** | | | | |
| `message.send`, inbox, `message.deliver`, contacts | n/a | n/a | n/a | n/a |
| **Server-role settings and actions, `packet.transmit`** | | | | |
| `packet.transmit` | n/a | n/a | n/a | n/a |
| Server reads (`neighbour.list` answers not-on-this-device) | fam | ✓ 2026-11-12 b43 | fam | ✓ 2026-11-25 d32 |
| Unsigned server writes refused (`room.post`, `log.*`, ...) | fam | ✓ 2026-11-12 b44 | fam | ✓ 2026-11-25 d32 |
| `room.post`, signed | fam | ✓ 2026-11-23 b56 | ✓ 2026-11-15 m22 | ✓ 2026-11-15 m22 |
| Other v0.3.3 server settings and actions, signed | sim | sim | sim | sim |
| Stale remote-admin login refused, audited | fam | ✓ 2026-11-08 m6 | — | — |
| **Continuity package, recovery, measurements** | | | | |
| `deployment.continuity.export` | fam | ✓ 2026-11-23 b59 | fam | ✓ 2026-11-25 d31 |
| `device.reset` scopes, factory reset | fam | ✓ 2026-11-23 b58 | fam | ✓ 2026-11-25 d30 |
| Heap and loop stack with a verified session | ✓ 2026-11-05 m13 | ✓ 2026-11-05 m13 | fam | ✓ 2026-11-06 d8 |
| Role installed by serial DFU over a Companion's store | n/a | n/a | — | ✓ 2026-11-07 d7 |

## Why a cell says n/a

- **Wi-Fi, MQTT and HTTPS download on the Wio Tracker L1 and T1000-E:** the boards have no Wi-Fi and no IP link.
- **BLE on a Repeater or Room server:** none, by design decision. Local management is the USB console.
- **BLE update on the Wio Tracker L1 and T1000-E:** none, by design decision. nRF52 updates are signed offers followed by the bootloader's serial DFU over USB.
- **Serial DFU on a Heltec board:** ESP32 images are written by the device itself, not by a bootloader DFU.
- **Screen stream on the T1000-E:** the board has no screen.
- **Messaging operations on a Repeater or Room server:** Companion only.
- **`packet.transmit` on a Companion or Room server:** Repeater only.
- **Wi-Fi join, profiles and connectivity mode on the nRF52 boards:** no Wi-Fi.

## Limits worth knowing before trusting a cell

- A `*` cell ran on a test build that was not a release.
- Where a row names a hardware test peer, such as a Heltec V3 Companion acting as the manager's radio, the mark belongs to the board under test, not the peer.
- A BLE `✓` on the T1000-E Companion is 33 of 34 fixtures: the one failure is a fixture that sets a Wi-Fi key, which a board without Wi-Fi refuses by design.
- The T1000-E reset-scope cells (`d30`) cover every scope but `factory`: the board's node key had to stay for a Backend row. The factory reset over the interface on an nRF52 server role has no row.
- On the Heltec V4 Repeater, the console update (`b41`) was run with 1,024-byte chunks. Chunks of 2,304 bytes moved nothing because the console line limit is 1,600 bytes.
- A Wio Tracker L1 row marked `c8` was reported by the reference Backend's tooling through the Reader, not run by this repository's own tools.
- The row `m24`, LoRa management on the Wio Tracker L1 and T1000-E pair, does not say which board held the Room server and which the Repeater, so no cell takes a mark from it.
- The Heltec V4 screen cell (`a42`) is filed under the Heltec V3 heading in HARDWARE_VERIFIED, but the row itself names a Heltec V4 Companion.

## Known gaps

Every `—` above is a gap; a `fam` cell is not. A `—` in a family whose other board also has none, or in a board-specific hardware row, is what conformance still needs. These are the larger ones.

- **LoRa management with a Companion as the target, on a Heltec board.** The T1000-E Companion answered on both paths, one hop (the Wio cells take `fam` from it). A resend under the same `xid` that completes, and a flood across a hop, have no row. Multi-frame replies still lose a frame now and then.
- **LoRa management of a Heltec V3 Repeater or Room server.** Only the V4 was the target.
- **Room server on the Wio Tracker L1.** The fixtures (24 of 24), a stock GETTING_STARTED run and a signed `room.post` are recorded. First provisioning, an unsigned refusal, LoRa and updates are not.
- **Room server on the T1000-E.** Console fixtures, enrolment, a signed `room.post` and an unsigned request refused are recorded. No LoRa mark and no update. One LoRa management run on the Wio Tracker L1 and T1000-E pair names a Room server, but not which board held it.
- **Room server on the Heltec V3.** No fixture row for the Room server build. Rotation, broker interface and heap are recorded.
- **Channel-mode replies on the nRF52 server roles.** A `standing.put` or channel-mode request lost a frame on the air. So did the first frame of 2 of 6 channel replies; a resend returned the stored result.
- **Standing instructions that actually run (scheduled report, reboot) on anything but a Heltec V4 Repeater and a Wio Tracker L1 Companion.** The Wio run had no reboot.
- **Compact report cadence and airtime.** Cadence is measured on a Wio Tracker L1 Companion only (902.9 s, then 931.2 s, for every 900 s). Airtime per report has no hardware row.
- **BLE bonds, BLE PIN reads and key export on a board, and the `network` and `radio` reset scopes on a Companion.** Simulator only. The `contacts`, `channels` and `factory` scopes, and the redirect of `deployment` and `identity`, ran on a Heltec V4 and a T1000-E Companion. On the server roles every scope ran on the Heltec V4 and the T1000-E, and the factory reset on the Heltec V4 only.
- **Deployment continuity destroyed by Hardened.** Storage, export and destruction by a signed reset ran in both chip families on all three roles; destruction under Hardened ran in the simulator only.
- **Server-role actions and settings v0.3.3 added** beyond the flood limits and `stats.clear`, run for real on an enrolled board. Needs a signed request a reference Backend cannot send yet.
- **`packet.transmit` on the Heltec V3 Repeater.**
- **Signed updates on the server roles** by USB console on the nRF52 boards, by broker push, and by HTTPS download on the Room server.
- **Signed updates on the Heltec V3 by HTTPS download**, and on the Heltec V4 Companion by broker push.
- **Messaging on the nRF52 Companions:** `message.deliver`. `message.send` and `inbox.*` with a received message ran on the T1000-E.
- **T1000-E Companion over BLE:** the Reader connecting directly, and resource use under BLE and RF load together.
- **Wio Tracker L1 Companion over BLE:** no device-interface row.
- **Screen stream on the Wio Tracker L1**, and the stream over BLE or a broker on any board.
- **Factory reset over the interface** on the nRF52 server roles; the Companion has it in both chip families and the server roles on the Heltec V4. The rescue-console `erase` is recorded on the Heltec V3 only.
- **Software-only boards.** Other board environments get software checks only; they are not in this matrix.

## Evidence key

Each code is a row of [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md) under the named heading, counted from the top of that table. The text is the row's feature column.

- `a2` Heltec V3: Adaptive contacts
- `a7` Heltec V3: CLI over LoRa (Repeater build)
- `a8` Heltec V3: Duty-cycle floor (Repeater)
- `a10` Heltec V3: Hardened posture (default PIN)
- `a11` Heltec V3: Device screen and button (standalone build)
- `a13` Heltec V3: Rescue console (standalone build)
- `a14` Heltec V3: Companion-to-Companion messaging over LoRa (with a Heltec V4 test board)
- `a15` Heltec V3: ESP-IDF 5.3: upgrade without erase (standalone)
- `a18` Heltec V3: ESP-IDF 5.3: BLE (NimBLE)
- `a19` Heltec V3: ESP-IDF 5.3: Wi-Fi + TLS MQTT (standalone)
- `a22` Heltec V3: ESP-IDF 5.3: loop task stack (standalone)
- `a23` Heltec V3: Device interface (standalone)
- `a24` Heltec V3: Logs over MQTT, signed (standalone, provisioned with the public test keys)
- `a25` Heltec V3: `message.send` over USB (standalone), to a Heltec V4 on stock MeshCore companion-v1.18.0
- `a26` Heltec V3: `inbox.list` and `inbox.ack` over USB (standalone), from a Wio Tracker L1 sending on a private setting
- `a27` Heltec V3: `clock back` over USB
- `a29` Heltec V3: Device interface (Repeater build, USB console)
- `a32` Heltec V3: Signed update over BLE on an enrolled board, from the real releases (standalone)
- `a38` Heltec V3: Paged inventory stream, Companion over USB, 100 made-up contacts
- `a39` Heltec V3: A push over a verified broker session with other requests beside it (enrolled, all ten streams, 1.74 MB, 0 ...
- `a41` Heltec V3: Screen stream, Companion and Repeater
- `a42` Heltec V3: Screen stream and message text, Normal and Hardened (Heltec V4 standalone Companion; Wio Tracker L1 Compani...
- `a46` Heltec V3: `identity.rotate` over a verified broker (standalone, public test keys, bench timing marks)
- `a53` Heltec V3: † v0.3.0 signed update over USB from the signed v0.2.1, from the release's own assets (standalone)
- `a55` Heltec V3: v0.3.3 Companion interface keys and reads (standalone, enrolled on the reference Backend, the release's image in both app slots)
- `a59` Heltec V3: Large requests over the USB cable (v0.3.5 standalone, enrolled on the reference Backend, the release's image in both app slots)
- `b6` Heltec V4: Default-PIN BLE refusals
- `b7` Heltec V4: Connectivity mode
- `b9` Heltec V4: Wi-Fi failover between two access points
- `b10` Heltec V4: ESP-IDF 5.3: upgrade without erase (standalone)
- `b16` Heltec V4: ESP-IDF 5.3: loop task stack (standalone)
- `b17` Heltec V4: Device interface (standalone)
- `b18` Heltec V4: Device interface (Repeater build, USB console)
- `b19` Heltec V4: Device interface (Room server build, USB console)
- `b20` Heltec V4: Authority layer (standalone, USB)
- `b21` Heltec V4: Authority layer: corrupt record over USB
- `b27` Heltec V4: Longest interface line on the Repeater's native USB
- `b28` Heltec V4: `identity.rotate` over a verified broker (standalone Companion, few contacts)
- `b30` Heltec V4: Signed update over BLE, enrolled (standalone)
- `b37` Heltec V4: † v0.2.1 signed update over USB from the signed v0.2.0, from the release's own assets (standalone)
- `b39` Heltec V4: Unsigned cable, enrolled, in Hardened (standalone)
- `b41` Heltec V4: † v0.3.1 signed update over the console from the signed v0.3.0, from the release's own assets (Repeater, `h...
- `b42` Heltec V4: Heap floor on a server build (Repeater, the v0.3.0 release)
- `b43` Heltec V4: v0.3.3 Repeater and Room server reads (enrolled on the reference Backend, the release's images in both app slots)
- `b44` Heltec V4: v0.3.3 Repeater and Room server writes over an unsigned cable (enrolled)
- `b45` Heltec V4: v0.3.3 by a signed update from a reference Backend (Repeater, enrolled, Backend row 694, from v0.3.2)
- `b46` Heltec V4: Flood limits by a signed `config.apply` from a reference Backend (v0.3.3 Repeater, enrolled, Backend row 694)
- `b47` Heltec V4: `stats.clear` signed by a reference Backend over MQTT (v0.3.3 Repeater, enrolled, Backend row 694)
- `b51` Heltec V4: `status.get` battery and LoRa counters (standalone Companion, v0.3.17)
- `b52` Heltec V4: Management-channel slot over the cable (standalone Companion, v0.3.17)
- `b53` Heltec V4: Continuity package storage (standalone Companion, v0.3.17)
- `b54` Heltec V4: `device.reset` scopes over the cable (standalone Companion, v0.3.17)
- `b55` Heltec V4: † v0.3.17 signed update over the console from the signed v0.3.16 (Room server)
- `b56` Heltec V4: `room.post` signed, and busy, revert and replay over the console (Room server, v0.3.17)
- `b57` Heltec V4: Signed `deployment.reset` over the console (Repeater and Room server, v0.3.17)
- `b58` Heltec V4: `device.reset` scopes and the factory reset over the console (Repeater and Room server, v0.3.17)
- `b59` Heltec V4: Continuity package storage over the console (Repeater and Room server, v0.3.17)
- `b48` Heltec V4: `packet.transmit` on a Repeater (`heltec_v4_repeater`, main 13fcd703, enrolled with the public test authori...
- `c1` Wio Tracker L1: Adaptive contacts capacity
- `c2` Wio Tracker L1: nRF52 interface worker
- `c5` Wio Tracker L1: Device interface (Companion USB, Repeater and Room server console)
- `c6` Wio Tracker L1: `GETTING_STARTED` walked from a fresh clone, Repeater and USB Companion builds
- `c8` Wio Tracker L1: v0.2.0 signed update through the Reader over USB (`WioTrackerL1_companion_radio_usb`), run by the reference Backend's tooling...
- `c9` Wio Tracker L1: Loop stack through the Companion's radio paths (v0.2.1 USB Companion, unenrolled, Heltec V3 as the peer, pr...
- `c10` Wio Tracker L1: v0.3.3 by release DFU over v0.3.2 (USB Companion, enrolled on the reference Backend)
- `c14` Wio Tracker L1: A cable host that stops reading (USB Companion; main 13fcd703, then the build with the fix, the fix)
- `c15` Wio Tracker L1: Repeater management over LoRa (Repeater package, enrolled with the public test keys; Heltec V4 standalone C...
- `d1` SenseCAP T1000-E: Public T1000-E Companion USB build, enrolled
- `d2` SenseCAP T1000-E: Companion on a board carrying the private firmware's data
- `d3` SenseCAP T1000-E: Companion heap and loop stack (USB build)
- `d4` SenseCAP T1000-E: Device interface over BLE (`t1000e_companion_radio_ble`), a PIN of the device's own
- `d5` SenseCAP T1000-E: Management writes with the private firmware's Deployment record
- `d6` SenseCAP T1000-E: Device interface (Companion USB, Repeater console)
- `d7` SenseCAP T1000-E: Repeater and Room server on a store left by a Companion, serial DFU with no erase
- `d8` SenseCAP T1000-E: Room server on cleared marker pages
- `d10` SenseCAP T1000-E: Repeater enrolment with the public test keys (`tools/mecon_worker_hw.py`)
- `d13` SenseCAP T1000-E: v0.3.1 signed update through the bootloader from the signed v0.3.0, from the release's own assets (`t1000e_...
- `d14` SenseCAP T1000-E: v0.3.3 by release DFU over v0.3.2 (USB Companion, enrolled on the reference Backend)
- `d15` SenseCAP T1000-E: A cable host that stops reading (USB Companion; main 13fcd703, then the build with the fix, the fix)
- `d16` SenseCAP T1000-E: Repeater management over LoRa (Repeater package, enrolled with the public test keys; Heltec V4 standalone C...
- `d17` SenseCAP T1000-E: Continuity package storage (SenseCAP T1000-E USB Companion, v0.3.8, enrolled with the public test authority, over its cable)
- `d18` SenseCAP T1000-E: `get dutycycle` (USB Companion, v0.3.17)
- `d19` SenseCAP T1000-E: `clock back` over USB (USB Companion, v0.3.17)
- `d20` SenseCAP T1000-E: Paged inventory stream over USB (USB Companion, v0.3.17)
- `d21` SenseCAP T1000-E: Large requests over the USB cable (USB Companion, v0.3.17)
- `d22` SenseCAP T1000-E: `device.reset` scopes over the cable (USB Companion, v0.3.17)
- `d23` SenseCAP T1000-E: `message.send` on a private channel (USB Companion, v0.3.17)
- `d24` SenseCAP T1000-E: Replay and the reserved mark (USB Companion, v0.3.17)
- `d25` SenseCAP T1000-E: Signed `identity.rotate` (USB Companion, v0.3.17)
- `d26` SenseCAP T1000-E: `inbox.list`, `inbox.ack` with a received message (USB Companion, v0.3.19)
- `d27` SenseCAP T1000-E: Device interface over LoRa point to point, Companion as target (USB Companion, v0.3.19)
- `d28` SenseCAP T1000-E: Device interface on the LoRa management channel, Companion as target (USB Companion, v0.3.19)
- `d29` SenseCAP T1000-E: Hardened posture and locked reads over the cable (USB Companion, v0.3.19)
- `d30` SenseCAP T1000-E: `device.reset` scopes over the console (Repeater and Room server, v0.3.19)
- `d31` SenseCAP T1000-E: Continuity package storage over the console (Repeater and Room server, v0.3.19)
- `d32` SenseCAP T1000-E: Server reads, unsigned writes, flood limits, duty floor (Repeater and Room server, v0.3.19)
- `m1` More than one board: `get dutycycle` per band (Heltec V3 standalone Companion)
- `m2` More than one board: `message.deliver` (Heltec V3 standalone Companion, USB)
- `m3` More than one board: Clock unset until a real source sets it (Heltec V3)
- `m4` More than one board: Clock unset until a real source sets it (Wio Tracker L1)
- `m6` More than one board: A stale login refused and audited (Heltec V4 Repeater, then Room server, as the server; Heltec V3 standalon...
- `m7` More than one board: Public Wio Tracker L1 Companion USB build
- `m8` More than one board: `online` means requests are accepted (Heltec V3 standalone Companion, verified broker)
- `m10` More than one board: HTTPS download beside a verified broker session (Heltec V4 standalone Companion, test key, budget lowered)
- `m11` More than one board: Management-channel slot and `identity.rotate` (Heltec V3 and V4, Repeater and Room server, USB console)
- `m12` More than one board: Device interface over a TLS-verified broker (Heltec V3 and V4, Repeater and Room server)
- `m13` More than one board: Repeater and Room server heap with a verified session (Heltec V3, V4)
- `m14` More than one board: USB Companion build over an earlier build, by serial DFU (`t1000e_companion_radio_usb`)
- `m15` More than one board: A standing instruction runs over LoRa (Heltec V4 Repeater, Heltec V3 standalone Companion as the manager's ...
- `m16` More than one board: LoRa management, point to point and on the management channel (Heltec V4 Repeater, then Room server; Heltec...
- `m18` More than one board: `packet.transmit` from two Repeaters (Wio Tracker L1 and SenseCAP T1000-E Repeaters, test keys; Heltec V4 s...
- `m19` More than one board: Repeating beside a manager packet, and two Companions through a Repeater with no Backend (Heltec V4 and Sen...
- `m20` More than one board: `message.deliver` bounds (Heltec V4 standalone Companion, USB)
- `m21` More than one board: A cable host that stops reading, control (Heltec V4 standalone Companion, main 13fcd703, no fix)
- `m22` More than one board: Room server console with the public test keys (Wio Tracker L1 and SenseCAP T1000-E, v0.3.7)
- `m23` More than one board: `log.read` on a Room server console (SenseCAP T1000-E, v0.3.7, test keys)
- `m24` More than one board: LoRa management to a Room server and a Repeater (Wio Tracker L1 and SenseCAP T1000-E, each as the other's USB Companion manager, v0.3.7)
- `m25` More than one board: A standing report with the manager's Backend away (Seeed Wio Tracker L1 USB Companion as the target, v0.3.6)
