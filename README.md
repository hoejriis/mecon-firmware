# mecon-firmware

**MeshCore devices that are easier to manage, monitor and maintain.**

mecon-firmware keeps ordinary MeshCore radio operation and adds secure configuration, diagnostics and firmware updates. You can manage a nearby device over USB or Bluetooth, a connected device through MQTT, or a remote node over direct or private-channel LoRa. Use it on its own, or connect it to any application that implements the public interface.

It is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore), built on the upstream **v1.18.0** release (commit `81dbae5`, tagged `companion-v1.18.0` and `repeater-v1.18.0` upstream). Stock apps and stock nodes interoperate with it, within the [restrictions listed below](#stock-meshcore-1180-and-mecon-firmware-compared).

## What you can do with it

- **Diagnose a remote repeater without travelling to it.** From a manager, read its health, radio figures, airtime and last reset reason over MQTT, or over LoRa where it has no IP link. LoRa carries compact figures at a low rate. Logs and screen snapshots need a higher-capacity link.
- **Change connectivity settings with a way back.** A Wi-Fi, broker or radio change sent remotely takes effect on trial. If the manager cannot confirm it in the time it asked for, because the new settings lost contact, the device returns to the old ones. This covers a change that loses contact, not every fault, so keep USB access to a device you depend on.
- **Use a manager and keep MeshCore.** The device keeps its ordinary radio role, and a Companion keeps working with the stock app, within the documented restrictions. Managed workflows need an application that implements the public contracts. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) is one, and its Reader needs its Backend.

## Why use it

- **MeshCore keeps working on its own.** Messaging, repeating and the stock phone apps need no Internet, account or management service.
- **One management model across supported transports, with consistent authorization and explicit capability limits.** An operation means the same thing over USB, Bluetooth, MQTT and LoRa, and one privilege table decides it. A transport never grants a permission by itself. Which operations a path carries does differ: LoRa carries no secrets or subscriptions, for example, and a board has only the links it has. A device lists what it can do, and [what differs, and why](docs/SUPPORTED_HARDWARE.md#what-differs-and-why) says which limits come from hardware, from privacy and airtime rules, and from project decisions.
- **A manager can send, collect and supply messages.** On a Companion, an authorized manager can send a native direct or channel message and follow what became of it, collect what was received, administer contacts and channels, and hand a missed message to the local stock app without transmitting it. A manager can also leave a device a bounded standing instruction, such as a report every few hours. The device chooses no gateway or route by itself.
- **You can see what the device is doing.** Battery and power, connectivity, radio health and airtime, logs, observations, inventories and diagnostic screen snapshots are all available. Over LoRa, the device sends compact telemetry at a lower rate.
- **Changes can be undone, and recovery is explicit.** Signed firmware updates go over IP and USB, and over Bluetooth on the Heltec Companions; the nRF52 boards take them over USB only, by owner decision. A remote configuration change reverts unless it is confirmed, and each reset says exactly what it clears.
- **Security is on by default.** Untrusted links cannot manage the device, default passwords and PINs do not open remote administration, and passwords are never read back. An optional Hardened posture tightens this further.
- **No vendor is required.** The contracts are open, so any conforming manager can use them. No hosted Backend, broker or domain is needed.
- **Each board does what it can.** Every board and role reports its own capabilities. A board without Wi-Fi or a screen does not pretend to have one.

## Stock MeshCore 1.18.0 and mecon-firmware compared

The "Stock" column describes upstream MeshCore 1.18.0 as released. The "mecon-firmware" column is the finished product: what this project adds or changes. Not all of it is available yet: see [availability](docs/AVAILABILITY.md). Everything not listed behaves as stock. Each file this project changes upstream is listed in the [upstream delta register](docs/UPSTREAM_DELTA.md).

### Capabilities

| Area | Stock MeshCore 1.18.0 | mecon-firmware | Benefit / practical limit |
|---|---|---|---|
| Device management | Text CLI on each role. Companion settings through app frames. No structured interface | One versioned device interface over USB, BLE, MQTT and LoRa. It covers capability discovery, typed settings with a schema, jobs with results that survive a disconnect or reboot, and subscriptions. It sits beside the native frames and CLI and adds no second settings store ([device interface](docs/contract/DEVICE_INTERFACE.md), [operations](docs/contract/OPERATIONS.md)) | Any manager can drive any conforming device. Only named operations exist: there is no raw shell |
| Telemetry | Stats frames and CLI readouts on request | Streams for health, power, radio (counters, noise floor, airtime, duty cycle), runtime and resets, connectivity, observations, inventory, logs, environment sensors, location and screen snapshots, and on a Companion the replies to its LoRa management requests (`rf`). Each stream has a period, a lifetime and bounded queues. Over LoRa, compact low-rate telemetry ([telemetry](docs/contract/TELEMETRY.md)) | Problems show up without someone at the device. A board without a sensor, battery reading or screen omits that stream rather than sending zeros. A slow consumer loses events, never radio time |
| Remote management over LoRa | Remote admin by password login to a repeater or room server | Authenticated device-interface operations point to point, or over a private management channel, under the same authorization as every other path. Channel membership grants nothing by itself ([management channel](docs/contract/MANAGEMENT_CHANNEL.md), [LoRa management](docs/contract/LORA_MANAGEMENT.md)) | A node with no IP link can still be managed. Airtime limits apply, so LoRa carries operations and compact, low-rate telemetry. Large logs and screen snapshots are not sent over LoRa |
| Wi-Fi | One network | Up to 10 prioritised profiles. The device fails over when a network is lost, a password is wrong or a link keeps dropping, and returns to a better network only after a hold-down ([Wi-Fi](docs/WIFI.md)) | Moves between sites without a reflash. Stock `wifi.*` settings map to the first profile and load unchanged |
| MQTT | None | Up to 6 broker profiles, one session at a time, with failover by priority or by the role a manager assigns (local, primary, secondary). Brokers can be static or discovered on the LAN by mDNS. TLS trusts a pinned certificate or a private CA. WebSocket transport is supported ([MQTT](docs/MQTT.md)) | Works with your own broker. A discovered broker is used only if it passes TLS verification. On the Heltec Companion, Repeater and Room server. Not on the nRF52 boards, which have no Wi-Fi |
| BLE or Wi-Fi | Separate BLE and Wi-Fi builds | The standalone Heltec Companion carries BLE, Wi-Fi, MQTT and USB in one image. It runs BLE or Wi-Fi, switched by command or a two-step button press ([connectivity mode](docs/CONNECTIVITY_MODE.md)) | One image covers both uses. The two radios never run at the same time, a deliberate choice that keeps memory headroom. USB works in both modes |
| Contacts | A fixed array in RAM (350 on the Heltec V3), saved as one file | A persistent store with a RAM cache sized from free heap. Favourites stay resident. When the store is full, the least useful non-favourite is evicted first. A stock contact file migrates on first boot ([contacts](docs/CONTACTS.md)) | Capacity follows free flash and is never below the stock count while space allows. The T1000-E Companion keeps the stock array |
| Messages through a manager | Companion frames for the app | A manager sends one native direct or channel message and reads its outcome: queued, held for airtime, transmitted, acknowledged, timed out or failed. It collects received messages beside the native queue, with what each field proves. It can hand a missed message to the stock app, marked and never transmitted ([messages](docs/contract/OPERATIONS.md#6-messages)) | A manager and a stock app share a Companion, and neither takes the other's messages. An acknowledgement shows the recipient's node had the message, and a timeout does not prove it did not. Companion only. The copies are small and in RAM, so a reboot loses them: this is not delivery or history |
| Contacts and channels through a manager | Companion frames and app | Operations to list, add, change, protect and remove contacts and channels, on the native tables with no second copy. A Repeater or Room server holds one management slot ([operations](docs/contract/OPERATIONS.md#51-device-interface-operations)) | A manager can prepare a device without the app. A shared secret or channel key is never read back. The device keeps no favourites database of a manager's |
| Standing instructions | None | A manager can ask for a compact report now or on a schedule, every 15 minutes to 30 days, to a named destination, with at most four schedules at once. Every report goes through the duty window. The device runs it while the manager is away | Monitoring continues without a live connection. No scripts or arbitrary commands, no unlimited catch-up after a reboot, no rule engine |
| Firmware updates | Board bootloader (USB DFU, ESP32 web or serial flashing), unsigned | Signed release manifests, and an install that a Deployment member, or the cable, has authorised. Delivery over IP, USB and BLE, depending on the board. Preflight checks board, layout, role and a security floor. ESP32 boards boot the new image on trial and roll back if it is not confirmed ([updates](docs/contract/UPDATES.md)) | No unsigned image installs through the interface. The board's own bootloader still accepts any image from someone with physical access (see device security below). On nRF52 boards, an update is written by the bootloader over USB. It is checked by its reported version and has no previous image to return to |
| Version reporting | `v1.18.0` | `v1.18.0-mecon.<tag>`, with the upstream base first ([versioning](docs/VERSIONING_AND_RELEASES.md)) | Apps that read the leading `v1.18.0` still work |

### Regulatory airtime

| Area | Stock MeshCore 1.18.0 | mecon-firmware | Benefit / practical limit |
|---|---|---|---|
| Duty-cycle limit | Token bucket set by the airtime factor: 50 % by default, 10 % at the strictest setting. A sliding hour can carry close to twice the limit | Rolling one-hour window, in 30 s bins. The limit is the stricter of the user's setting and a floor for the configured channel | Within the listed bands, the firmware holds the configured channel to its duty-cycle limit, even if the user's own setting is higher |
| Bands | No band awareness | EU 863–870 MHz sub-bands (0.1 %, 1 % or 10 %) and 433.05–434.79 MHz (10 %). The whole occupied channel (frequency ± half the bandwidth) is judged, and the strictest band touched applies. Other regions' bands get their own airtime rules. | Matches the channel you actually use. `get dutycycle` shows the band, limit, floor and airtime used |
| Reboots | Budget refills at boot | Each boot starts as if the previous hour had used the full limit | A reboot loop cannot exceed the limit. In return, the first transmission after boot can wait up to about 30 s, and a node on a 1 % channel sends its first advert later than stock |
| Roles | Every role, via the airtime factor | Every role that transmits on LoRa: Companion, Repeater, Room server, Sensor, secure chat and the KISS modem. The KISS modem refuses to transmit on a channel it cannot classify | On the Sensor, `get dutycycle` replies as stock |

These are firmware controls, not a certification. They cover airtime only, not transmit power or antenna gain, and only the bands the firmware knows. You remain responsible for a legal radio configuration in your country.

### Device security

| Area | Stock MeshCore 1.18.0 | mecon-firmware | Benefit / practical limit |
|---|---|---|---|
| Trusted and untrusted links | The Wi-Fi/Ethernet TCP companion port serves every frame to any client, including the CLI, key export and import, PIN change and factory reset | USB is trusted. BLE is trusted only when paired with a PIN other than 0 or the default 123456. The TCP port and a default-PIN BLE link are untrusted: they get messaging and reads, management frames are refused with error 7 (`ERR_CODE_NOT_PERMITTED`), and reads there carry no PIN or channel secret. Anything that does not declare trust is untrusted ([command-line engine](docs/CLI.md)) | The phone app still messages over Wi-Fi. Management over those links needs USB or a BLE link paired with a real PIN |
| Command authority | Whoever reaches the CLI may use it | A device can join one Deployment, enrolled over the cable. A Deployment authority key signs the list of managers. Each manager signs its requests (Ed25519), and a strictly increasing per-manager sequence, saved before the request runs, refuses replays. Results and events are signed by the device. A broker login, TLS session, BLE pairing, USB cable, LoRa route or channel key is a transport, never authority ([authority](docs/contract/AUTHORITY.md)) | A relay (a broker or a bridge app) cannot forge or replay a command. Standalone devices with no Deployment keep the posture rules below. Once enrolled, a change needs a signature even on the cable, which keeps reads, reboot and the recovery operations |
| Default passwords and PINs | Anyone who sends the compiled-in `password` is admin over the air. Debug builds print the attempted password | A Repeater or Room server on the default or an empty admin password refuses remote admin in both postures. Attempted passwords are never logged | Set a password over USB (`password <new>`) before administering over the air |
| Remote Companion CLI | A flagged contact may run any command, including `set pin`, radio settings and `poweroff` | Reads and `reboot` only, off in Hardened. A remote timestamp of 0 is no longer taken for the local console | A contact flag cannot take over a phone's radio |
| Secrets | `get wifi.pwd` returns the Wi-Fi password in clear text | Wi-Fi and broker passwords are write-only on every transport. Telemetry and logs carry no secret. A secret is returned only by its own operation to a trusted local caller, such as the private key by an explicit export, which Hardened refuses ([Wi-Fi](docs/WIFI.md)) | A stolen session or log does not leak credentials |
| Postures | None | **Normal** (the default) and **Hardened**. Hardened does not start BLE with a default PIN or the TCP companion server, keeps remote admin read-only plus `reboot`, refuses plain MQTT brokers, and refuses key export even on USB. Raising the posture works from a trusted link. Lowering it works only on the cable. A persistent audit log records posture changes, refusals, failed logins and resets, and never a secret ([postures](docs/STANDALONE_POSTURES.md)) | A device can be locked down without a manager. By design, a Hardened Companion in Wi-Fi mode has no app transport: use USB |
| Physical access | Bootloader accepts any image | Unchanged, by deliberate choice: neither the ESP32-S3 nor the nRF52 bootloader checks signatures, and no secure boot or flash encryption is configured | Someone with the device and a cable can install any image, which inherits the device's keys and records. Signed updates protect remote paths, not a device in hostile hands |
| Input hardening | A short TRACE packet reads past the packet buffer. Long `set radio`, `tempradio`, `sensor set` and `extra.sf` arguments overflow a 132-byte buffer | Short TRACE payloads are dropped, hop entries are bounds-checked and the copies are bounded | |

### Storage and recovery

| Area | Stock MeshCore 1.18.0 | mecon-firmware | Benefit / practical limit |
|---|---|---|---|
| Saves | Settings, identity, channels, contacts and access lists are rewritten in place. After a power cut, the loader accepts a truncated file | Each file is written to a temporary copy, read back and checked (length and CRC), then swapped in. Boot completes or discards an interrupted swap. A damaged identity is set aside, not replaced | A power cut during a save loses that save, not the device's state |
| Recovery | Factory reset | A factory reset over USB always works and returns the device to Normal. Resets have explicit, named scopes, so clearing one kind of state does not clear the others. A client locked out by a clock that ran ahead recovers with a device restart ([recovery](docs/STANDALONE_POSTURES.md#recovery)) | Keep USB access to a device you depend on: it is the recovery path |

## Hardware

Four boards are hardware test targets: **Heltec V3, Heltec V4, Seeed Wio Tracker L1 and SenseCAP T1000-E**. The Heltec boards add Wi-Fi and MQTT. The nRF52 boards (Wio Tracker L1, T1000-E) work over USB and BLE and have no IP. Other boards in the tree build from the same source and are checked in software only, which is not a claim of physical-board support. [Supported hardware](docs/SUPPORTED_HARDWARE.md) shows what each board and role can do, and [hardware evidence](docs/HARDWARE_VERIFIED.md) records what has been checked on real boards.

## Getting it

> **Availability.** These pages describe the finished product. The latest pre-release is [v0.3.6](https://github.com/hoejriis/mecon-firmware/releases/tag/v0.3.6), signed like v0.2.0, the first. No release is supported yet, and some features have run on few boards. Later MECON features are [outside these pages](docs/AVAILABILITY.md#what-only-a-manager-brings). [Availability](docs/AVAILABILITY.md) says what you can get today, and the [release notes](https://github.com/hoejriis/mecon-firmware/releases) say what each release contains.

[Install and set up a device](docs/GETTING_STARTED.md) goes from choosing a board, through keeping what is on it, to a checked device and one useful result. Today that means building from source. Contributors' test commands are in [contributing](CONTRIBUTING.md#build-and-test).

## Documentation

- [Documentation index](docs/README.md)
- Using it: [CLI reference](docs/CLI_REFERENCE.md), [screens and buttons](docs/UI.md), [Wi-Fi](docs/WIFI.md), [MQTT](docs/MQTT.md), [postures](docs/STANDALONE_POSTURES.md), [connectivity mode](docs/CONNECTIVITY_MODE.md)
- Integrating a manager: [contract index](docs/contract/README.md), [device interface](docs/contract/DEVICE_INTERFACE.md), [authority](docs/contract/AUTHORITY.md), [updates](docs/contract/UPDATES.md)
- Developing: [simulated-board harness](docs/SIMULATION.md), [upstream delta register](docs/UPSTREAM_DELTA.md), [runtime platform](docs/PLATFORM.md), [contributing](CONTRIBUTING.md)

## Managers and MeshContinuum

Any application that implements the public contracts can manage mecon-firmware devices. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) is one such manager: a Backend with a browser Reader, which needs that Backend and bridges USB and BLE to it.

The firmware offers generic capabilities that a manager asks for: sending, collecting messages and outcomes, contact and channel administration, local delivery and standing instructions. Choosing a gateway, deciding which history to catch up on and setting a health policy belong to the manager. MECON connected messaging, backfill, RF health monitoring, a human-readable `ping` on the management channel, delegated receive identities on a Companion and firmware-image delivery over LoRa are later work built on those capabilities, and are outside the product described here. [What the firmware does, and what a manager does](docs/INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does) draws the line.

## Licence and security

MIT, with the upstream MeshCore notice kept. See [licensing and provenance](NOTICE.md). Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md). [Published advisories](https://github.com/hoejriis/mecon-firmware/security/advisories) list the vulnerabilities found so far, their severity, the versions affected and the release that carries each fix.
