# Device settings contract

> **Target documentation:** this public repository has no installable release yet. Public releases start at **0.1.0**, a standalone enhanced MeshCore on 1.18.0 (see [Clean refounding on MeshCore 1.18](../CLEAN_REFOUNDING_1_18.md)); the first version to serve a MECON fleet is planned as 0.8.0 (see [Versioning and releases](../VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](../CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

Configuration is one logical model across supported MQTT, USB, BLE and remote MeshCore/LoRa paths. **Released MeshCore 1.18 CLI/configuration semantics and storage are authoritative for native settings.** MECON does not maintain a second authoritative copy of native settings and does not define transport-specific setting semantics.

See [`TARGET_FIRMWARE_CONTRACT_0.9.md`](TARGET_FIRMWARE_CONTRACT_0.9.md) for the consolidated pre-1.0 target contract.

## Canonical command/configuration plane

For every native setting/operation exposed suitably by released MeshCore 1.18, MECON reuses the upstream CLI/configuration operation. USB, BLE, MQTT and applicable MeshCore RF/LoRa carry that same operation.

MECON-specific settings extend the common plane under `mecon.*` (or the final collision-free 1.0 CLI namespace frozen after 1.18 is pinned). They are not a separate configuration product.

The public contract does not expose an unrestricted shell. Transports carry allowlisted operations using a versioned envelope with request/job identity, authorization, result/error correlation and replay/idempotency rules. Compact transport encodings may map onto the same operation.

## Structured operations

MECON provides a structured orchestration layer useful to Reader/backend UIs:

- `get_config` — read current safe values/metadata across native and MECON-owned configuration;
- `get_schema` — discover paths, constraints, applicability and mapping/capability metadata;
- `apply_config` — atomic requested changes where the requested set can be transacted safely;
- explicit job/result correlation.

These operations **describe and orchestrate the canonical CLI/configuration plane**. They do not replace it. A simple native setting change may map directly to its released MeshCore CLI operation; a multi-field transaction validates first and then applies the required canonical operations/storage changes.

Transport adapters do not redefine these meanings.

## Transaction rules

An `apply_config` job is one transaction, not a sequence of unrelated path writes. Validate the complete request before committing. A reported success means the accepted configuration is persisted and has reached the declared application state (`live`, reconnect or restart as applicable). Partial success is not silently reported as success.

For changes that can strand the management path (Wi-Fi/MQTT/network policy), preserve/recover a known-working path where practical and roll back a failed transition rather than persist an unreachable configuration without reporting failure.

State-changing jobs are replay-protected/idempotent across MQTT reconnects, local/cloud path switching and supported direct/RF transports.

## Schema metadata

Each setting advertises, as applicable:

- canonical path/operation mapping;
- ownership (`meshcore` or `mecon`);
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

MECON-owned settings use `mecon.*`. New MECON 1.0 devices do not advertise the historical `deimos.*` namespace. Legacy translation belongs in backend/tool adapters. (The reference firmware already advertises `mecon.*`, since 0.25.0, and accepts `deimos.*` permanently as an alias.)

Native mesh/radio/location/routing/GPS/system settings map to released MeshCore 1.18's canonical CLI/configuration model wherever possible. Expected configurable outcomes include node name, applicable radio parameters and location. Final command/path mapping is frozen after 1.18 reaches `main`.

## Wi-Fi

MECON extends upstream Wi-Fi support to **three ordered profiles**, slots 0–2, only where the target supports the IP profile. Each profile contains SSID and write-only credential material plus any future versioned connection metadata. The device tries available configured networks in entered priority order and changes association as availability changes.

Current status reports the actual associated slot and actual SSID independently from configured values so configuration drift can be detected. Disconnected state is explicit `null`; slot 0 must never be overloaded to mean disconnected.

Wi-Fi loss does not stop the native MeshCore role.

## MQTT

MECON 1.0 describes a **single active-session architecture**:

- configured cloud endpoint/credentials/namespace;
- local-broker discovery enabled/policy;
- authentication/authority information needed to use an eligible discovered local broker;
- TLS trust selection/policy where configurable.

The device prefers an eligible local broker and falls back to cloud. There are no two concurrent public broker slots and no backend may depend on simultaneous sessions. (The reference firmware exposes the single legacy `mecon.mqtt.broker_*` settings and also stores up to four broker profiles through an `apply_settings` command, tiered cloud or LAN; it opens one session at a time.)

MQTT username/password/tokens are write-only.

## Remote MeshCore/LoRa configuration

Where released MeshCore 1.18 supports authenticated remote CLI/command delivery, MECON uses it rather than defining an RF-only configuration protocol. A request routed through a Companion to a remote target reaches the same canonical operation used locally.

Remote requests are target-addressed, replay-protected, correlated and bounded for LoRa airtime/MTU. A compact RF encoding may be used, but it must map unambiguously to the same command semantics.

## Security/identity settings

Device authority/enrollment credentials are write-only and protected from ordinary config reads. The BLE pairing passkey is a per-device random secret that is never derived from public data. As a settings path it is read-only (`writable: false`; an `apply_config` naming it is rejected as `read_only`), and the target exposes it only over authenticated management, on the device's own display and over physical USB. It is changed by a Deployment-signed command or by USB, not by a settings write. The reference firmware returns it in `get_config` and, on a device with no Deployment record, also without authentication over BLE and the Wi-Fi companion port; after a signed passkey command of 0, `get_config` reports 0 although a fresh value is stored at the next boot. Private-key custody and identity rotation follow `SECURITY_AND_AUTHORITY.md`, not the ordinary settings path. Reset/reprovision operations must explicitly define whether they clear network credentials, backend authority, device identity and/or native MeshCore state; a generic 'factory reset' must not ambiguously claim to clear everything.

## Outage/resilience settings

The common model exposes the configuration required for the private resilience channel, including enabled/configured state, channel key, member roster, configuration version, stock-readable-copy option and daily-status behaviour. Secrets such as channel keys are never returned in clear text.

The device reports roster/capacity constraints rather than relying on a backend hard-coded number.

## Native contacts/channels

Contacts and channels are native MeshCore state. MECON may expose them read-only through inventory/schema and may invoke native/provisioning operations required to complete an authorized send, but does not create a second declarative contact/channel database unless MeshCore 1.18 itself provides the authoritative mechanism.

Protected/managed-contact semantics, if the supported build evicts contacts under pressure, operate on the native contact table.

## Companion TCP/private legacy features

The private MVP exposed `mecon.companion_tcp.enabled` for an unauthenticated port-5000 Companion TCP listener; the reference firmware still ships it (default off, no authentication when on, latched at boot). This is **not automatically part of MECON 1.0**. After MeshCore 1.18 is released, retain it only if a concrete interoperability requirement remains and the security model explicitly documents the exposure. Backends must not assume it exists on v1 devices.

## Secrets

Secrets may be replaced but are never returned. A read/schema may expose `configured: true`, credential type or other non-secret metadata sufficient for administration, but not the secret itself.

## Unknown and unsupported settings

Unknown paths/operations are rejected explicitly. Type coercion is not used for security-sensitive settings. Unsupported settings are not silently accepted and ignored.

## Reference firmware settings

The 0.9x reference firmware's schema contains: `mesh.name`; `mesh.radio.{frequency_mhz,bandwidth_khz,spreading_factor,coding_rate}`; `mesh.power.{battery_low_mv,battery_critical_mv}`; `mesh.location.{lat,lon,source,fix_age_s,advert}`; `mesh.gps.enabled`; `mesh.channels[N].{name,slot}` (read-only); `mecon.wifi_profiles[0-2].{ssid,password}`; `mecon.mqtt.{broker_uri,broker_username,broker_password}`; `mecon.companion_tcp.enabled`; `mecon.companion_ble.enabled`; `mecon.connectivity.mode`; and `mecon.posture`. A Companion's GPS defaults on and advertising its location defaults off. Effects are `live` or `restart_required`, and Wi-Fi/MQTT changes are committed in two phases and reverted if the device does not return. Its base is MeshCore 1.17.1, so the native keys map to that release's preferences, not yet to 1.18's. On the Wio repeater only `mesh.name`, `mesh.location.lat`, `mesh.location.lon`, `mesh.location.advert` and `mesh.gps.enabled` can be written over RF; `mesh.radio.*` cannot.

## Roles

Companion and Repeater share the logical MECON command/settings mechanism; applicability is schema/capability-driven. A client must not infer a setting solely from the role label.

## Versioning and 1.18 freeze

The configuration profile is independently versioned. Additive schema fields are forward-compatible; semantic changes to transaction/path meaning require a profile version change.

Before contract 1.0 is frozen, released MeshCore 1.18 is pinned and every native configuration requirement is mapped to its final upstream CLI/configuration operation. Only true MECON extensions receive new commands/paths.