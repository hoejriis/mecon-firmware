# Supported hardware and roles

MECON 1.0 depends on released **MeshCore 1.18 from upstream `main`**. A target is called supported only after its derived build passes its real-hardware gate.

MECON uses capability profiles rather than defining the product as ESP32/Wi-Fi/MQTT firmware. Heltec V3/V4 are the **supported 1.0 release baseline**; non-ESP boards can implement portable MECON Core plus the transports/features their hardware actually provides.

| Hardware | Status | Companion | Repeater | IP/Wi-Fi | USB | BLE | MQTT |
|---|---|---:|---:|---:|---:|---:|---:|
| Heltec V3 | MECON 1.0 target | Yes | Yes | Yes | Yes | capability-dependent | one active session |
| Heltec V4 | MECON 1.0 target | Yes | Yes | Yes | Yes | capability-dependent | one active session |
| SenseCAP T1000-E | Experimental / P3 | Planned MVP | No initial target | No | Planned | Planned | No direct client |

V3 and V4 are first-class targets from initial 1.0 bring-up. T1000-E is explicitly **not a MECON 1.0 release dependency**; it is the first planned portability proof for the Core/BLE/USB architecture.

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

The Companion image preserves MeshCore 1.18 behaviour, adds three ordered Wi-Fi profiles, local-first/cloud-fallback MQTT, MECON management/observations, and direct USB/BLE where advertised. BLE uses NimBLE.

Companion contact capacity is **not fixed in advance at the private MVP's historical 32/64 values**. It is set from the measured memory budget after pioarduino/Arduino-ESP32 3.x/IDF5 and NimBLE migration and published in status/manifests.

## Heltec Repeater

The Repeater image preserves MeshCore 1.18 repeating behaviour and adds the same shared MECON runtime where hardware/role capabilities permit. USB remains the recovery/configuration path. BLE support is explicitly advertised rather than assumed.

## SenseCAP T1000-E experimental target

The T1000-E is intended to prove that MECON Core is portable beyond ESP32. The first MVP should remain a normal MeshCore Companion and add only functionality justified by the hardware/native target:

- MECON identity/version/capability discovery;
- direct BLE Reader/management using the native target BLE stack and MeshCore Companion semantics;
- direct USB configuration/recovery/diagnostics where supported by the upstream target;
- common MECON configuration semantics for settings the target can expose;
- health/status including applicable battery, RF, runtime and GNSS/location state;
- contacts/channels inventory and normal Companion messaging;
- favourites/attention semantics where useful, without inventing a display;
- private-channel resilience/daily-status behaviour where resource and upstream APIs permit;
- target-appropriate managed DFU/update capability as a later extension if it can be made safe.

The T1000-E has no direct MECON IP requirement: no Wi-Fi profiles, MQTT client, mDNS broker discovery or local/cloud broker failover. A MeshContinuum Reader connected over BLE/USB can bridge the device to backend/MQTT services. ESP32-specific IDF5/pioarduino/NimBLE requirements do not apply to this target.

The first T1000-E MVP should prioritize proving **Core + BLE + USB interoperability with the existing Reader contracts** rather than feature parity with an IP-capable Heltec.

## Device UI / attention

Supported display targets retain the stock 1.18 front screen/button model with only the documented MECON 1.0 additions: MECON version at boot, Wi-Fi/MQTT/BLE status, BLE PIN while unconnected, and DM/favourite wake filtering.

Non-display targets omit display/framebuffer requirements. If a target exposes LEDs, buzzer or haptics, notification mappings are target-specific but should follow the same attention principle: DMs/favourited messages may alert; ordinary observed/public traffic should not generate attention merely because it was received.

## Hardware gate

Each supported Heltec V3/V4 Companion/Repeater target must verify stock-derived role behaviour, USB, advertised BLE/NimBLE, all three Wi-Fi slots, local broker discovery and cloud fallback, packet observations, configuration/recovery, reconnect behaviour, memory headroom, UI additions and managed OTA. Compilation alone is not a hardware gate.

Experimental targets have their own issue-defined MVP gate and are not added to the supported release matrix until that gate passes on real hardware.