# Public device contract

These documents define the canonical device-facing contract for **MECON 1.0**, built on released MeshCore 1.18 from upstream `main`.

The contract set is intended to be **implementation-complete**: a firmware developer should not need the private MVP source to discover required device-visible behaviour, and a backend implementer should not need private MeshContinuum knowledge to integrate. If a required payload, operation, lifecycle, error, setting or capability is missing here, that is a documentation defect.

- [Core device contract](CORE_DEVICE_CONTRACT.md)
- [MQTT protocol](MQTT_PROTOCOL.md)
- [Direct USB/BLE protocol](DIRECT_READER_PROTOCOL.md)
- [Device settings](DEVICE_SETTINGS.md)
- [Security and authority](SECURITY_AND_AUTHORITY.md)

The complete functional scope is enumerated in [`../FIRMWARE_IMPLEMENTATION_SPEC.md`](../FIRMWARE_IMPLEMENTATION_SPEC.md). Every device-visible item there must either be specified in this contract set or explicitly identified as a native MeshCore 1.18 operation that MECON reuses unchanged.

## Principles

1. MeshCore 1.18 native behaviour is reused where suitable; the contract identifies the boundary rather than inventing duplicates.
2. MECON contracts are backend-neutral.
3. Capabilities and independently versioned profiles describe behaviour.
4. MQTT, USB and BLE are transports; they do not redefine logical MECON operations.
5. Authority is explicit and least-privilege.
6. State-changing jobs have stable identity, explicit results and replay/idempotency protection.
7. Unknown fields are forward-compatible; unknown authority is never granted.
8. Public names use `mecon`, never private deployment vocabulary.
9. Resource limits such as contact capacity are reported by the supported build, not assumed from the private MVP.
10. The contract covers observations, inventory, health/status/logging, settings, messaging, outage resilience, local/cloud MQTT selection, direct Reader access and OTA wherever these are device-visible.

## Legacy/back-end migration boundary

Legacy/private behaviour is **not normative**. `../BACKEND_MIGRATION.md` is the actionable list of changes required in the current MeshContinuum backend/Reader. `../CONTRACT_MIGRATION_NOTES.md` provides engineering history/context. During mixed-fleet rollout the backend may implement compatibility adapters, but the clean MECON firmware should not reproduce private protocol debt solely for compatibility.

## Completeness gate

Before MECON 1.0 implementation is declared ready, audit the private MVP's historical `DEVICE_HEALTH`, `DEVICE_SETTINGS_PROTOCOL`, `MQTT_GATEWAY_PROTOCOL`, `OUTAGE_SYNC_PROTOCOL` and `PROVISIONING_SERIAL_PROTOCOL` contracts plus deployed backend behaviour. Every still-required capability must be either:

- represented here as MECON v1;
- explicitly delegated to a named MeshCore 1.18 native operation; or
- explicitly rejected as legacy/backend-only in `BACKEND_MIGRATION.md`.

No required behaviour may remain implicit in private source.
