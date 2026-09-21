# MECON 1.0 firmware implementation specification

This document is the **functional implementation checklist** for rebuilding mecon-firmware from the released MeshCore 1.18 baseline. Together with `docs/contract/`, it must be sufficient to determine what the firmware is expected to do without reading the private MVP source. The private MVP is evidence and a test oracle only.

The five historical private firmware contracts have been audited field-by-field/function-by-function at product-contract level in [`LEGACY_CONTRACT_AUDIT.md`](LEGACY_CONTRACT_AUDIT.md). Any implementation ambiguity should be resolved from the public contracts plus that classification, not by silently copying private source. Items marked VERIFY 1.18 are deliberately blocked on the released upstream API, not undocumented requirements.

## Release dependency and foundation

MECON 1.0 is blocked on MeshCore 1.18 being released/merged to upstream `main`. Pin a specific 1.18 revision, prove stock Companion and Repeater on Heltec V3/V4, then migrate those targets to pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x and NimBLE. Prove stock-derived behaviour again before adding MECON.

MECON must extend 1.18 facilities rather than recreate them: Wi-Fi/runtime configuration, native Companion/Repeater commands, radio settings, board preferences, UI and remote CLI/message primitives remain upstream-owned where they already solve the requirement.

## Supported targets

- Heltec V3 Companion
- Heltec V3 Repeater
- Heltec V4 Companion
- Heltec V4 Repeater

V3 and V4 are first-class targets. Hardware-specific USB, display, buttons, radio and power code belongs behind target abstractions. A target is supported only after the real-hardware gate passes.

## Native MeshCore behaviour

MECON is additive. With Wi-Fi, MQTT and all backends unavailable, the device must still perform its normal MeshCore 1.18 role. Companion remains a normal Companion; Repeater remains a normal Repeater. MECON must not introduce a proprietary RF role.

The Companion contact capacity is a measured resource limit, not a hard-coded product requirement. Determine it after IDF5/NimBLE migration under realistic Wi-Fi+TLS+MQTT+BLE load. Managed/protected contacts must not be evicted by ordinary learned contacts.

## Wi-Fi

- Store up to three ordered Wi-Fi profiles.
- Extend the MeshCore 1.18 Wi-Fi/configuration model rather than maintaining a parallel private implementation.
- Try configured profiles in user-entered priority order, while avoiding needless roaming away from a working connection.
- Automatically reconnect/fail over when the active network disappears.
- A configuration change must not strand the device: preserve a known-working profile until replacement connectivity is proven where practical.
- Report actual active slot, actual associated SSID and nullable RSSI separately from configured values.
- Wi-Fi loss must not affect RF or local USB/BLE operation.
- Wi-Fi/DNS/discovery operations must be non-blocking with respect to radio servicing.

## MQTT

MECON uses **one MQTT client/session at a time**.

- A provisioned cloud broker profile is the fallback/default remote path.
- The device automatically discovers a compatible local broker through mDNS when on a LAN.
- A trusted/compatible discovered local broker is preferred over cloud.
- If local becomes unavailable, reconnect to cloud; if an eligible local broker later becomes available, selection may return to local according to the connection policy.
- Broker switching must not duplicate state-changing jobs or RF transmissions.
- MQTT uses encrypted authenticated transport and explicit authority grants.
- Discovery grants no authority.
- MQTT failure never stops RF.
- Publish connection state, selected path/profile identity and errors through status/health.
- Do not implement the private concurrent-multi-broker architecture.

## Common management/event model

MQTT, USB and BLE are transports for one logical MECON capability model. Do not implement separate product semantics per transport.

Expose, where supported by role/transport:

- identity, firmware/base versions, hardware, role, capabilities and profile versions;
- health and runtime status;
- raw packet observations with reception metadata;
- relevant decoded/native Companion events;
- logs/diagnostics;
- contact/channel inventory;
- configuration schema, reads and atomic validated writes;
- messaging operations;
- allowed administrative actions;
- resilience/backfill lifecycle;
- OTA lifecycle and results;
- framebuffer export for automated display verification.

Reuse native MeshCore 1.18 operations whenever they already provide the required semantics. MECON-specific functionality uses the versioned public envelopes in `docs/contract/`.

## USB and BLE

USB is the universal local recovery/configuration path. Companion retains standard MeshCore Companion USB framing and adds the MECON extension/tunnel only for operations not adequately represented upstream. Clients tolerate unsolicited push frames interleaved with replies. Repeater exposes a documented direct USB management/observation path mapped to the same logical operations.

The obsolete first-boot/time-limited line-JSON provisioning console is not reintroduced. Configuration/recovery is available throughout device life.

Companion BLE uses NimBLE, retains standard MeshCore interoperability, starts independently of successful Wi-Fi/MQTT, and exposes applicable MECON operations. BLE credentials/PIN are per-device, not universal. Repeater BLE is capability-driven; 1.0 does not require it unless the supported target advertises it.

A bounded local raw-observation diagnostic stream is retained where supported; it drops diagnostic records rather than block RF and reports dropped-record count.

## Device UI

Keep the MeshCore 1.18 Companion/Repeater UI and button semantics as intact as possible.

- Show `MECON <version>` at boot; no build date is required on the display.
- Add compact Wi-Fi, MQTT and BLE connection state to the existing front screen.
- When BLE is available but not connected, show the pairing PIN; when connected, show connected state instead.
- No MECON secondary/status screen in 1.0.
- DMs and messages on favourited channels may wake/show on the display.
- Public/unfavourited channel traffic does not wake the display merely because MECON observed it.
- No new normal MECON button semantics in 1.0.
- Framebuffer export is a test/management capability, not another screen.

## Observations and inventories

Every supported build can expose received MeshCore packet observations when authorized. Preserve raw packet bytes plus reception time, RSSI, SNR and receiving device identity. Multiple receivers hearing the same packet produce separate observations. Observation delivery is best-effort while no authorized transport exists; do not promise an unbounded offline packet queue. Publish counters reveal observations tried, accepted/refused, no-target and size/serialization drops.

Companion exposes contacts/channels and related native state required by a Reader/backend. Contact count and actual runtime capacity are reported. Protected/managed contacts and eviction count are reported where applicable. Repeater exposes its applicable radio/runtime state. Capability discovery, not role-name guessing, determines what a client may request.

## Messaging

Companion preserves native MeshCore messaging and additionally permits authorized MECON messaging jobs. Sending requires explicit authority and idempotent job handling. Contacts/channels may be provisioned on demand where required by the native MeshCore operation.

When the RF mesh is unavailable, authorized DM/channel messages may be submitted through MQTT for delivery through MECON infrastructure. This is an alternate path, not a change to the MeshCore RF protocol. Stable logical message identity prevents duplicate conversations/display when paths overlap.

## Outage resilience

The 1.0 target retains the private MVP resilience behaviour:

- when MQTT/IP is unavailable, selected DM traffic can be forwarded to a configured private MeshCore channel;
- configuration is explicit/versioned and includes a channel key and bounded member roster;
- forwarding uses an authenticated/versioned group-data envelope with stable origin identity and replay protection;
- attribution hints are not cryptographic identity proof;
- initial forwarding rate safety is one event per 30 seconds unless 1.18 hardware validation documents a replacement;
- overlength authenticated forwards are dropped, not silently truncated;
- every private-channel member can read forwarded DM content and the operator must be told;
- optional stock-readable duplicate copies are off by default, cannot trigger injection/recursion, and require privacy/airtime warnings;
- loop/duplicate prevention is mandatory when connectivity returns;
- the device publishes a bounded autonomous daily health/status report to that configured private channel;
- delivery/backfill synchronization preserves recoverable messages within explicitly bounded storage/protocol rules.

The legacy binary envelope semantics are documented in `contract/MQTT_PROTOCOL.md`; freeze the exact v1 byte layout only after validating against released MeshCore 1.18.

## Configuration

One logical settings API applies across MQTT, USB and BLE. It covers at least:

- native MeshCore name/radio/TX/location settings exposed by 1.18;
- read-only/native channel/contact visibility and applicable provisioning operations;
- three Wi-Fi profiles and ordering;
- cloud MQTT endpoint/credentials/namespace/authority;
- local-broker discovery policy and trust/authority identifiers;
- private outage/status channel, roster and stock-copy option;
- logging/health/reporting settings;
- OTA policy/trust metadata where configurable.

Secrets are write-only. Apply validates the complete requested transaction before persistence. Each field declares type, constraints, role applicability and runtime effect. Requested configuration is not reported as successful until it is actually accepted/applied. Connectivity-affecting changes preserve/roll back to a working path where practical.

The private unauthenticated Companion TCP listener is not automatically a 1.0 feature; retain only if a concrete post-1.18 interoperability need is documented.

## Health, status and logs

Expose enough state to operate and diagnose a remote device without serial access. The required logical health set is normative in `contract/CORE_DEVICE_CONTRACT.md` and includes:

- uptime/reset reason/abnormal reset and bounded previous crash record;
- current/minimum heap and loop-stack headroom;
- battery millivolts where measurable;
- Wi-Fi actual slot/SSID/RSSI;
- MQTT selected path/state/counters;
- BLE state;
- RF packet/raw/error counters, counter epoch, RX age and software/chip receive state where measurable;
- receiver liveness/rearm evidence if retained after 1.18 validation;
- observation publisher counters;
- contact count/capacity/protected/evicted where applicable;
- coalesced status-event counter;
- OTA state and relevant errors.

`null`, absent and zero have distinct meanings. Logs are bounded and must not block RF processing or leak secrets.

## OTA

Managed OTA is a supported 1.0 capability. MQTT may trigger OTA, but authority permits only an approved release represented by signed/integrity-checked metadata compatible with hardware, role and variant. Report accepted/progress/result states and confirm the post-reboot firmware version. Preserve a documented recovery/rollback path; USB remains ultimate recovery.

## Security and authority

Separate observation, config read, messaging, management/admin, resilience/backfill and OTA authority. A broker connection alone does not grant all operations. State-changing jobs use stable IDs and replay/idempotency protection. Secrets are never returned in ordinary status/config reads. No management transport exposes arbitrary firmware execution, unrestricted shell or raw NVS merely because it is authenticated.

Private resilience-channel authentication proves channel membership, not individual sender attribution. Stock-readable copies are explicitly unauthenticated channel text.

## Runtime/resource requirements

- pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x.
- NimBLE.
- One active MQTT client.
- Non-blocking DNS/mDNS/network/OTA work relative to RF servicing.
- Measure V3 and V4 memory budgets after runtime migration before fixing contact capacity and buffer sizes.
- Test realistic persisted contacts/channels/configuration and simultaneous Wi-Fi+TLS+MQTT+BLE on Companion.
- Avoid copying private-MVP static allocations where the new runtime permits a better design.

## Release and compatibility

Report MECON firmware version, exact MeshCore base, hardware target, role/build variant and independent contract/profile versions. Clients gate behaviour on capabilities/profile versions, not firmware-number inference. All supported board/role targets pass real-hardware gates including RF role, USB, applicable BLE, all Wi-Fi slots, local/cloud MQTT switching, observations, config, messaging, resilience/backfill, health diagnostics, reconnect, memory headroom and OTA.

## Backend compatibility

MECON 1.0 intentionally changes parts of the private MVP wire contract. The firmware implements the clean public contract rather than preserving private names/architectural debt internally. **All backend work required to move the current MeshContinuum backend from the private MVP to MECON 1.0 is tracked in `BACKEND_MIGRATION.md`.** Compatibility adapters may exist during rollout, but they are migration code and not part of the canonical firmware architecture.