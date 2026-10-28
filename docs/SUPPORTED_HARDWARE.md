# Supported hardware and roles

## Verification policy — owner decision, 25 October 2026

Only four test boards are available: **Heltec V3, Heltec V4, Seeed Wio Tracker L1 and SenseCAP T1000-E**. Public firmware must be hardware-verified on these boards for their applicable roles, transports and advertised capabilities. Availability does not mean a check has passed; the evidence matrix records actual board, role, commit, test and result. T1000-E remains compile-only until #1 supplies hardware evidence.

All other repository board environments receive **software verification only**: compilation, size checks where configured, and applicable unit/simulation/conformance tests. Publish the checks actually run and their results, including exclusions and known upstream failures. A build pass proves compilation, not runtime behaviour; a shared-stack test on another board is not hardware evidence for this board. No additional board purchase or unavailable-board UAT is required to close software-only work.

Public documentation must distinguish **hardware verified**, **software verified (with checks specified)**, **failed**, and **not verified**, at board/role/feature level. Never silently promote a board or all its features from partial evidence. The four-board hardware gate applies to applicable capabilities as they are implemented, not unsupported features. This policy supersedes older requests to wait for hardware on other boards. Release timing remains separate.

> **Public availability:** this repository publishes source-only pre-releases (0.1.x) with no binaries; the tags are on ESP-IDF 4.4 and have no device interface. Current `main` is ahead of them. Hardware verification has started on a Heltec V3, a Heltec V4 and a Wio Tracker L1: what was checked on which board and build, and what is still only compiled, is in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). No target has yet passed the full hardware gate below. The 18 Heltec V3/V4, T1000-E and Wio Tracker L1 environments (Companion, Repeater, Room server and, on the Heltec boards, the standalone Companion) compile on every pull request, and compiling is not support. Read the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for the private beta versus public distinction.

The public line is built on released **MeshCore 1.18.0 from upstream `main`**. The public line and the private 0.9x reference line are separate lines; their version numbers do not order across each other. Release timing is a separate decision. A target is called supported only after its derived build passes its real-hardware gate.

Roles are described by capability rather than by defining the product as ESP32/Wi-Fi/MQTT firmware. The table says what builds on `main` today, from the CI target matrix and [PLATFORM](PLATFORM.md); the evidence for each is in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). The managed-device capabilities still planned (identity and authority, updates over IP, USB and BLE, management over LoRa) are in [CLEAN_REFOUNDING_1_18](CLEAN_REFOUNDING_1_18.md) and [ARCHITECTURE](ARCHITECTURE.md#management-over-lora-planned).

| Hardware | Builds on `main` | Hardware evidence | IP/Wi-Fi | USB | BLE |
|---|---|---|---|---|---|
| Heltec V3 | Companion (USB, BLE, Wi-Fi, standalone), Repeater, Room server | Real rows | Yes | Yes | Companion: NimBLE; the standalone Companion runs BLE or Wi-Fi mode, one at a time |
| Heltec V4 | Companion (USB, BLE, Wi-Fi, standalone), Repeater, Room server | Real rows | Yes | Yes | Companion: NimBLE; the standalone Companion runs BLE or Wi-Fi mode, one at a time |
| Seeed Wio Tracker L1 | Companion (USB, BLE), Repeater | One Companion row (contacts capacity) | No | Yes | Companion: Bluefruit |
| SenseCAP T1000-E | Companion (USB, BLE), Repeater | Compiled only ([#1](https://github.com/hoejriis/mecon-firmware/issues/1)) | No | Yes | Companion: Bluefruit |

V3 and V4 are first-class targets and build on Arduino-ESP32 3.1.3 / ESP-IDF 5.3 with NimBLE today ([PLATFORM](PLATFORM.md)); this is current, not a future target. The T1000-E and the Wio Tracker L1 are the non-ESP (nRF52) targets. They are not Wi-Fi/MQTT devices. The Heltec V4 Repeater builds as a stock-derived Repeater; it has no MQTT Observer role, and the device interface is not compiled into it. The device interface is compiled into the V3 Repeater, Room server and standalone Companion and the V4 standalone Companion only, and not into any nRF52 build ([DEVICE_INTERFACE](contract/DEVICE_INTERFACE.md)).

Only the four boards above are in scope for hardware verification. Every other board environment is software-verified only, and a board being available is not evidence that a check passed.

**Updates.** No managed update path is built in this repository. Signed updates over IP, USB and BLE are planned (Issue #110, with the T1000-E in #1); update over IP applies only to IP-capable hardware, and USB and BLE delivery are separate work, not a button or an undocumented fallback. LoRa image delivery is later, MECON-specific work (Issue #123). Real bootloader limits per board are documented when that work lands.

## Capability layers

A board may advertise any applicable combination of:

- **Core:** identity/capabilities, common configuration and health/status. Built as device interface v1; messaging semantics, inventory and resilience are not part of it;
- **USB:** direct Reader/configuration/recovery;
- **BLE:** direct Reader/configuration/messaging;
- **IP:** IP networking, broker discovery and MQTT/backend connectivity;
- **display/attention:** screen/framebuffer or target-specific LED/buzzer/haptic notification behaviour;
- **managed update:** target-specific supported OTA/DFU mechanism (planned, Issue #110).

A client must discover capabilities rather than infer them from board name or firmware version.

## Heltec Companion

The Companion image preserves MeshCore 1.18 behaviour and adds ordered Wi-Fi profiles (up to ten), MQTT with failover, the unified CLI and, in the standalone build, the device interface over USB, BLE and MQTT. BLE uses NimBLE on Arduino-ESP32 3.1.3 / ESP-IDF 5.3. The standalone Companion runs BLE or Wi-Fi at a time ([CONNECTIVITY_MODE](CONNECTIVITY_MODE.md)).

Companion contact capacity is **not fixed by the contract**. It follows free file-system space and heap, is measured after the pioarduino/Arduino-ESP32 3.x/IDF5 migration, and is reported by the running build ([CONTACTS](CONTACTS.md)).

## Heltec Repeater

The Repeater image preserves MeshCore 1.18 repeating behaviour, adds the unified CLI with postures, and on the V3 the device interface on the USB console. USB remains the recovery/configuration path. BLE support is explicitly advertised rather than assumed.

## SenseCAP T1000-E Companion

The T1000-E builds a Companion (USB and BLE) and a Repeater. On `main` it is compiled only; hardware evidence is tracked in Issue #1. The device interface is not compiled into it. It has no IP requirement: no Wi-Fi profiles, MQTT client, mDNS broker discovery or local/cloud broker failover. ESP32-specific IDF5/pioarduino/NimBLE requirements do not apply to this target.

## Seeed Wio Tracker L1

The Wio Tracker L1 (nRF52840) builds a Companion (USB and BLE) and a Repeater. The one hardware row is the Companion's contacts capacity of 1000 on its QSPI flash. It has no Wi-Fi or MQTT client. The device interface is not compiled into it.

## Reference firmware (private)

This note describes the private 0.9x reference firmware, not this repository's builds, and is not evidence for them. The private beta ships, among other things: Deployment-signed management, security posture, identity rotation/export, BLE bonds and an opaque continuity slot; on the T1000-E an update through the USB bootloader after a Deployment-signed offer (BLE DFU compiled out, no over-the-air update); on the Wio repeater RF management with Deployment-signed writes, a signed status beacon, USB DFU and a LoRa-staged update that is stored but not activated by the radio; the ESP32 Repeater Observer on the V3 only; and, on the V3 and V4 gateways, a fixed contact limit of 50 (350 on the T1000-E). Its hardware evidence is limited-scope bench testing in the private line. Its behaviour is summarised in [CONTRACT_MIGRATION_NOTES](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line).

## Moving a board from the private 0.90.x line

The private reference firmware stores its Deployment record in files with the same names and frame header as this firmware's, but in a different layout. A board flashed from 0.90.x to this firmware with its file system kept therefore reads them as a **corrupt record**. That is deliberate: it fails closed, and it never treats another firmware's authority state as its own. Until the record is cleared, the board refuses provisioning, signed requests and unsigned configuration writes, Wi-Fi settings included.

To move a board:

1. Flash this firmware over the USB cable.
2. Over the same cable, in Normal posture, send `deployment.reset`. It needs no signature, because a corrupt record may be reset from the cable (contract: [Management authority](contract/AUTHORITY.md), section 11). It removes the old record and keeps the node key, `device_id`, contacts and channels.
3. Reboot the board. Right after the reset, the heap can stay fragmented until the next boot: a largest free block of about 15 KB, against about 30 KB normally.
4. Configure and provision it as a new device. The first provisioning is accepted unsigned over the USB cable only.

An erase over the cable (a factory reset) is the alternative. It removes everything, the node key included.

This firmware does not recognise or import the private line's record. That decision may be revisited with MECON's own fleet migration.

## Other boards

All other board environments are software-verification targets under the policy above. Use the current CI matrix for the environment count and actual results; describe build-only evidence as such. No hardware verification is claimed for these boards.

## Device UI / attention

Supported display targets retain the stock 1.18 front screen/button model with only the documented additions: the firmware version at boot, a connectivity page (mode, posture, PIN state, Wi-Fi and MQTT state), BLE PIN while unconnected, and DM/favourite wake filtering. See [UI](UI.md) and [CONNECTIVITY_MODE](CONNECTIVITY_MODE.md) for what is built.

Non-display targets omit display/framebuffer requirements. If a target exposes LEDs, buzzer or haptics, notification mappings are target-specific but should follow the same attention principle: DMs/favourited messages may alert; ordinary observed/public traffic should not generate attention merely because it was received.

## Hardware gate

For each of the four boards above, the capabilities that exist on that board are verified on the hardware: stock-derived role behaviour, USB, BLE/NimBLE where the board has it, Wi-Fi profiles, broker discovery and failover, the device interface where compiled in, configuration/recovery, reconnect behaviour, memory headroom and UI. The gate covers a capability once it is implemented, not before; managed update is added when Issue #110 lands. Compilation alone is not a hardware gate. [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md) records what has passed; it does not show mDNS broker discovery or the device interface over MQTT on a V3.

Experimental targets have their own issue-defined MVP gate and are not added to the supported matrix until that gate passes on real hardware.
