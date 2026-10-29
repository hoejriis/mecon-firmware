# Public device contract

> **Target documentation (planned and MECON-specific work; not implemented here unless a section says so).** The tagged releases v0.1.0 to v0.1.2 are source-only pre-releases without the device interface. Current `main` implements the generic [device interface v1](DEVICE_INTERFACE.md) and the standalone features in the [README](../../README.md), with real-board evidence in [HARDWARE_VERIFIED](../HARDWARE_VERIFIED.md). The generic work still planned (Issues #95, #96, #109, #110, #114 to #117) and the MECON-specific work (Issues #118 to #123) described here are not implemented. Release timing is a separate decision. This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

## Which documents are current

**Precedence.** [DEVICE_INTERFACE.md](DEVICE_INTERFACE.md) is the current wire contract: the generic interface that `main` implements, over USB, BLE and MQTT, with its [schemas](schema) and [fixtures](fixtures). It is the only document here that describes shipped behaviour. [MQTT_PROTOCOL.md](MQTT_PROTOCOL.md), [CORE_DEVICE_CONTRACT.md](CORE_DEVICE_CONTRACT.md), [DIRECT_READER_PROTOCOL.md](DIRECT_READER_PROTOCOL.md), [DEVICE_SETTINGS.md](DEVICE_SETTINGS.md) and [TARGET_FIRMWARE_CONTRACT_0.9.md](TARGET_FIRMWARE_CONTRACT_0.9.md) are **target or background documents**. Where any of them disagrees with DEVICE_INTERFACE.md (topic names, field names, framing, limits, operations, results, subscriptions, retry and deduplication, capability discovery), DEVICE_INTERFACE.md wins. [SECURITY_AND_AUTHORITY.md](SECURITY_AND_AUTHORITY.md) is a target document for the planned authority layer (generic authority is Issue #95; MECON's Deployment mapping is Issue #118).

Legacy private wire names (`deimos/gw`, `gateway_id`, `deimos_*`) appear in these pages only where marked as the reference firmware's legacy wire. They are not part of the current contract.

Planned work that these target pages feed into, and that is not built: [management over LoRa](../ARCHITECTURE.md#management-over-lora-planned) (private management channels, authorized operations over direct and channel LoRa, compact LoRa telemetry) and the other Issues listed in [CLEAN_REFOUNDING_1_18](../CLEAN_REFOUNDING_1_18.md).

## The target contract set

The documents below define the device-facing **target** contract for the public line, built on released MeshCore 1.18 from upstream `main`. "MECON target" means this target set. It is not a release or a date; release timing is a separate decision. The private 0.9x reference firmware speaks a legacy wire today (`gateway_id`, `deimos/gw/…`, `enrollment_token`); that shipped behaviour is summarised in [Current-to-target adjustments](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line) and is not yet normative here.

The current consolidated architecture freeze candidate is **[Target Firmware Contract v0.9](TARGET_FIRMWARE_CONTRACT_0.9.md)**. It records the target architecture as it stood before MeshCore 1.18 was released and pinned. MeshCore 1.18.0 was released on 2026-10-15 and is pinned. Exact upstream CLI/configuration mappings and MECON extension names/encodings are still to be frozen, and no mapping inventory against 1.18 is recorded in this repository.

The contract set is intended to be **implementation-complete**: a firmware developer should not need the private MVP source to discover required device-visible behaviour, and a backend implementer should not need private MeshContinuum knowledge to integrate. If a required payload, operation, lifecycle, error, setting or capability is missing here, that is a documentation defect.

- [Target Firmware Contract v0.9](TARGET_FIRMWARE_CONTRACT_0.9.md)
- [Device interface v1](DEVICE_INTERFACE.md): **current and implemented.** The one versioned interface across MQTT, USB and BLE; takes precedence over the pages below
- [Core device contract](CORE_DEVICE_CONTRACT.md)
- [MQTT protocol](MQTT_PROTOCOL.md)
- [Direct USB/BLE protocol](DIRECT_READER_PROTOCOL.md)
- [Device settings](DEVICE_SETTINGS.md)
- [Security and authority](SECURITY_AND_AUTHORITY.md)
- [Management authority contract v1](AUTHORITY.md): Deployment, authority sets, signed requests and events, replay, record, reset scopes (design under review)
- [Signed firmware updates](UPDATES.md): release manifest and anchors, the install request, one lifecycle over IP, USB and BLE, and what each target verifies (contract, not built)
- [Reference firmware today (legacy wire)](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line): what the 0.9x firmware speaks now; informative, not normative

The complete functional scope is enumerated in [`../FIRMWARE_IMPLEMENTATION_SPEC.md`](../FIRMWARE_IMPLEMENTATION_SPEC.md). Every device-visible item there must either be specified in this contract set or explicitly identified as a native MeshCore 1.18 operation that MECON reuses unchanged.

## Principles

1. MeshCore 1.18 native behaviour is reused where suitable; the contract identifies the boundary rather than inventing duplicates.
2. **MeshCore CLI/configuration is the canonical management plane for native device behaviour; MECON extends it rather than duplicating it.**
3. USB, BLE, MQTT and applicable MeshCore RF/LoRa are transports for the same logical operations, not separate configuration products.
4. The public contract uses allowlisted/versioned operation envelopes; it does not expose an unrestricted raw shell merely because CLI semantics are canonical.
5. MECON contracts are backend-neutral.
6. Capabilities and independently versioned profiles describe behaviour.
7. Authority is explicit and least-privilege.
8. State-changing jobs have stable identity, explicit results and replay/idempotency protection.
9. Unknown fields are forward-compatible; unknown authority is never granted.
10. Public names use `mecon`, never private deployment vocabulary. (This is the target; the reference firmware's shipped wire still uses `deimos/gw` topics and `deimos_*` aliases, and accepts `deimos.*` settings permanently.)
11. Resource limits such as contact capacity are reported by the supported build, not assumed from the private MVP.
12. The contract covers observations, inventory, health/status/logging, settings, messaging, outage resilience, local/cloud MQTT selection, direct Reader access and OTA wherever these are device-visible.

## Legacy/back-end migration boundary

Legacy/private behaviour is **not normative**. `../BACKEND_MIGRATION.md` is the actionable list of changes required in the current MeshContinuum backend/Reader. `../CONTRACT_MIGRATION_NOTES.md` provides engineering history/context. During mixed-fleet rollout the backend may implement compatibility adapters, but the clean MECON firmware should not reproduce private protocol debt solely for compatibility.

## Completeness gate

Before MECON target implementation is declared ready, audit the private MVP's historical `DEVICE_HEALTH`, `DEVICE_SETTINGS_PROTOCOL`, `MQTT_GATEWAY_PROTOCOL`, `OUTAGE_SYNC_PROTOCOL` and `PROVISIONING_SERIAL_PROTOCOL` contracts plus deployed backend behaviour. Every still-required capability must be either:

- represented here as MECON v1;
- explicitly delegated to a named MeshCore 1.18 native operation; or
- explicitly rejected as legacy/backend-only in `BACKEND_MIGRATION.md`.

Before v0.9 becomes contract 1.0, also inventory released MeshCore 1.18's Companion/CommonCLI/configuration surface and map every native configuration requirement to upstream. Only true MECON-owned behaviour receives MECON extension commands/paths.

No required behaviour may remain implicit in private source.