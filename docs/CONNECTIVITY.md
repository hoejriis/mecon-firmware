# Connectivity and operating modes

Connectivity is additive. The underlying MeshCore 1.18 role does not depend on MECON connectivity.

## Wi-Fi

MECON extends the MeshCore 1.18 Wi-Fi/configuration infrastructure to store **up to three ordered profiles**. The device connects to the highest-priority available configured network and falls back as availability changes without interrupting RF operation. Configuration changes should preserve a known-working path where practical.

## MQTT

MECON 1.0 uses **one active MQTT session**. A provisioned cloud broker provides the default remote path. When a compatible broker is discovered on the current local network, the device prefers that local broker; when it disappears, the device falls back to cloud.

Local broker discovery is automatic and non-blocking. Discovery establishes reachability, not authority: credentials and permitted operations remain provisioned configuration.

The private MVP's historical two-concurrent-broker model is not part of the public 1.0 target.

## USB and BLE

USB remains the universal local/recovery path. Companion builds preserve native MeshCore Companion behaviour and expose MECON-specific operations through the versioned direct tunnel where needed.

BLE uses **NimBLE** in the public refactor. Companion BLE remains usable independently of Wi-Fi/MQTT. Pairing credentials are device-specific and the PIN is shown on the existing front display while BLE is not connected.

Repeater BLE is capability-dependent; clients must discover support rather than assume it from role alone.

## Outage behaviour

- Wi-Fi/MQTT loss never stops MeshCore RF operation.
- When MQTT is unavailable, configured DM outage forwarding may use the configured private MeshCore channel.
- The device publishes its configured daily health/status report on that private channel.
- When the RF mesh path is unavailable, authorized DM/channel traffic may use the MQTT path through MECON infrastructure.
- Forwarding must prevent loops and unintended duplicate delivery.

## Browser support

Direct browser support targets desktop Chrome/Edge where Web Serial/Web Bluetooth are available. Browser support does not change firmware semantics.