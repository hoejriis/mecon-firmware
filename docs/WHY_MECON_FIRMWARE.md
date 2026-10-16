# Why mecon-firmware?

> **Target documentation:** this public repository has no installable release yet. Public releases start at **0.1.0**, a standalone enhanced MeshCore on 1.18.0 (see [Clean refounding on MeshCore 1.18](CLEAN_REFOUNDING_1_18.md)); the first version to serve a MECON fleet is planned as 0.8.0 (see [Versioning and releases](VERSIONING_AND_RELEASES.md)). This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

MeshCore is valuable because the mesh continues to work without cloud infrastructure. MECON starts from that property rather than replacing it.

The public firmware (first release mecon-firmware 0.8.0) is deliberately built on **upstream MeshCore 1.18.0**, released on 2026-10-15. The newer upstream facilities are the foundation; the private reference firmware (the 0.9x line, built on 1.17.1) is evidence about useful behaviour and failure modes, not source architecture to preserve.

## Design goals

- **MeshCore first:** normal Companion/Repeater RF behaviour survives loss of MECON infrastructure.
- **1.18 first:** reuse upstream 1.18 Wi-Fi, configuration, command, UI and board facilities rather than recreating older equivalents.
- **Modern ESP32 baseline (target):** pioarduino / Arduino-ESP32 3.x / IDF 5.x and NimBLE are established before MECON features are layered on. The private reference gateways already use NimBLE but still build on Arduino-ESP32 2.x / IDF 4.4; IDF 5 is used only for a few ESP32-C6 candidate builds there.
- **Local first:** three ordered Wi-Fi profiles and one MQTT session that prefers a locally discovered compatible broker, then falls back to cloud.
- **One capability model:** MQTT, USB and BLE expose the same logical MECON operations where applicable.
- **Resilient messaging:** mesh can provide an outage path when IP is unavailable, and authorized MQTT infrastructure can provide an alternate path when RF is unavailable.
- **Backend neutrality:** the public contract belongs to this project, not MeshContinuum.
- **Measured resource limits:** contact capacity and other constraints are determined after the modern-runtime migration, not copied blindly from the private MVP.
- **Upstream friendliness:** MECON maintains a small, documented patch surface.

## Relationship to MeshContinuum

MeshContinuum is the sister project and reference backend/Reader. Third parties can implement `docs/contract/` without running MeshContinuum.