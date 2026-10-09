# Direct USB/BLE Reader protocol

> **Target documentation:** this public repository has no installable release yet. See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

## Goal

Direct access preserves native MeshCore 1.18 interfaces and adds only the MECON-specific operations needed for the public capability model. A device remains recoverable/manageable locally when Wi-Fi, MQTT or the backend is unavailable.

The private first-boot line-JSON provisioning console is retired and must not be recreated.

## Logical model

MECON configuration/status/diagnostic operations have the same meaning over direct transports as over MQTT. A transport adapter changes framing and authentication context, not the device model. Where MeshCore 1.18 already has a native operation, use it directly.

MECON-specific direct operations use a reserved/versioned extension/tunnel. Do not preserve private `key:value` or Repeater-only command vocabularies as the public abstraction merely because the MVP used them.

## Companion USB

USB preserves standard released MeshCore 1.18 Companion framing and command interoperability. MECON extensions may expose:

- status/capabilities/profile versions;
- configuration schema/read/apply;
- safe reprovision/recovery operations;
- health/diagnostics;
- bounded observation/raw logging;
- framebuffer export;
- OTA/reboot administration where explicitly authorized locally.

Native Companion commands remain native for identity, messaging, contacts/channels and other functionality 1.18 already supplies.

### Interleaved push frames

The Companion serial link is a frame stream, not a guaranteed request/response-only channel. A client waiting for a response must tolerate unsolicited native/MECON push frames, consume their complete framed bodies, skip unknown future codes safely and continue until the expected correlated response arrives. Clients must be able to resynchronize on frame boundaries after transport interruption.

## Companion BLE

BLE preserves stock Companion interoperability but the public ESP32 implementation uses **NimBLE**. It exposes the same native Companion semantics and versioned MECON extension where applicable.

BLE is a local recovery/Reader path and must not require successful Wi-Fi or MQTT before becoming available. Startup may be delayed only for bounded boot/resource reasons documented by the supported build, never indefinitely waiting for cloud connectivity.

Pairing uses a device-specific credential/PIN. On display-capable targets, the PIN is shown on the existing front screen while BLE is available but unconnected; connected state replaces the PIN. The passkey is random per device, never derived from public data and never a shared default, and BLE does not start if one cannot be established. It is not an ordinary readable setting: it is exposed read-only over authenticated management and physical USB only. Bonds can be counted and cleared over physical USB only; a clear requested while BLE is not running is deferred to the next BLE start and verified.

### Handshake, replay and capability paging

The Reader's single unsigned readiness probe on attach is answered but not counted as an authentication rejection; every other unsigned request on a Deployment-aware device is refused and counted. A line refused before execution consumes no request identifier and moves no signer watermark. The identification response pages its capability list so a device may advertise more capabilities than one frame holds.

### Firmware update over a direct Reader

For targets updated through USB/bootloader DFU: offer, bootloader update, attest on boot, acknowledge. An otherwise-valid new offer supersedes a pending update whose image already attested OK but whose acknowledgement was lost (the old job is reported as succeeded first); an unverified pending image never blocks.

## Repeater USB

Repeater USB preserves the native 1.18 CLI/recovery model and adds the common MECON logical management operations where advertised. MECON 1.0 does not require a Repeater to pretend to be a Companion binary device.

The private `mecon_set` / `mecon_status` vocabulary is migration history; public tooling should discover/use the v1 direct capability rather than encode private command names.

### Raw observation diagnostic stream

A Repeater may expose a local bounded raw observation stream equivalent to the MQTT observation object. The private MVP proved useful semantics that remain required:

- explicitly enabled/disabled; default off at boot;
- one complete framed/line record per received packet, never mid-record interleaving;
- raw bytes, RSSI, SNR and reception time when known;
- bounded output buffering;
- if output cannot accept a complete record, drop that diagnostic record rather than block RF processing;
- expose/reset a dropped-record counter when the stream is enabled;
- the diagnostic stream is local-only unless a separate remote capability explicitly allows it.

The exact public framing is frozen with the direct-profile implementation; tools must not depend on the old `MECON_RAW ` textual prefix unless using the legacy adapter.

## Repeater BLE

BLE on a Repeater is capability-dependent. If a supported V3/V4 Repeater build advertises BLE, it follows the same NimBLE/security/versioning principles; clients must not infer BLE solely from role or board.

## Reprovision/reset

Direct management must define explicit reset scopes. At minimum tooling must be able to change Wi-Fi/MQTT configuration without destroying native MeshCore identity, and an explicitly stronger unenroll/reset operation may clear MECON authority/credentials. Do not overload native MeshCore `factory reset` with undocumented MECON credential semantics.

## UI/testing

No secondary MECON screen exists in 1.0. Framebuffer export is retained so automated tests can verify boot identity, connectivity indicators, BLE PIN/connected state and message-wake behaviour without a camera.

## Security

In Hardened and the fail-secure state BLE does not start on any target; USB answers identification and the authenticated-Reader handshake only.

Direct access never exposes unrestricted command execution. Sensitive values remain write-only. Native MeshCore identity/key operations remain governed by native/security semantics. MECON extensions are allowlisted/versioned; unknown operations fail rather than falling through to arbitrary CLI execution.