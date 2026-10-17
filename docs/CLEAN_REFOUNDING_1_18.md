# Clean refounding on MeshCore 1.18

## Why MECON firmware exists

Stock MeshCore provides independent LoRa messaging and device roles. MECON firmware starts by improving the **standalone device**—performance, security, connectivity, usability, and administration—without requiring a MECON account, Backend, Deployment, or cloud service. Later stages add interoperable MECON management and connected services.

This is a **clean implementation on a pinned upstream MeshCore 1.18.0 baseline**, not a code port of the private 0.9x MVP. The private MVP is design and test inspiration only: no obligation to preserve its internal architecture, implementation details, transport workarounds, or legacy wire formats. Public design, tests, and upstream interfaces are authoritative. If a cleaner firmware design requires changes to a MECON Backend, **the cleaner firmware design wins**; compatibility and migration are addressed explicitly, not by perpetuating technical debt.

## Stage A — standalone enhanced MeshCore (ranked implementation targets)

**Status (see the [README](../README.md) for how each is verified; everything is simulation-verified only, nothing is hardware-verified):**

| # | Target | State |
|---|---|---|
| 1 | Platform and portability | Baseline pinned and reproducible container build; full board set compiled in CI (594 of 618 environments, the 24 failures are stock's). NimBLE and heap work not started; the hardware-verified board matrix does not exist yet |
| 2 | Security and duty cycle | Built: [postures and audit log](STANDALONE_POSTURES.md), transport trust, regulatory duty-cycle enforcement. BLE rules covered by unit tests only |
| 3 | Wi-Fi controller | Built: up to ten prioritised profiles ([WIFI](WIFI.md)) |
| 4 | MQTT controller | Built: up to six broker profiles, one session, failover by priority, TLS pinning (fingerprint or CA, SNI), LAN discovery ([MQTT](MQTT.md)); credentials are write-only. DNS and dynamic DNS are handled by re-resolving on every attempt |
| 5 | Screen UI and interfaces | Built for the Heltec V3/V4 standalone Companion: BLE or Wi-Fi mode switching, connectivity page, button gesture ([CONNECTIVITY_MODE](CONNECTIVITY_MODE.md)). USB in both modes is assumed, not verified on hardware |
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

**Stage A acceptance:** reproducible upstream-based builds; applicable automated software checks for upstream boards; hardware evidence for a limited verified matrix; security and duty-cycle tests; standalone CLI operations over each supported transport; safe USB recovery; Wi-Fi and MQTT failover tests; UI mode switching; favourite-preserving memory-pressure tests. Do not claim every feature works on every board merely because it compiles.

## Stage B — MECON-specific functionality (later, not prerequisites for Stage A)

Add Deployment identity and key custody; device enrollment and recovery; signed and authorized management; interoperable MQTT and observation uplinks; compatible Reader/Backend configuration and messaging; OTA management (including supported LoRa paths); federation and multi-Backend integration as applicable. These must build on Stage A's generic CLI, security, connectivity and storage mechanisms. Keep ordinary MeshCore operation usable without MECON services.

The public [README](../README.md) describes what is built; the [product target](PRODUCT_TARGET.md), [security model](SECURITY_MODEL.md), and [contracts](contract/README.md) describe the broader destination, not Stage A implementation dependencies. The separate private MVP provides historical lessons, not a code baseline.

## Development principles

- Publish no private-repository internals, device credentials, infrastructure details, security secrets, or private issue references.
- A fresh 1.18 architecture is preferred to copying legacy private firmware; use private MVP only as inspiration.
- Document upstream differences and test them; contribute generic improvements upstream where practical.
- Prioritize low heap usage and reliable recovery over speculative concurrency.
- Keep public Stage A issue scopes independently testable and avoid premature Backend coupling.
- Make any later Backend compatibility changes explicit; never compromise a clean firmware interface to preserve a private implementation.
