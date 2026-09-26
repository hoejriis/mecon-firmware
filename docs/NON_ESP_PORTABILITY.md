# Non-ESP portability

MECON is a capability architecture around MeshCore, not an ESP32/Wi-Fi/MQTT product definition.

The initial MECON 1.0 release is intentionally Heltec V3/V4 focused, but portable functionality is separated so later targets can implement only what their hardware supports.

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

The logical operation is portable but the mechanism is target-specific: ESP OTA, BLE DFU, USB DFU or another explicitly supported secure mechanism.

## First portability proof: SenseCAP T1000-E

The T1000-E is the planned first non-ESP MECON MVP. Its purpose is not to recreate the Heltec IP feature set. It should prove that a normal MeshCore Companion can implement MECON Core + BLE + USB and interoperate with the same Reader/backend model while honestly advertising the absence of Wi-Fi/MQTT/display capabilities.

Success on T1000-E should be used to remove any remaining accidental ESP assumptions from public contracts before adding further non-ESP targets such as the SenseCAP MeshTracker X1.