# Supported hardware and roles

> **Public availability:** no firmware releases are published here yet. Read the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for the private beta versus public target distinction. Every table below describes the target, not accepted downloadable builds.

The public line starts with **mecon-firmware 0.8.0**, built on released **MeshCore 1.18.0 from upstream `main`**. The public 0.8.x line and the private 0.9x reference line are separate lines; their version numbers do not order across each other. A target is called supported only after its derived build passes its real-hardware gate.

MECON uses capability profiles rather than defining the product as ESP32/Wi-Fi/MQTT firmware. The roles below are the **intended 0.8.0 set, subject to acceptance**; non-ESP boards implement portable MECON Core plus the transports/features their hardware actually provides.

| Hardware | Status | Companion | Repeater | IP/Wi-Fi | USB | BLE | MQTT |
|---|---|---:|---:|---:|---:|---:|---:|
| Heltec V3 | 0.8.0 target | Companion Gateway | ESP32 Repeater Observer | Yes | Yes | Companion: mode `mqtt` or `ble`, not both; repeater: capability-dependent | one active session |
| Heltec V4 | 0.8.0 target | Companion Gateway | ESP32 Repeater Observer (new in 0.8.0) | Yes | Yes | Companion: capability-dependent | one active session |
| SenseCAP T1000-E | 0.8.0 target, limited scope | BLE Companion | No | No | Yes | Yes | No direct client |
| Seeed Wio Tracker L1 | 0.8.0 target, limited scope | No | RF repeater (nRF52) | No | Yes (console) | No | No (managed over RF and USB) |

V3 and V4 are first-class targets from initial bring-up. The T1000-E and the Wio Tracker L1 are the non-ESP (nRF52) targets: both are built in every private beta release and bench-tested in limited scope, and they prove the Core architecture beyond ESP32. They are not Wi-Fi/MQTT devices. The Heltec V4 Repeater Observer has no build in the private beta; 0.8.0 adds it.

## Capability layers

A board may advertise any applicable combination of:

- **MECON Core:** identity/capabilities, common configuration, health/status, messaging semantics, applicable inventory, resilience and common management operations;
- **MECON USB:** direct Reader/configuration/recovery;
- **MECON BLE:** direct Reader/configuration/messaging;
- **MECON IP:** IP networking, broker discovery and MQTT/backend connectivity;
- **display/attention:** screen/framebuffer or target-specific LED/buzzer/haptic notification behaviour;
- **managed update:** target-specific supported OTA/DFU mechanism.

A client must discover capabilities rather than infer them from board name or firmware version.

## Heltec Companion

The Companion image preserves MeshCore 1.18 behaviour, adds three ordered Wi-Fi profiles, local-first/cloud-fallback MQTT, MECON management/observations, and direct USB/BLE where advertised. BLE uses NimBLE (the private 0.9x gateways already do, on the older Arduino-ESP32 2.x / ESP-IDF 4.4 platform; the pioarduino / IDF 5 migration is still a target). On the V3, connectivity mode `both` is not available: a measured 9 KB of heap remains in that mode, so the V3 runs `mqtt` or `ble`.

Companion contact capacity is **not fixed by this target contract**, and not at the private MVP's historical 32/64 values. It is set from the measured memory budget after the pioarduino/Arduino-ESP32 3.x/IDF5 migration and published in status/manifests. For reference, the 0.9x gateways enforce 50 contacts on V3 and V4, and the T1000-E 350.

## Heltec Repeater

The Repeater image preserves MeshCore 1.18 repeating behaviour and adds the same shared MECON runtime where hardware/role capabilities permit. USB remains the recovery/configuration path. BLE support is explicitly advertised rather than assumed. On the V3 and V4 the repeater build is the ESP32 Repeater Observer: MQTT plus RF administration, with Deployment-signed RF lines for password and reboot and security posture. The private beta builds it for the V3 only; 0.8.0 adds the V4.

## SenseCAP T1000-E BLE Companion

The T1000-E proves that MECON Core is portable beyond ESP32. It is built in every private beta release as a normal MeshCore Companion and hardware-proven in limited scope: BLE Companion, USB Reader bridge, no over-the-air update. It adds only functionality justified by the hardware/native target:

- MECON identity/version/capability discovery;
- direct BLE Reader/management using the native target BLE stack and MeshCore Companion semantics;
- direct USB configuration/recovery/diagnostics where supported by the upstream target;
- common MECON configuration semantics for settings the target can expose;
- health/status including applicable battery, RF, runtime and GNSS/location state;
- contacts/channels inventory and normal Companion messaging;
- favourites/attention semantics where useful, without inventing a display;
- private-channel resilience/daily-status behaviour where resource and upstream APIs permit;
- update through the USB bootloader after a Deployment-signed offer, attested on boot and acknowledged (BLE DFU is compiled out); there is no over-the-air update.

The private beta also ships Deployment-signed management, security posture, identity rotation/export, BLE bonds and an opaque continuity slot on this target.

The T1000-E has no direct MECON IP requirement: no Wi-Fi profiles, MQTT client, mDNS broker discovery or local/cloud broker failover. A MeshContinuum Reader connected over BLE/USB can bridge the device to backend/MQTT services. ESP32-specific IDF5/pioarduino/NimBLE requirements do not apply to this target.

The T1000-E prioritizes **Core + BLE + USB interoperability with the existing Reader contracts** rather than feature parity with an IP-capable Heltec.

## Seeed Wio Tracker L1 repeater

The Wio Tracker L1 (nRF52840) runs the MECON RF repeater. It has no Wi-Fi, BLE or MQTT client; it is managed over MeshCore RF and the USB console. Shipped in the private beta: RF management (reads, and Deployment-signed writes through a compact signed RF line), a per-device signed-writes switch, two-phase admin and guest passwords, security posture with a recovery-only image, a signed status beacon, USB DFU, and a LoRa-staged update that is stored but never activated by the radio. Hardware-proven in limited scope; the build is held back from remote update.

## Other boards

Roughly seventy further boards are built in CI and classified "software proven": they compile, and nothing more is claimed. Compiled is not supported; a board enters the tables above only after a real-hardware test.

## Device UI / attention

Supported display targets retain the stock 1.18 front screen/button model with only the documented MECON 1.0 additions: MECON version at boot, Wi-Fi/MQTT/BLE status, BLE PIN while unconnected, and DM/favourite wake filtering.

Non-display targets omit display/framebuffer requirements. If a target exposes LEDs, buzzer or haptics, notification mappings are target-specific but should follow the same attention principle: DMs/favourited messages may alert; ordinary observed/public traffic should not generate attention merely because it was received.

## Hardware gate

Each supported Heltec V3/V4 Companion/Repeater target must verify stock-derived role behaviour, USB, advertised BLE/NimBLE, all three Wi-Fi slots, local broker discovery and cloud fallback, packet observations, configuration/recovery, reconnect behaviour, memory headroom, UI additions and managed OTA. Compilation alone is not a hardware gate.

Experimental targets have their own issue-defined MVP gate and are not added to the supported release matrix until that gate passes on real hardware.