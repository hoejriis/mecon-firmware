# Why mecon-firmware?

MeshCore is valuable because the mesh continues to work without cloud infrastructure. MECON starts from that property rather than replacing it.

MECON 1.0 is deliberately built on **MeshCore 1.18 after it lands on upstream `main`**. The newer upstream facilities are the foundation; the private MVP is evidence about useful behaviour and failure modes, not source architecture to preserve.

## Design goals

- **MeshCore first:** normal Companion/Repeater RF behaviour survives loss of MECON infrastructure.
- **1.18 first:** reuse upstream 1.18 Wi-Fi, configuration, command, UI and board facilities rather than recreating older equivalents.
- **Modern ESP32 baseline:** pioarduino / Arduino-ESP32 3.x / IDF 5.x and NimBLE are established before MECON features are layered on.
- **Local first:** three ordered Wi-Fi profiles and one MQTT session that prefers a locally discovered compatible broker, then falls back to cloud.
- **One capability model:** MQTT, USB and BLE expose the same logical MECON operations where applicable.
- **Resilient messaging:** mesh can provide an outage path when IP is unavailable, and authorized MQTT infrastructure can provide an alternate path when RF is unavailable.
- **Backend neutrality:** the public contract belongs to this project, not MeshContinuum.
- **Measured resource limits:** contact capacity and other constraints are determined after the modern-runtime migration, not copied blindly from the private MVP.
- **Upstream friendliness:** MECON maintains a small, documented patch surface.

## Relationship to MeshContinuum

MeshContinuum is the sister project and reference backend/Reader. Third parties can implement `docs/contract/` without running MeshContinuum.