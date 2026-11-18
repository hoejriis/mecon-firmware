# Availability

The other pages describe the finished product. This page and the release notes say what you can actually get. [Hardware evidence](HARDWARE_VERIFIED.md) says what has run on which board.

**Reviewed 15 November 2026, against v0.3.7.** Anything newer is in the [Issues](https://github.com/hoejriis/mecon-firmware/issues) and the [releases](https://github.com/hoejriis/mecon-firmware/releases).

## What you can get

- **Source, under MIT.** You can build it yourself ([install and set up a device](GETTING_STARTED.md)).
- **A signed pre-release,** [v0.3.7](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.7), with images and signed manifests for 14 board variants: every build of the four boards that carries the managed-device interface (on the Heltec V3 and V4 the standalone Companion, the Repeater and the Room server; on the Wio Tracker L1 and the SenseCAP T1000-E the BLE and USB Companions, the Repeater and the Room server). It carries the features described here. The table says which are not yet proven. A device on an earlier signed release takes it by a signed update. From v0.3.0 that has run on a Heltec V4 Repeater over its console and on a SenseCAP T1000-E Companion through its bootloader; from v0.2.1 to v0.3.0 on a Heltec V3 Companion over USB ([hardware page](HARDWARE_VERIFIED.md)). A device on an earlier build takes it by flashing over a cable, and after that it takes signed updates.
- **Why 14, not 20.** Since v0.3.0 a release signs only these. v0.2.0 and v0.2.1 also signed the Heltec V3 and V4 `_usb`, `_ble` and `_wifi` Companions. Those are upstream builds without the managed-device interface: they cannot take a signed update, and the standalone build covers their transports. From v0.3.0 a release signs only the builds that can use it. A Heltec on one of them moves to the standalone build by cable.
- **Earlier signed pre-releases since v0.3.0,** each with the same 14 variants:
  - [v0.3.6](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.6): `packet.transmit` on the Repeater, the Deployment continuity package, `status.get` `events_dropped`, `cable_dropped` (nRF52), `rf.last_incomplete`, `lora_tx` and the frame timing (`lora_tx.recent`, `rf.heard`), and `rf` replies and `job` events that wait for room on a full queue. It fixes a loop stall on an nRF52 cable whose host stops reading, a confirm-revert that left a decimal radio setting changed, and a signed request answered `busy` using up its job id. It has the lost first frame of a multi-frame LoRa reply that v0.3.7 fixes.
  - [v0.3.5](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.5): a 2 KB receive ring on an ESP32 cable with `hello` `limits.rx_bytes` to size requests to it, the manager's rf counts in `status.get`, and the loop-section timing (`get mecon.slowpass`, `get mecon.slowsections`).
  - [v0.3.4](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.4): the Heltec V3 Companion's heap in Wi-Fi mode, the event buffers off the loop stack, and no event built while a request is arriving. It has the V3 cable loss that v0.3.5 fixes.
  - [v0.3.3](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.3): the remaining stock-CLI settings and actions over the interface on every role, `audit.list`, `contact.capacity`, and the broker's certificate chain freed once the session is up.
  - [v0.3.2](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.2): the Bluetooth controller's memory released in Wi-Fi mode.
  - [v0.3.1](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.1): the BLE bond, BLE PIN and key-export operations below.
  - [v0.3.0](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.0), without them.
- **v0.3.7 itself** fixes the lost first frame of a multi-frame LoRa management reply (a request to a contact with no known path goes zero-hop first, so Repeaters do not rebroadcast it into the reply), an enrolled manager's `status.get` answering `too-large-to-sign` (the frame timing moves to `lora.trace`), `gps.setloc` saving 0,0 without a fix, and the `stats-*` counters refused on an enrolled Repeater's cable.
- **An earlier signed pre-release,** [v0.2.1](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.2.1). From the production keys, its update path ran on a Heltec V3 and a Heltec V4 Companion over USB, on an enrolled Heltec V3 over BLE, and through the Reader on a Wio Tracker L1 over USB ([release keys](RELEASE_KEYS.md)).
- **The first signed pre-release,** [v0.2.0](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.2.0). On a Heltec V3 or V4 every request waits 0.4 to 0.9 s before it is answered. v0.2.1 fixes that.
- **Source-only pre-releases,** [v0.1.0 to v0.1.3](https://github.com/hoejriis/mecon-firmware/releases).
- **No supported release.** v0.2.0 to v0.3.7 are pre-releases. No date is announced for a supported one.

## Not available yet, or not yet proven

| Capability | Where it stands |
|---|---|
| Device-interface operations over LoRa, point to point and on the management channel | In v0.2.0 and later, with limits. Passes in the simulator on all three roles. On real radios, with a Companion as the manager's radio, on both paths: a Heltec V4 Repeater and Room server (a `main` build that v0.3.0 contains), and the Wio Tracker L1 and SenseCAP T1000-E as Repeater and Room server (v0.3.7). A reply whose first frame was lost on the air was recovered by resending the same request, which returned the stored result without running it twice. Not run with a Companion as the target or across a hop. Stock remote admin over LoRa is separate, and works with this project's restrictions |
| Compact reports over LoRa | In v0.2.0 and later, with limits. Passes in the simulator. On real radios a report on request arrives with its signature verified, on both server roles of all four boards. A 99-byte report goes out as a 149-byte packet, about 1.43 s at the EU Narrow preset, and a 900 s schedule measured 902.9 s and 931.2 s between reports ([telemetry](contract/TELEMETRY.md)) |
| Standing instructions | In v0.2.0 and later, with limits. Passes in the simulator. In v0.2.0 and v0.2.1 a new instruction sends its first report at once instead of one period later, and a count set with `runs` starts again at each boot; v0.3.0 fixes both. On real radios, put and removed over LoRa on the Heltec V4 Repeater and on the Wio Tracker L1 and SenseCAP T1000-E Room servers |
| `device.reset` | In v0.3.0: the `radio`, `network`, `contacts`, `channels` (Companion) and `factory` scopes. The `network`, `contacts`, `channels` and `factory` scopes have run on a Heltec V3 Companion. `radio` has not run on a board. The `bonds` scope is in v0.3.1 |
| BLE bonds, the BLE PIN and private-key export | In v0.3.1. `ble.bonds` reads how many centrals hold a bond; the `bonds` scope of `device.reset` clears them, from the cable only. `ble.pin.get` answers the cable always, a verified broker session while there is no Deployment record, and a member's signed request once enrolled; never a paired phone or LoRa. `identity.key.export` gives the key in clear with no Deployment record (to the cable, and on a Companion to a phone paired with the device's own PIN); once enrolled only a member's signed request gets it, wrapped for that member (`mecon-kx-v1`). Passes in the simulator; not run on a board yet |
| Every feature checked on all four boards | In progress. [Hardware evidence](HARDWARE_VERIFIED.md) lists what has been checked, and the [conformance matrix](CONFORMANCE_MATRIX.md) summarises it per board. A feature not listed there for a board is not verified on it |

The management authority's contract is still being refined, so it can still gain detail.

## What only a manager brings

Managed use needs a manager, and this project publishes none. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) publishes its documentation and target contracts, but no application source or installable release. Standalone use needs no manager.

Backend-coordinated messaging (the gateway sequence described in the [README](../README.md#fleet-profiles)), backfill, RF health monitoring, a human-readable `ping` on the management channel, delegated receive identities on a Companion and firmware-image delivery over LoRa are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)). Autonomous outage forwarding is not planned. *(Corrected 2026-11-14: it was listed as later work.)*
