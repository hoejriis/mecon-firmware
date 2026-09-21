# Device settings contract

Configuration is one logical model across supported MQTT, USB and BLE paths. Native MeshCore 1.18 configuration primitives/storage are reused wherever they provide the required semantics; MECON does not maintain a second authoritative copy of native settings.

## Operations

MECON-specific configuration provides:

- `get_config` — read current safe values/metadata;
- `get_schema` — discover paths, constraints and applicability;
- `apply_config` — atomic requested changes;
- explicit job/result correlation.

Transport adapters do not redefine these meanings.

## Transaction rules

An `apply_config` job is one transaction, not a sequence of unrelated path writes. Validate the complete request before committing. A reported success means the accepted configuration is persisted and has reached the declared application state (`live`, reconnect or restart as applicable). Partial success is not silently reported as success.

For changes that can strand the management path (Wi-Fi/MQTT/network policy), preserve/recover a known-working path where practical and roll back a failed transition rather than persist an unreachable configuration without reporting failure.

State-changing jobs are replay-protected/idempotent across MQTT reconnects, local/cloud path switching and supported direct transports.

## Schema metadata

Each setting advertises, as applicable:

- canonical path;
- type;
- readable/writable flags independently;
- secret/write-only flag;
- constraints/enumeration/range;
- default when meaningful;
- role/capability applicability;
- application effect (`live`, `reconnect_required`, `restart_required` or future versioned equivalent);
- whether the change can disrupt RF or management connectivity.

A reported/readable setting is not automatically writable. Read-only exposure is preferred where another native MeshCore lifecycle owns the state.

## Canonical namespace

MECON-owned settings use `mecon.*`. New MECON 1.0 devices do not advertise the historical `deimos.*` namespace. Legacy translation belongs in backend/tool adapters.

Native mesh/radio/location/channel settings map to released MeshCore 1.18's canonical configuration model where possible. Expected configurable outcomes include node name, applicable radio parameters and location. Final path/operation mapping is frozen after 1.18 reaches `main`.

## Wi-Fi

MECON extends upstream Wi-Fi support to **three ordered profiles**, slots 0–2. Each profile contains SSID and write-only credential material plus any future versioned connection metadata. The device tries available configured networks in entered priority order and changes association as availability changes.

Current status reports the actual associated slot and actual SSID independently from configured values so configuration drift can be detected. Disconnected state is explicit `null`; slot 0 must never be overloaded to mean disconnected.

Wi-Fi loss does not stop the native MeshCore role.

## MQTT

MECON 1.0 describes a **single active-session architecture**:

- configured cloud endpoint/credentials/namespace;
- local-broker discovery enabled/policy;
- authentication/authority information needed to use an eligible discovered local broker;
- TLS trust selection/policy where configurable.

The device prefers an eligible local broker and falls back to cloud. There are no two concurrent public broker slots and no backend may depend on simultaneous sessions.

MQTT username/password/tokens are write-only.

## Security/identity settings

Device authority/enrollment credentials are write-only and protected from ordinary config reads. BLE pairing PIN is not a remotely readable setting. Reset/reprovision operations must explicitly define whether they clear network credentials, backend authority, device identity and/or native MeshCore state; a generic 'factory reset' must not ambiguously claim to clear everything.

## Outage/resilience settings

The common model exposes the configuration required for the private resilience channel, including enabled/configured state, channel key, member roster, configuration version, stock-readable-copy option and daily-status behaviour. Secrets such as channel keys are never returned in clear text.

The device reports roster/capacity constraints rather than relying on a backend hard-coded number.

## Native contacts/channels

Contacts and channels are native MeshCore state. MECON may expose them read-only through inventory/schema and may invoke native/provisioning operations required to complete an authorized send, but does not create a second declarative contact/channel database unless MeshCore 1.18 itself provides the authoritative mechanism.

Protected/managed-contact semantics, if the supported build evicts contacts under pressure, operate on the native contact table.

## Companion TCP/private legacy features

The private MVP exposed `mecon.companion_tcp.enabled` for an unauthenticated port-5000 Companion TCP listener. This is **not automatically part of MECON 1.0**. After MeshCore 1.18 is released, retain it only if a concrete interoperability requirement remains and the security model explicitly documents the exposure. Backends must not assume it exists on v1 devices.

## Secrets

Secrets may be replaced but are never returned. A read/schema may expose `configured: true`, credential type or other non-secret metadata sufficient for administration, but not the secret itself.

## Unknown and unsupported settings

Unknown paths are rejected explicitly. Type coercion is not used for security-sensitive settings. Unsupported settings are not silently accepted and ignored.

## Roles

Companion and Repeater share the logical MECON settings mechanism; applicability is schema/capability-driven. A client must not infer a setting solely from the role label.

## Versioning

The configuration profile is independently versioned. Additive schema fields are forward-compatible; semantic changes to transaction/path meaning require a profile version change.