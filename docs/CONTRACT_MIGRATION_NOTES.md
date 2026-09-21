# Current-to-target contract adjustments

This file is **non-normative**. It records lessons from the private MVP and differences from the MECON 1.0 public target. MECON 1.0 is rebuilt from **MeshCore 1.18 after it reaches upstream `main`**; the private implementation is inspiration/evidence, not a code migration baseline.

## Foundational reset

Before porting MECON features:

1. pin released MeshCore 1.18;
2. prove stock V3/V4 Companion/Repeater behaviour;
3. migrate to pioarduino / Arduino-ESP32 3.x / IDF 5.x;
4. adopt NimBLE;
5. prove behaviour again and measure memory;
6. set resource limits from those measurements;
7. implement MECON using 1.18 primitives where possible.

This intentionally avoids inheriting obsolete private-MVP architecture.

## Naming and identity

Public firmware uses `mecon_*`, `mecon.*`, backend-neutral `device_id`, and native `companion`/`repeater` roles. Legacy `deimos_*`, `deimos.*`, `gateway_id`, `Companion Gateway` and `Repeater Observer` are migration concerns only.

## MeshCore 1.18 reuse

The private MVP contains parallel Wi-Fi, configuration, CLI/command, UI and board-specific mechanisms created against older MeshCore. Review each against 1.18 before porting. If 1.18 provides the required primitive, extend/reuse it instead of migrating the private implementation.

## Wi-Fi and MQTT

The target is **three ordered Wi-Fi profiles and one active MQTT session**. A compatible locally discovered broker is preferred; otherwise the device uses its configured cloud broker. The private MVP's historical two-concurrent-broker architecture, independent broker slots and associated replay complexity are not public 1.0 requirements.

Discovery is non-blocking and conveys reachability only; trust/authority remains provisioned.

## Settings and transports

One logical settings/capability model spans MQTT, USB and BLE. Native MeshCore 1.18 operations are reused where adequate. MECON-specific operations use versioned envelopes/tunnels rather than transport-specific product implementations.

BLE is NimBLE-based. USB remains recovery-capable.

## Resource limits

Do not copy the private MVP's historical 32/64-contact limits into the public contract. Measure V3/V4 after IDF5/NimBLE and set/publish the supported contact capacity from evidence. Managed/protected-contact semantics remain required where contact eviction exists.

## Device UI

Do not port the private UI wholesale. Preserve the MeshCore 1.18 Companion/Repeater screen/button behaviour and add only: MECON version at boot; Wi-Fi/MQTT/BLE status on the existing front screen; BLE PIN while unconnected; and DM/favourite-only display wake behaviour. No secondary MECON screen in 1.0. Retain framebuffer export as test/diagnostic capability.

## Resilience features to preserve

The public refactor should preserve the MVP-derived product behaviours: MQTT-offline DM forwarding to a configured private channel; daily private-channel health/status report; and authorized MQTT delivery of DMs/channel messages when the RF mesh path is unavailable. Define loop/duplicate prevention in the final implementation.

## OTA

Signed managed OTA remains a 1.0 requirement, implemented after the clean 1.18/IDF5/NimBLE base is established.

## MeshContinuum alignment

`mecon-firmware/docs/contract/` is canonical for MECON-specific public behaviour. MeshContinuum should consume it and keep legacy compatibility adapters outside the public v1 contract.