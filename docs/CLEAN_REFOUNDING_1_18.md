# Clean refounding on MeshCore 1.18

## Approved programme — 25 October 2026

There is **no standalone-release prerequisite** for any of this work; release timing, production fleet migration and public Backend publication are separate decisions. There is no programme index Issue: each item below is owned by its own Issue.

Done on `main`: [platform consolidation (#93)](https://github.com/hoejriis/mecon-firmware/issues/93) (ESP-IDF 5.3, pioarduino and NimBLE on the Heltec V3 and V4) and the [common device interface (#94)](https://github.com/hoejriis/mecon-firmware/issues/94) (see [DEVICE_INTERFACE](contract/DEVICE_INTERFACE.md); evidence in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md)).

Generic P3, for any conforming manager and not tied to a particular Backend, broker, domain or MECON workflow. All are planned, not built:

- [#95](https://github.com/hoejriis/mecon-firmware/issues/95): device identity, authority and recovery.
- [#96](https://github.com/hoejriis/mecon-firmware/issues/96): full telemetry. Only the `health` stream is on `main`.
- [#109](https://github.com/hoejriis/mecon-firmware/issues/109): managed operations.
- [#110](https://github.com/hoejriis/mecon-firmware/issues/110): signed updates over IP, USB and BLE.
- [#114](https://github.com/hoejriis/mecon-firmware/issues/114): private management channels.
- [#115](https://github.com/hoejriis/mecon-firmware/issues/115): authenticated peer-to-peer and private-channel LoRa management.
- [#116](https://github.com/hoejriis/mecon-firmware/issues/116): compact LoRa telemetry.
- [#117](https://github.com/hoejriis/mecon-firmware/issues/117): four-board conformance.
- [#1](https://github.com/hoejriis/mecon-firmware/issues/1): T1000-E.
- [#112](https://github.com/hoejriis/mecon-firmware/issues/112) and [#113](https://github.com/hoejriis/mecon-firmware/issues/113): generic correctness.

The permission defect [#111](https://github.com/hoejriis/mecon-firmware/issues/111) is fixed on `main` (PR #124).

Later, MECON-specific (P4), planned: [#118](https://github.com/hoejriis/mecon-firmware/issues/118) Deployment mapping, membership and ownership; [#119](https://github.com/hoejriis/mecon-firmware/issues/119) messaging; [#120](https://github.com/hoejriis/mecon-firmware/issues/120) backfill; [#121](https://github.com/hoejriis/mecon-firmware/issues/121) outage and sync behaviour; [#122](https://github.com/hoejriis/mecon-firmware/issues/122) MECON RF health policy; [#123](https://github.com/hoejriis/mecon-firmware/issues/123) LoRa image delivery. MECON consumes the generic private management channel later as a sync facility.

Technical design is part of each item. Unsettled design is not settled in these pages.

**Reader boundary:** the Reader requires a reachable Backend. When connected to a device over USB or BLE, it acts as a bridge between that device and the Backend. MQTT and bridged USB/BLE carry the same logical interface and full applicable telemetry, with explicit hardware, authorization, bandwidth and resource limits. Offline Reader behavior is not designed or included here. Native MeshCore clients and physical device recovery remain separate capabilities.

**Screen write-back:** device → Backend diagnostic/logging telemetry, alongside events, battery and environmental readings. This means captured screen/framebuffer data, not commands to draw content onto the display. Collection is bounded, capability-dependent and subject to privacy/access rules.

Everything from "Stage A" to the end of the Stage sections below is **historical and non-normative**. It records the plan of September to October 2026 as written then. It is not the current programme, and its status text is not current evidence. Current evidence is [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md) and the README.

## Why MECON firmware exists

Stock MeshCore provides independent LoRa messaging and device roles. MECON firmware starts by improving the **standalone device**—performance, security, connectivity, usability, and administration—without requiring a MECON account, Backend, Deployment, or cloud service. Later stages add interoperable MECON management and connected services.

This is a **clean implementation on a pinned upstream MeshCore 1.18.0 baseline**, not a code port of the private 0.9x MVP. The private MVP is design and test inspiration only: no obligation to preserve its internal architecture, implementation details, transport workarounds, or legacy wire formats. Public design, tests, and upstream interfaces are authoritative. If a cleaner firmware design requires changes to a MECON Backend, **the cleaner firmware design wins**; compatibility and migration are addressed explicitly, not by perpetuating technical debt.

## Stage A — standalone enhanced MeshCore (historical, non-normative)

**Status as of the original plan, kept for context. See the [README](../README.md) and [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md) for what is verified now:**

| # | Target | State |
|---|---|---|
| 1 | Platform and portability | Baseline pinned and reproducible container build; full board set compiled in CI (594 of 618 environments, the 24 failures are stock's). NimBLE and ESP-IDF 5.3 are done on the Heltec V3 and V4 (see [PLATFORM](PLATFORM.md)); the hardware-verified matrix is [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md) |
| 2 | Security and duty cycle | Built: [postures and audit log](STANDALONE_POSTURES.md), transport trust, regulatory duty-cycle enforcement, command replies bound to the transport the command arrived on. BLE rules covered by unit tests, a simulated pairing link and real V3 and V4 BLE runs in HARDWARE_VERIFIED |
| 3 | Wi-Fi controller | Built: up to ten prioritised profiles ([WIFI](WIFI.md)) |
| 4 | MQTT controller | Built: up to six broker profiles, one session, failover by priority, TLS pinning (fingerprint or CA, SNI), LAN discovery ([MQTT](MQTT.md)); credentials are write-only. DNS and dynamic DNS are handled by re-resolving on every attempt |
| 5 | Screen UI and interfaces | Built for the Heltec V3/V4 standalone Companion: BLE or Wi-Fi mode switching, connectivity page, button gesture ([CONNECTIVITY_MODE](CONNECTIVITY_MODE.md)). USB answering in both modes is checked on a V4 (HARDWARE_VERIFIED) |
| 6 | Adaptive contact memory | Built ([CONTACTS](CONTACTS.md)) |
| 7 | Generalized CLI | Built: one engine and privilege table over USB, BLE, LoRa remote and MQTT, `mecon.*` commands ([CLI](CLI.md)) |

The text below is the original plan and acceptance criteria, kept as written.

1. **Platform and portability:** pin upstream 1.18.0, reproducible builds, performance and heap improvements (evaluate NimBLE where suitable), preserve stock functionality and interoperability. Maintain a small hardware-verified board matrix and compile/software-verify the full upstream board set. Exclude only boards with documented demonstrated hardware/resource issues; feature availability may vary by hardware.
2. **Security and regulatory hardening:** define standalone Normal and Hardened postures with auditable authorization, BLE pairing/credential protections, and firmware-enforced legal duty-cycle limits for *all* transmissions. No MECON Deployment dependency.
3. **Wi-Fi controller:** build on upstream 1.18 facilities; persist prioritized profiles (three minimum, target ten if feasible within memory; ten built), reconnect/failover, and secure credential handling. Avoid duplicating upstream Wi-Fi implementation.
4. **MQTT controller:** configure multiple brokers, credentials, DNS and dynamic DNS provisioning, status and reconnection. **Start with priority-based failover, one active broker at a time**: parallel broker connections previously posed heap challenges and are not a Stage A requirement. Keep transport/broker management independent of MECON services.
5. **Screen UI and interfaces:** show Wi-Fi and MQTT status; switch between Wi-Fi mode and BLE mode; **no simultaneous BLE+Wi-Fi requirement**. Keep functional USB management available whenever hardware permits, including during mode switching. Upstream 1.18 USB/serial constraints must be validated per board; unsupported hardware exceptions must be explicit.
6. **Adaptive contact memory:** replace the fixed 50-contact ceiling with capacity-aware storage, keeping favourites protected from automatic eviction; evict least-recently-meaningful non-favourites when necessary. Separate persistent storage capacity from RAM cache and test low-memory behavior.
7. **Generalized CLI access:** one command/authorization engine with USB, BLE, MQTT, and LoRa adapters; preserve upstream MeshCore CLI compatibility. Add missing standalone operations through the upstream external CLI hook as `mecon_*` extensions. A command's privilege depends on caller authorization and transport; remote access must not inherit local USB privileges. No MECON enrollment required.

**Architecture:** separate CLI command semantics from transport, authentication/authorization and device-specific adapters. Wi-Fi and MQTT controllers are separate, coordinated modules. Preserve ordinary MeshCore messaging and offline use. Use upstream capabilities rather than duplicating them; upstream suitable generic improvements where practical.

**Original Stage A acceptance (historical):** reproducible upstream-based builds; applicable automated software checks for upstream boards; hardware evidence for a limited verified matrix; security and duty-cycle tests; standalone CLI operations over each supported transport; safe USB recovery; Wi-Fi and MQTT failover tests; UI mode switching; favourite-preserving memory-pressure tests. Do not claim every feature works on every board merely because it compiles.

## Stage B — MECON-specific functionality (historical, non-normative)

This section predates the approved programme above, which places generic signed management, telemetry and updates in P3 and only the MECON-specific mapping in P4. As originally written: add Deployment identity and key custody; device enrollment and recovery; signed and authorized management; interoperable MQTT and observation uplinks; compatible Reader/Backend configuration and messaging; OTA management (including supported LoRa paths); federation and multi-Backend integration as applicable. These must build on Stage A's generic CLI, security, connectivity and storage mechanisms. Keep ordinary MeshCore operation usable without MECON services.

The public [README](../README.md) describes what is built; the [product target](PRODUCT_TARGET.md), [security model](SECURITY_MODEL.md), and [contracts](contract/README.md) describe the broader destination, not implementation dependencies. The separate private MVP provides historical lessons, not a code baseline.

## Development principles

- Publish no private-repository internals, device credentials, infrastructure details, security secrets, or private issue references.
- A fresh 1.18 architecture is preferred to copying legacy private firmware; use private MVP only as inspiration.
- Document upstream differences and test them; contribute generic improvements upstream where practical.
- Prioritize low heap usage and reliable recovery over speculative concurrency.
- Keep public issue scopes independently testable and avoid premature Backend coupling.
- Make any later Backend compatibility changes explicit; never compromise a clean firmware interface to preserve a private implementation.
