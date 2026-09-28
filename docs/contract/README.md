# Public device contract

These documents define the canonical device-facing contract for **MECON 1.0**, built on released MeshCore 1.18 from upstream `main`.

The current consolidated architecture freeze candidate is **[Target Firmware Contract v0.9](TARGET_FIRMWARE_CONTRACT_0.9.md)**. It records the target architecture before the final MeshCore 1.18 API is released/pinned. Contract 1.0 will freeze exact upstream CLI/configuration mappings and MECON extension names/encodings after that gate.

The contract set is intended to be **implementation-complete**: a firmware developer should not need the private MVP source to discover required device-visible behaviour, and a backend implementer should not need private MeshContinuum knowledge to integrate. If a required payload, operation, lifecycle, error, setting or capability is missing here, that is a documentation defect.

- [Target Firmware Contract v0.9](TARGET_FIRMWARE_CONTRACT_0.9.md)
- [Core device contract](CORE_DEVICE_CONTRACT.md)
- [MQTT protocol](MQTT_PROTOCOL.md)
- [Direct USB/BLE protocol](DIRECT_READER_PROTOCOL.md)
- [Device settings](DEVICE_SETTINGS.md)
- [Security and authority](SECURITY_AND_AUTHORITY.md)

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
10. Public names use `mecon`, never private deployment vocabulary.
11. Resource limits such as contact capacity are reported by the supported build, not assumed from the private MVP.
12. The contract covers observations, inventory, health/status/logging, settings, messaging, outage resilience, local/cloud MQTT selection, direct Reader access and OTA wherever these are device-visible.

## Legacy/back-end migration boundary

Legacy/private behaviour is **not normative**. `../BACKEND_MIGRATION.md` is the actionable list of changes required in the current MeshContinuum backend/Reader. `../CONTRACT_MIGRATION_NOTES.md` provides engineering history/context. During mixed-fleet rollout the backend may implement compatibility adapters, but the clean MECON firmware should not reproduce private protocol debt solely for compatibility.

## Completeness gate

Before MECON 1.0 implementation is declared ready, audit the private MVP's historical `DEVICE_HEALTH`, `DEVICE_SETTINGS_PROTOCOL`, `MQTT_GATEWAY_PROTOCOL`, `OUTAGE_SYNC_PROTOCOL` and `PROVISIONING_SERIAL_PROTOCOL` contracts plus deployed backend behaviour. Every still-required capability must be either:

- represented here as MECON v1;
- explicitly delegated to a named MeshCore 1.18 native operation; or
- explicitly rejected as legacy/backend-only in `BACKEND_MIGRATION.md`.

Before v0.9 becomes contract 1.0, also inventory released MeshCore 1.18's Companion/CommonCLI/configuration surface and map every native configuration requirement to upstream. Only true MECON-owned behaviour receives MECON extension commands/paths.

No required behaviour may remain implicit in private source.