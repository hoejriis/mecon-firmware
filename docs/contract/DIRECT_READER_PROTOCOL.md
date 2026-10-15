# Direct USB/BLE Reader protocol

> **Target documentation:** this public repository has no installable release yet. The first public firmware will be mecon-firmware 0.8.0 (see [Versioning and releases](../VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

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

### Reference firmware commands

The 0.9x reference firmware's USB and BLE link uses these companion commands beyond the stock set: 45 (status), 49 (device information, with capabilities paged and the fields `posture_hardened` and `reader_auth_required`), 120 (the JSON tunnel; replies are pushed as 0x92), 121 (passkey read, USB only), 122 to 124 (the Reader unlock), 125 (health, T1000-E), 23 and 24 (key export and import), 37 (set passkey) and 46, 47, 48 and 51 (USB-only: credentials, replay watermarks, provisioning reset, factory erase). The tunnel carries request kinds 0 command, 1 `get_config`, 2 `apply_config`, 3 `sync_config`, 4 `authority_update` and 5 to 7 update offer, chunk and ack (5 to 7 USB only). One tunnel request is in flight at a time, with a four-slot command queue and a 3,071-byte bound.

## Companion BLE

BLE preserves stock Companion interoperability but the public ESP32 implementation uses **NimBLE**. It exposes the same native Companion semantics and versioned MECON extension where applicable.

BLE is a local recovery/Reader path and must not require successful Wi-Fi or MQTT before becoming available. Startup may be delayed only for bounded boot/resource reasons documented by the supported build, never indefinitely waiting for cloud connectivity.

Pairing uses a device-specific credential/PIN. On display-capable targets, the PIN is shown on the existing front screen while BLE is available but unconnected; connected state replaces the PIN. The passkey is random per device, never derived from public data and never a shared default, and BLE does not start if one cannot be established. It is never writable through the settings path. The target exposes it read-only over authenticated management and physical USB only. The reference firmware also sets it by a Deployment-signed `ble_passkey` command (any transport; 0 for a fresh random value, or 100000 to 999999; in force at the next reboot and never echoed) and by USB command 37, and returns it in `get_config` over MQTT, the signed tunnel and USB; on a device that holds no Deployment record `get_config` also returns it without authentication over BLE and the Wi-Fi companion port. A `get_config` read after setting it to 0 reports 0 while the stored value is re-rolled at the next boot.

Bonds can be counted over any `cmd` path (broker, USB, BLE) and cleared over physical USB only (the target's rule is that both are USB-only; the reference firmware relaxes the count). A clear requested while BLE is not running is deferred to the next BLE start and verified.

### Handshake, replay and capability paging

The Reader's single unsigned readiness probe on attach is answered but not counted as an authentication rejection; every other unsigned request on a Deployment-aware device is refused and counted. A line refused before execution consumes no request identifier and moves no signer watermark. The identification response pages its capability list so a device may advertise more capabilities than one frame holds.

### Firmware update over a direct Reader

For targets updated through USB/bootloader DFU: offer, bootloader update, attest on boot, acknowledge. An otherwise-valid new offer supersedes a pending update whose image already attested OK but whose acknowledgement was lost (the old job is reported as succeeded first); an unverified pending image never blocks.

## Repeater USB

Repeater USB preserves the native 1.18 CLI/recovery model and adds the common MECON logical management operations where advertised. MECON 1.0 does not require a Repeater to pretend to be a Companion binary device.

The private `mecon_set` / `mecon_status` vocabulary (Observer) and the `mecon …` console lines (Wio) are the reference firmware's current repeater console, not only migration history; public tooling should still discover/use the v1 direct capability rather than encode private command names.

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

In Hardened and the fail-secure state BLE does not start on any target; USB answers identification and the authenticated-Reader handshake only. In the reference firmware that means commands 49 and 122 to 124 are answered and everything else returns error 8, with no unsolicited frames; a repeater console answers `mecon info`, `mecon posture` and `mecon auth challenge|prove|end` in the same way.

Direct access never exposes unrestricted command execution. Sensitive values remain write-only. Native MeshCore identity/key operations remain governed by native/security semantics. MECON extensions are allowlisted/versioned; unknown operations fail rather than falling through to arbitrary CLI execution.