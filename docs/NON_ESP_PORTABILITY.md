# Non-ESP portability

> **Target documentation:** this public repository has no installable release yet. The first public firmware will be mecon-firmware 0.8.0 (see [Versioning and releases](VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

MECON is a capability architecture around MeshCore, not an ESP32/Wi-Fi/MQTT product definition.

The first public release (mecon-firmware 0.8.0) centres on Heltec V3/V4 and also carries two nRF52 roles, the T1000-E BLE Companion and the Wio Tracker L1 RF repeater; portable functionality is separated so each target implements only what its hardware supports.

## Portable Core

The following behaviour should be designed without an ESP32 dependency:

- MECON/native identity, firmware/base versions and capability discovery;
- common configuration semantics;
- health/status and applicable diagnostics;
- contacts/channels inventory for Companion roles;
- DM/channel messaging and stable message/job identity;
- favourites/attention policy;
- resilience/private-channel forwarding and daily health reports where resource/API limits permit;
- authorization, replay and idempotency semantics;
- common management operation/result model.

## Optional platform profiles

### USB

Direct Reader/configuration/recovery using the target's native MeshCore interface plus MECON extensions where required.

### BLE

Direct Reader/configuration/messaging. ESP32 uses NimBLE; other platforms use their appropriate BLE implementation. NimBLE itself is not a MECON protocol requirement.

### IP

Wi-Fi/network profiles, local broker discovery, MQTT and cloud fallback. A target without IP simply omits this profile. A BLE/USB Reader may bridge a non-IP device into MeshContinuum/backend infrastructure.

### Attention/UI

Display and framebuffer are optional. A non-display board may expose LED, buzzer or haptic alerts. The portable policy is that DMs and favourited-channel messages may request user attention; ordinary observed/public traffic does not.

### Managed update

The logical operation is portable but the mechanism is target-specific: ESP OTA, USB DFU, a LoRa-staged image or another explicitly supported secure mechanism. BLE DFU is not an accepted mechanism: the reference nRF52 builds compile it out because the bootloader checks no signature. The T1000-E updates through the USB bootloader after a Deployment-signed offer; the Wio repeater stages an image received over LoRa (it never activates it) and updates by USB DFU.

## First portability proof: SenseCAP T1000-E

The T1000-E is the first non-ESP MECON target, built in every private beta release and hardware-proven in limited scope (BLE Companion, USB Reader bridge, no over-the-air update). The Wio Tracker L1 (nRF52840) is the second: an RF repeater managed over signed RF lines and USB, with no Wi-Fi, BLE or MQTT. The T1000-E's purpose is not to recreate the Heltec IP feature set. It should prove that a normal MeshCore Companion can implement MECON Core + BLE + USB and interoperate with the same Reader/backend model while honestly advertising the absence of Wi-Fi/MQTT/display capabilities.

Success on T1000-E should be used to remove any remaining accidental ESP assumptions from public contracts before adding further non-ESP targets such as the SenseCAP MeshTracker X1.