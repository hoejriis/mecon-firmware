# Core device and health contract

## Identity

A device reports:

- stable MECON `device_id`;
- native `node_public_key_hex`;
- `hardware_target` (`heltec_v3` / `heltec_v4` for 1.0 supported releases);
- native role (`companion` / `repeater`);
- build variant;
- `mecon_firmware_version`;
- exact `meshcore_base_version`;
- MECON contract/profile versions.

For MECON 1.0, supported release builds identify a pinned **MeshCore 1.18** base from upstream `main`.

Backend enrollment/database identifiers are not firmware identity and must not replace `device_id` or the native node key.

## Profiles and capabilities

`core` is mandatory. Optional/versioned profiles include observation, messaging, configuration, health, resilience, OTA and direct Reader functionality. Capabilities are explicit. A client must not infer a MECON capability from board/role/version alone.

Capabilities/resource declarations include applicable contact capacity, resilience roster capacity, direct transports, BLE, observation/logging support and OTA support.

## Native boundary

Companion and Repeater retain their MeshCore 1.18 semantics. MECON does not define proprietary replacement RF roles. Where 1.18 exposes a suitable operation, MECON reuses it rather than creating a duplicate command/state store.

## Status publication

Status is a complete snapshot. It is emitted on a regular heartbeat and may additionally be emitted after significant state changes. Event-triggered publishes are coalesced/rate-bounded so connectivity flapping cannot starve the RF loop; the periodic heartbeat itself remains reliable.

## Unknown-value rules

These are normative consumer rules:

1. **`null` means measured/supported but currently unknown/unavailable. It never means zero.**
2. **Absent means the field/capability is not implemented/applicable or predates the field. It is distinct from null.**
3. **Zero is a real numeric reading.**
4. Monotonic counters are monotonic only inside their declared epoch.

## Health fields

MECON 1.0 health/status preserves the diagnostic coverage proven useful in the private MVP. Exact field grouping may evolve only with the health profile version; the following logical values are required where measurable/applicable:

### Runtime

- `uptime_seconds`;
- reset reason stable token or null plus raw reset code when unknown;
- abnormal-reset boolean;
- previous-panic/crash record when available across reboot;
- current free heap bytes;
- minimum free heap bytes since boot;
- loop-task stack size and minimum free/high-water headroom;
- battery millivolts where the board can measure it.

No firmware-derived battery percentage is required unless a trustworthy chemistry/model-specific implementation is later added.

### Wi-Fi

- associated/disconnected state;
- actual active profile slot (`null` when disconnected);
- actual associated SSID (`null` when disconnected);
- RSSI dBm (`null` when disconnected).

Configured SSID and actual associated SSID are separate facts.

### MQTT

- connected/disconnected state;
- selected path (`local` or `cloud`) when connected;
- active endpoint/profile identity safe to expose;
- connection/reconnect/error counters sufficient to diagnose flapping;
- publish counters for consequential/observation traffic.

MECON 1.0 does not report an array of simultaneously connected broker sessions because only one session is active.

### BLE

Where supported, report stable state such as `off`, `advertising` or `connected`. BLE startup is not dependent on successful MQTT; direct local recovery remains available according to the direct profile.

### RF

Expose enough state to diagnose both traffic and a receiver that appears armed in software but is no longer receiving. Required logical fields are:

- RX flood/direct counters;
- TX flood/direct counters;
- raw radio receptions;
- radio read/error/CRC failures where available;
- counter epoch;
- seconds since last reception (`null` until first reception);
- software RX-armed state;
- chip mode where cheaply available on the target;
- receiver-liveness rearm/recovery counter if the liveness guard is retained after 1.18/IDF5 validation.

RF counters reset on reboot and may reset on native `clear stats`; consumers difference only within the published counter epoch.

### Observation publisher

Expose boot/path counters equivalent to:

- observations reaching the publisher (`tried`);
- accepted by active MQTT client/broker path;
- refused by client/outbox;
- dropped because no authorized target is connected;
- dropped because serialization/size constraints made publishing impossible.

A broker acknowledgement is not proof the backend consumed the observation.

### Contacts

Companion status reports:

- current contact count;
- actual enforced contact capacity;
- protected/managed contact count where supported;
- contacts evicted since boot where eviction exists.

Roles with no contact table omit these fields rather than report `0/0`. Capacity is measured after the IDF5/NimBLE migration and reported by the running build; MECON 1.0 does **not** normatively fix it to historical private-MVP limits.

### Status coalescing

Expose a boot-scoped suppressed/coalesced status-event counter so repeated state transitions can be diagnosed without publishing every transition immediately.

## Crash record

Where ESP-IDF/platform support permits, preserve a bounded record of the previous panic/reset sufficient to diagnose deployed crashes (reason/panic summary, relevant PC/backtrace identifiers if safely available, stack headroom and timestamp/uptime context). The record is diagnostic, bounded and must not contain secrets.

## UI contract

MECON 1.0 preserves the released MeshCore 1.18 Companion/Repeater screen/button model and adds only:

- `MECON <version>` at boot;
- Wi-Fi, MQTT and BLE connection status on the existing front screen;
- BLE pairing PIN while BLE is available but unconnected, replaced by connected status afterwards;
- DM and favourited-channel messages may wake/show the display; public/unfavourited channel traffic does not wake it merely because it was observed;
- no secondary MECON screen;
- no new normal button semantics.

Framebuffer export is retained as a diagnostic/test capability, not a user-facing screen.

## Runtime/resource declaration

V3 and V4 Companion and Repeater are first-class targets. The public baseline is pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x plus NimBLE. Wi-Fi/MQTT/discovery/OTA work is non-blocking with respect to normal MeshCore servicing.

Material resource limits are machine-readable and hardware/role/build-specific rather than copied from the private MVP.