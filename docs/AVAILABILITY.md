# Availability

The other pages describe the finished product. This page says what you can get today and how far each capability has been checked. [Hardware evidence](HARDWARE_VERIFIED.md) says what has run on which board, and the [conformance matrix](CONFORMANCE_MATRIX.md) lays it out per board and role.

**As of 27 November 2026.** The latest release is [v0.3.22](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.22). The [releases page](https://github.com/hoejriis/mecon-firmware/releases) is the current source: if it shows a newer tag, the table below has not been reviewed against it. Each release's notes say what it contains and what was and was not run.

## What you can get

- **Source, under MIT.** You can build it yourself ([install and set up a device](GETTING_STARTED.md)).
- **Signed pre-releases.** Every release from v0.2.0 to v0.3.22 is a pre-release. No supported release exists, and no date is announced for one. Source-only pre-releases (v0.1.0 to v0.1.3) came before them.
- **Images for 14 board variants** in each release since v0.3.0: every build of the Heltec V3, Heltec V4, Wio Tracker L1 and SenseCAP T1000-E that carries the managed-device interface. A device on v0.3.0 or later takes the next release by a signed update ([updating](UPDATING_AND_RECOVERY.md)). A device on an earlier build is flashed once over a cable. Since v0.3.22, each of the 8 nRF52 variants also has a `.uf2` recovery file beside its signed package (37 files in all), for a board stuck in its bootloader after an interrupted update ([recovery](VERSIONING_AND_RELEASES.md#recovering-an-nrf52-board-from-the-bootloaders-uf2-drive)). It is not a signed update.
- **Heltec V3 and V4 `_usb`, `_ble` and `_wifi` Companions** were signed only in v0.2.0 and v0.2.1. They are upstream builds without the managed-device interface and cannot take a signed update. Use the standalone build.

## Where each capability stands

"Released" means the capability is in a release, with the first tag that has it. "Hardware-verified" names the board and path of a recorded check. A capability with no hardware row is not claimed on hardware. Boards other than these four (Heltec V3, Heltec V4, Wio Tracker L1, SenseCAP T1000-E) get compile and simulator checks only.

| Capability | Released | Hardware-verified | Not yet confirmed |
|---|---|---|---|
| Human-readable `ping` on the management channel | Companion and Repeater from v0.3.9, Room server from v0.3.10 | None recorded. Unit and simulator tests pass | A stock app asking and reading the reply on a real device |
| Device interface over LoRa, point to point and on the management channel | From v0.2.0, with limits | Repeater and Room server on a Heltec V4 and on the Wio Tracker L1 and SenseCAP T1000-E, with a Companion as the manager's radio. SenseCAP T1000-E Companion as the target, one hop, both paths | A Heltec or Wio Companion as the target, a flood across a hop, a retry that completes. Frames are lost on the air more often on flooded paths |
| Telemetry: compact reports, standing instructions, observations and counters | Reports and standing instructions from v0.2.0. Observations and counters grew in v0.3.9 and v0.3.10 | Compact report and standing instruction over LoRa on a Heltec V4 Repeater and a Wio Tracker L1 Companion. Paged inventory over USB on a Heltec V3 and a SenseCAP T1000-E. Signed log stream over MQTT on a Heltec V3. `status.get` battery and LoRa counters on a Heltec V4 and a SenseCAP T1000-E | The screen stream over Bluetooth or a broker. The observation fields added in v0.3.9 and v0.3.10 have no hardware row |
| Delegated receive identities (Companion only) | From v0.3.10 | None recorded | Receiving and acknowledging for a delegated identity on a board, and how a stock app shows it |
| Signed updates over the cable, Bluetooth, a broker or HTTPS | From v0.2.0 | Companion: USB on a Heltec V3 and V4, bootloader update over USB on a Wio Tracker L1 and SenseCAP T1000-E (nRF52 boards update this way only), Bluetooth on a Heltec V3 and V4, broker push on a Heltec V3, HTTPS download on a Heltec V4. Console on a Heltec V4 Repeater and Room server | Server-role updates by broker or HTTPS on boards other than those named |
| Signed updates over LoRa | The target and gateway side, in v0.3.21: ESP32 dual-slot builds only, point to point only. A reference manager is in `tools/` | None recorded. Fixtures and simulator tests pass | A whole image sent between two boards over the radio |
| Console request line of 4,096 bytes | From v0.3.21. A console `hello` reports `limits.line_bytes`; a console with none reads 1,600 | Heltec V4 Repeater: a 1,951-byte signed re-enrolment accepted, `limits.line_bytes` 4096 | An nRF52 console run |
| Reports kept while unreachable (`keep`, `report.list`, `report.ack`) | From v0.3.21. A flash ring of 64; `every_s` at least 900 | A kept report listed, then acknowledged, on an ESP32 board (recorded in the v0.3.21 notes) | The nRF52 batching path on a board |
| Companion notifications only for direct messages and favourite channels | From v0.3.21. A Companion stays quiet for channel traffic until a channel is marked favourite | `channel.list` carries `favourite` on a Heltec V3 standalone Companion | The buzzer and screen acting on a real device |
| CA anchor in pieces (`ca_begin`, `ca_add`, `ca_end`) | From v0.3.22 | None recorded. Simulator tests pass against a broker | A verified session after a CA installed in pieces over a real broker |
| nRF52 hardware watchdog (120 s) | From v0.3.20 | SenseCAP T1000-E Repeater | A Wio Tracker L1 or a RAK4631 |
| Sleep (battery guard) | Setting, CLI and dry run from v0.3.16. Opt-in and off by default. It reports only and never sleeps | None | The guard acting (sleep, shutdown, recovery) is not in any release. The setting has not been confirmed on a board |
| Cross-device sends | `packet.transmit` on the Repeater from v0.3.6. `message.send` on the Companion | `packet.transmit` on a Heltec V4 Repeater, and on Wio Tracker L1 and SenseCAP T1000-E Repeaters, received on a Companion. `message.send` over USB on a Heltec V3 | A manager's coordinated send across several gateways. That is the manager's feature, not the firmware's, and no end-to-end run is recorded |

Other limits:

- On main, not yet released: nRF52 builds compute signatures on the 8 KB worker task instead of the loop task. It has not run on a board.
- Heltec V3 with a TLS broker session: since v0.3.21 the heap-floor check allows for the session's own input buffer, so `heap_low` no longer reads set only because of it. The fix has not run on a board.
- The management authority's contract is still being refined, so it can still gain detail.

## What only a manager brings

Managed use needs a manager, and this project publishes none. The device contracts are public ([contracts](contract/README.md)), so any manager can implement them independently. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) publishes one, and its [capabilities page](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) says what is available there. Standalone use needs no manager.

Backend-coordinated messaging (the gateway sequence described in the [README](../README.md#fleet-profiles)), backfill, RF health monitoring and firmware-image delivery over LoRa from the manager's side are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)). Autonomous outage forwarding is not planned.
