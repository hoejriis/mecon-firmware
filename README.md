# mecon-firmware

**MeshCore 1.18.0 with standalone improvements to security, storage, regulatory compliance and connectivity.** It works with no backend, no account and no cloud service. The backend integration this project is named for (MECON, the MeshContinuum reference backend) is optional and comes later; nothing in the standalone features depends on it.

The code is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) pinned at upstream v1.18.0 (commit `81dbae5`), with upstream history kept so later upstream releases merge in the ordinary way. Every change to a file that exists upstream is listed in [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md). Stock behaviour and wire compatibility are kept: a stock app and stock nodes interoperate with this firmware.

> **Status.** Source-only pre-releases (0.1.x). No binaries are published. Everything described below is verified on **simulated boards** (the real Companion and Repeater code on a simulated LoRa channel, with fault-injecting storage, real MQTT brokers and simulated Wi-Fi access points) and by native unit tests. **No hardware test has been run.** Compiling for a board is not a hardware-support claim. Hardware sessions are about to start, on the Heltec V3 and V4 first; see [Status and hardware](#status-and-hardware).

## Differences from stock MeshCore 1.18.0

"Verified how" says what has been checked. "Sim" means the simulated-board suite on the real application code; "unit" means a native unit test of the decision logic. Nothing in the table has run on a board. Items marked "not run on hardware" depend on a radio, flash or TLS stack that the simulator replaces.

| Area | Stock 1.18.0 | This firmware | Verified how |
|---|---|---|---|
| Management over an untrusted transport | The Wi-Fi/Ethernet TCP companion port serves the app-protocol CLI, private-key export/import, set-PIN and factory reset to any client | These are refused with a new error code (7, `ERR_CODE_NOT_PERMITTED`) unless the transport is trusted: USB/UART always; BLE only when paired with a PIN other than 0 or the default 123456; anything that does not declare trust is untrusted (fail closed). Messaging over TCP still works. [UPSTREAM_DELTA](docs/UPSTREAM_DELTA.md), [CLI](docs/CLI.md) | Sim (TCP-class transport); BLE rule by unit test only, no simulated pairing |
| Wi-Fi password | `get wifi.pwd` returns it in clear text | Write-only on every transport; the controller also tells the radio not to keep its own copy. [WIFI](docs/WIFI.md) | Sim scans every file and log of the node for each password it set |
| Remote CLI | A flagged contact can run any command, including `set pin`, radio settings and `poweroff`; a zero timestamp passes as the local console | A remote caller on a Companion may run `get`, `ver`, `board` and `reboot`; a remote timestamp of 0 is raised to 1. `set prv.key` on a repeater or room server is local-console only. [CLI](docs/CLI.md) | Sim |
| Default admin password | Anyone who sends the compiled-in `password` is admin over the air; debug builds log the attempted password | A repeater or room server on the default (or empty) admin password refuses remote admin in both postures; attempted passwords are never logged. [STANDALONE_POSTURES](docs/STANDALONE_POSTURES.md) | Sim |
| Security postures and audit | None | **Normal** (default) and **Hardened** on Companion, Repeater and Room server. Hardened: no BLE with a default PIN, no TCP companion server, remote admin read-only plus `reboot`, remote Companion CLI off, key export/import refused even on USB. Lowering the posture works over the cable only. A 32-entry persistent audit log (`get audit`) records posture changes, refusals, failed logins and resets, never a password. [STANDALONE_POSTURES](docs/STANDALONE_POSTURES.md) | Sim for everything except BLE; BLE rules by unit test |
| Duty-cycle enforcement | Token bucket; default 100 %; strictest reachable limit 10 %; a sliding hour can carry almost twice the limit | Sliding one-hour airtime window (121 bins of 30 s, 242 bytes of RAM). The limit is the stricter of the user's setting and a regulatory floor for the channel (EU 863-870 MHz sub-bands, 433.05-434.79 MHz; the whole occupied channel is judged and the strictest band touched applies). Seeded at boot as if the previous hour were full, so reboot loops cannot exceed it. Companion, Repeater, Room server; not KISS modem, sensor or secure-chat. `get dutycycle` shows band, limit, floor and airtime. [UPSTREAM_DELTA](docs/UPSTREAM_DELTA.md) | Sim, measured by the simulated channel (table below) |
| Atomic saves | Prefs, identity, channels, contacts, blobs and the client ACL are rewritten in place; a power cut leaves a truncated file the loader accepts | All of them are written to a temporary file, read back (length and CRC), then swapped in; boot finishes or drops an interrupted swap. A damaged identity is kept aside as `<file>.damaged`, not replaced. [UPSTREAM_DELTA](docs/UPSTREAM_DELTA.md) | Sim: power cut at any byte and between write and rename. The rename behaviour of real SPIFFS/LittleFS is **not verified on hardware**. The nRF52/STM32 blob-table path compiles but no test exercises it |
| Bounds fixes | A short TRACE packet makes a `uint8_t` length wrap near 255 and reads past the packet buffer; `set radio`, `tempradio`, `sensor set`, `extra.sf` overflow a 132-byte buffer with a long argument | TRACE payloads shorter than the header are dropped and hop entries are bounds-checked; the copies are bounded | Sim under AddressSanitizer builds |
| Wi-Fi profiles | One network | Up to 10 profiles with priorities. Joins the best visible one, fails over on a lost network, a wrong password or repeated drops, returns to a better network only after a hold-down (120 s), escalating cooldown for a wrong password. Stock `wifi.*` commands map to profile 0 and stock settings files load unchanged. [WIFI](docs/WIFI.md) | Unit tests against a fake radio; sim against simulated access points. Not run on hardware: ESP32 disconnect-reason mapping, RP2040 status polling |
| MQTT | None | Up to 6 broker profiles, one session at a time, failover by priority, return after a hold-down. Static profiles (host or IP; names re-resolved on every attempt) or LAN discovery by mDNS. TLS trust is a pinned certificate fingerprint **or** a private CA certificate, with SNI; a discovered broker is used only if it advertises the profile's site ID **and** passes TLS verification, never plain. Retained status topic. Connect runs on a worker so the main loop never blocks. Hardened refuses plain profiles. [MQTT](docs/MQTT.md) | Sim against real Mosquitto (plain and TLS) with a simulated mDNS responder and DNS; the rogue-broker case is checked from the rogue's own log. Main-loop gap under 200 ms measured in sim. Not run on hardware: mDNS query, fingerprint and CA checks on ESP32, SNI, worker stack size, TLS heap (estimated 40-50 KB). RP2040 unsupported |
| BLE / Wi-Fi mode | Separate `_ble` and `_wifi` builds | The `*_companion_radio_standalone` builds (Heltec V3, V4) carry BLE, Wi-Fi, MQTT and USB and run one of BLE or Wi-Fi at a time, switched by `set connectivity.mode`, or by a two-step long press on the device button. Only one radio stack is initialised per boot. USB management is meant to work in both modes. [CONNECTIVITY_MODE](docs/CONNECTIVITY_MODE.md) | Sim with a BLE stub; the switch logic, button gesture and audit are tested. USB coexistence with BLE or Wi-Fi is **assumed, not verified on hardware** |
| Connectivity screen | Home screen shows the Wi-Fi address | A connectivity page (mode, posture, PIN state, Wi-Fi profile/RSSI/IP, MQTT state and TLS verification) with warnings for a default PIN and unverified TLS; the status text has no field that can carry a password. [CONNECTIVITY_MODE](docs/CONNECTIVITY_MODE.md), [UI](docs/UI.md) | Sim with a recording display on the real screens. Not run on a real OLED |
| Contact memory | One static array of 350 contacts (about 64 KB of RAM) on the Heltec V3, T1000-E and Wio Tracker L1 builds, saved as one file | A persistent store on the file system (160-byte records in 8-record bucket files, each saved atomically) with a RAM cache of 16-96 slots sized from the free heap at boot. Capacity follows free file-system space and is never below the board's stock count while space allows. Favourites stay resident. When full, the least meaningful non-favourite is evicted (advert-only before contacts with message history); a store of only favourites refuses new contacts rather than evict. A stock `/contacts3` is migrated on first boot. [CONTACTS](docs/CONTACTS.md) | Sim: RAM asserted below 65 % of the stock array's 65,872 bytes (358 slots of 184 bytes) at the default 300,000-byte heap budget; migration, eviction order, power cuts. Flash wear and real free heap **not exercised** |
| CLI | Four entry points (Companion frame, rescue console, remote contact, repeater console), each with its own rules | One engine in `src/helpers/cli/`: every command has a class (`read`, `write`, `secret`, `posture`, `destructive`, `reboot`) and one privilege table maps caller x posture x class to allow or refuse. An unregistered command is a `write`. A refusal gives an explicit reply and an audit entry. Adds a command line over MQTT (TLS-verified sessions only) and `mecon.*` commands through the upstream hook. Upstream command names and syntax are unchanged. [CLI](docs/CLI.md), [CLI_REFERENCE](docs/CLI_REFERENCE.md) | Unit test of every cell of the table; sim of the same line over USB, LoRa and MQTT |
| Firmware identity | `v1.18.0` | `ver` and `get mecon.version` print `v1.18.0-mecon.<tag>` from `git describe` (development builds add `+N.g<hash>`, dirty trees `+dirty`); the upstream base stays first, so an app reading the leading `v1.18.0` still works. The device-info frame carries the short form in its 20 bytes. [VERSIONING_AND_RELEASES](docs/VERSIONING_AND_RELEASES.md) | Sim and unit tests |

Measured at the simulated channel, as the largest share of any sliding hour spent on the air (release 0.1.1):

| Case | Stock 1.18.0 | This firmware |
|---|---|---|
| 869.525 MHz, BW 250 (10 % band) | 78.1 % | 9.85 % |
| 868.1 MHz (1 % band) | 88.8 % | 0.978 % |
| Power-cycled every 5 min, 1 % band | 84.0 % | 0.81 % |

The price of the duty-cycle window is a wait after boot: the first transmission can be delayed by up to 30 s at any limit, and the first advert came after about 152 s on a 1 % channel and 32 s on a 10 % channel.

### Behaviour that differs for users

- Wi-Fi Companion builds: the app can message but cannot run the CLI, export keys or factory-reset over Wi-Fi. Use USB.
- Boards without a display (default BLE PIN): CLI, key export and factory reset over BLE are refused. Use USB.
- Remote Companion CLI: settings changes and `poweroff` are refused.
- Repeaters and room servers on the default admin password cannot be administered over the air until a password is set over USB (`password <new>`).
- The standalone Companion runs BLE or Wi-Fi, not both.

## Using this in other projects

The firmware does not require MECON. The pieces below are written to be reused or borrowed, and most of the decision logic is separated from the platform so it can be tested on a laptop.

| Piece | Where | What it is | Dependencies |
|---|---|---|---|
| Regulatory duty-cycle floor | `src/helpers/RegulatoryDutyCycle.*` | Table of EU 863-870 MHz and 433 MHz sub-band limits and a function from centre frequency and bandwidth to the strictest limit touched | None (no Arduino, no Dispatcher) |
| Sliding-window airtime limiter | `src/Dispatcher.*` | Replaces MeshCore's token bucket; 121 bins of 30 s, reboot-safe seeding | MeshCore `Dispatcher` |
| `AtomicFile` | `src/helpers/AtomicFile.h` | Temp file, flush, read-back check, swap; handles SPIFFS (no rename-over) and LittleFS | Arduino `File` API |
| CLI privilege table | `src/helpers/cli/CliPolicy.h`, `CliRegistry.h`, `CliEngine.h` | Caller x posture x command-class table with fail-closed defaults | Pure logic, no file system or clock |
| Posture and audit ring | `src/helpers/Posture.h`, `AuditRing.h` | Posture decisions and the fixed-size folding audit log | Pure logic |
| `WifiCore` / `MqttCore` | `src/helpers/wifi/WifiCore.*`, `src/helpers/mqtt/MqttCore.*` | Platform-free decision cores (profile choice, cooldowns, hold-down, discovery rules) tested against a fake radio or network | `stdint` and the profile stores |
| `ContactStore` | `src/helpers/ContactStore.*` | Persistent contact store with a RAM cache, behind `BaseChatMesh`'s existing API | MeshCore contact types |
| Simulated-board harness | `variants/sim/`, `sim/` | The real Companion and Repeater code on Linux, an ether broker, fault-injecting storage, simulated Wi-Fi and a Mosquitto-based MQTT environment, a Python test client | Docker, PlatformIO, Python, Mosquitto, OpenSSL ([SIMULATION](docs/SIMULATION.md)) |
| Mutation script | `sim/mutate.sh` | Breaks one rule at a time and requires the tests that guard it to fail; run in CI one mutant per job | The harness |

To build on or borrow from the code:

- **Fork or merge:** the history is MeshCore's; [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md) is the register of every change to an upstream file, so you can see the patch surface before you merge, and cherry-pick one concern. [`docs/UPSTREAM_AND_PORTING.md`](docs/UPSTREAM_AND_PORTING.md) covers the upstream relationship.
- **Build:** inside the container, see [Getting started](#getting-started). Board environments are the upstream ones; 594 of the 618 compile (the 24 that fail also fail on stock 1.18.0 and are listed in `ci/known-upstream-failures.txt`).
- **Licence:** MIT. The licence file keeps the upstream MeshCore copyright (Scott Powell / rippleradios.com) and states the additions' authorship separately; see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md). Third-party libraries (for example PubSubClient 2.8.0, MIT) keep their own licences.
- **Contribute back:** generic improvements are meant to go upstream where practical. Nothing here is submitted upstream yet.

## Status and hardware

- **Releases:** 0.1.0 (pinned baseline, closed management and storage gaps), 0.1.1 (duty cycle, postures), 0.1.2 (Wi-Fi and MQTT controllers) are pre-releases of source. Work merged since 0.1.2 and described above (MQTT CA anchors and SNI, non-blocking connect, connectivity mode and screen, adaptive contact memory, unified CLI and CLI over MQTT, atomic blobs and ACL, firmware identity string) is not yet in a tagged release. Release notes: [GitHub releases](https://github.com/hoejriis/mecon-firmware/releases).
- **Test evidence (at 0.1.2, the last tagged release):** 134 simulated-board tests, 193 native unit tests, 131 mutants each required to be caught, 16 Heltec V3/V4/T1000-E/Wio Tracker L1 environments compiled on every pull request, and 594 of 618 board environments compiled. Later changes add tests; CI shows current counts.
- **Hardware:** none verified. Target boards for the first hardware sessions are the Heltec V3 and V4 (Companion, Repeater, Room server, and the standalone Companion). The T1000-E and Wio Tracker L1 compile in CI. [SUPPORTED_HARDWARE](docs/SUPPORTED_HARDWARE.md) is the target role matrix; [BETA_BOARDS](docs/BETA_BOARDS.md) describes a separate private beta line and not this repository's builds.
- **Not covered:** BLE is not simulated, so BLE trust and pairing are checked by unit tests of the decision logic only. RP2040 has no MQTT. KISS modem, sensor and secure-chat roles have no duty-cycle enforcement.

## Getting started

Build and test inside the container, not on the host.

```sh
docker build -t mecon-firmware-dev .
sim/run.sh pio run                          # builds the simulated-board environments (and any you select with -e)
sim/run.sh python3 -m pytest sim/tests -q   # the simulated-board suite
sim/run.sh pio test -e native               # native unit tests
sim/run.sh pio run -e Heltec_v3_companion_radio_standalone   # compile a target board
```

Flashing: build the environment for your board and flash it with PlatformIO (`pio run -e <env> -t upload`) or from the produced image. **Nothing in this repository has been run on hardware yet.** Flash a spare board, keep USB access for recovery (several management paths are intentionally refused on BLE with a default PIN and over Wi-Fi), and report what you see. The factory reset over USB always works and returns a device to Normal posture.

## Documentation

- [Documentation index](docs/README.md)
- Using it: [CLI reference](docs/CLI_REFERENCE.md), [screens and buttons](docs/UI.md), [Wi-Fi](docs/WIFI.md), [MQTT](docs/MQTT.md), [postures](docs/STANDALONE_POSTURES.md), [connectivity mode](docs/CONNECTIVITY_MODE.md), [contact memory](docs/CONTACTS.md), [command-line engine](docs/CLI.md)
- Developing: [simulated-board harness](docs/SIMULATION.md), [upstream delta register](docs/UPSTREAM_DELTA.md), [versioning and releases](docs/VERSIONING_AND_RELEASES.md)

## MECON integration (optional, later)

[MeshContinuum](https://github.com/hoejriis/MeshContinuum) (MECON) is the sister project: a backend and browser Reader for managed fleets. This firmware is designed to be usable without it and is not coupled to it; any backend can implement the public contract. The MECON-specific stage (Stage B: deployment identity, enrolment, signed management, MECON MQTT topics, observations, OTA) is **not implemented** in this repository. Its specifications are target designs, not shipped behaviour:

- [Clean refounding on MeshCore 1.18](docs/CLEAN_REFOUNDING_1_18.md): Stage A (standalone, what is built) and Stage B (MECON, later)
- [Device contracts](docs/contract/README.md), [product target](docs/PRODUCT_TARGET.md), [security model](docs/SECURITY_MODEL.md), [integration guide](docs/INTEGRATION.md)
- [Contract migration notes](docs/CONTRACT_MIGRATION_NOTES.md), [backend migration](docs/BACKEND_MIGRATION.md)

The first version planned to serve a MECON fleet is 0.8.0. No dates are announced. A separate private beta firmware (the 0.9x line) informs the design; its version numbers do not order against this repository's. The shared maturity reference is the [capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md).

## Security

Report vulnerabilities privately; see [`SECURITY.md`](SECURITY.md). Contributions: [`CONTRIBUTING.md`](CONTRIBUTING.md).
