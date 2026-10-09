# Connectivity and operating modes

> **Target documentation:** this public repository has no installable release yet. See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

Connectivity is additive to the underlying MeshCore role, but MECON security posture controls which management transports may be initialized. RF operation must continue independently of IP connectivity.

## Security posture

MECON defines two persistent operating postures plus a fail-secure recovery state:

- **Normal** — stock MeshCore interoperability and MECON convenience access are preserved.
- **Hardened** — a managed-infrastructure posture. Physical possession or proximity does not grant administration or message access.
- **Fail-secure recovery** — entered when persisted posture cannot be validated; it is not user-selectable and must never silently fall back to Normal.

Posture changes require authenticated MECON management, persist across reboot, and take effect through a controlled reboot so services are initialized according to the selected posture rather than dynamically torn down as a security boundary.

## Normal connectivity modes

On Wi-Fi-capable MECON devices the Normal posture exposes three persistent connectivity choices:

- **MQTT** — the default. Wi-Fi/MQTT are initialized and BLE is not. This is the normal managed operating mode and maximizes heap headroom.
- **BLE** — contingency/direct-access mode. BLE is initialized and Wi-Fi/mDNS/MQTT are not.
- **Both** — explicit high-memory mode. BLE and Wi-Fi/MQTT are both initialized where hardware resources permit.

The connectivity selector exists only in Normal posture. Entering Hardened discards the previous Normal mode. Returning Hardened → Normal on a Wi-Fi-capable device starts in **MQTT**, not the previous BLE/Both choice.

Non-Wi-Fi targets expose only modes supported by their hardware.

## Wi-Fi

MECON extends the MeshCore Wi-Fi/configuration infrastructure to store up to three ordered profiles. The device connects to the highest-priority available configured network and falls back as availability changes without interrupting RF operation. Configuration changes should preserve a known-working path where practical.

In Hardened posture Wi-Fi is infrastructure/outbound only: MQTT and supporting DNS/NTP/OTA as required. Stock-app TCP/HTTP/Web UI or other inbound convenience-management surfaces are not exposed.

## MQTT and broker selection

MECON uses **one active MQTT session at a time**.

A provisioned cloud broker is the fallback/default remote path. A compatible local broker may be discovered through mDNS and preferred when reachable, but discovery proves only reachability. Broker trust and management authority are separate and derive from the Deployment authority model.

The device may switch between eligible local and remote endpoints without duplicating state-changing jobs or RF transmissions. MQTT failure never stops RF.

The historical private concurrent-multi-broker architecture is not the target architecture.

## BLE

Normal Companion BLE uses NimBLE and preserves applicable stock MeshCore interoperability. BLE credentials/PIN are per-device.

**Hardened firmware does not initialize BLE at all** — not merely disabled services. This removes the local attack surface and releases the associated heap.

Loss of MQTT/Wi-Fi in Hardened posture must never automatically enable BLE or another convenience interface.

## USB

In Normal posture, Companion retains stock MeshCore USB/WebSerial access alongside MECON Reader access. Repeater direct USB management is capability-driven.

In Hardened posture, stock USB/WebSerial management is unavailable. Unauthenticated USB exposes only minimal non-sensitive identification/status: device/hardware/firmware identity, Hardened state, and guidance that an authenticated MECON Reader is required. An authenticated Reader proving current Deployment authority may use the MECON management interface.

Full flash erase/reflash by a person with physical access is outside the enrolled-device trust model and remains the ultimate physical recovery mechanism.

## Secure RF management

Authenticated MECON management over MeshCore RF is a first-class management path where the target supports it. It is particularly important for non-Wi-Fi repeaters. Hardened posture retains secure RF management; posture must not turn a deployed non-Wi-Fi device into an unmanageable appliance.

RF management is part of the same logical management capability model as MQTT and authenticated Reader USB, not a separate configuration language.

## Hardened local behaviour

A Hardened Companion is an infrastructure device rather than a user messaging terminal:

- it does not display received message content or sender/channel identities;
- it cannot initiate DM/channel messages from the local UI;
- local UI may show device name/role, Hardened indicator, power/battery, uptime, RF activity, Wi-Fi/MQTT state, GPS/fix state, aggregate traffic counts and sanitized operational diagnostics;
- no physical UI action may change posture, enable BLE, alter management/security connectivity, or factory-reset an enrolled device.

Authenticated management retains applicable detailed telemetry, diagnostics, contacts/channels administration and authorized actions.

## Outage behaviour

- Wi-Fi/MQTT loss never stops MeshCore RF operation.
- Hardened never weakens posture because infrastructure is unavailable.
- Configured DM outage forwarding may use the configured private MeshCore channel where the role/posture permits the underlying messaging function.
- Authorized DM/channel traffic may use MECON infrastructure as an alternate path where supported.
- Forwarding must prevent loops and unintended duplicate delivery.

## OTA

Posture restricts authority and exposure, not authorized OTA capability. Hardened devices may accept OTA through authenticated network/MQTT management and authenticated Reader USB where supported. Future secure RF OTA may be supported if technically safe; LoRa OTA is not a requirement.

In Normal `Both` mode an OTA coordinator may temporarily stop BLE to free heap without changing the persisted connectivity choice, then restore it after the OTA/reboot flow when appropriate. MQTT mode never loads BLE in the first place.

## Browser support

Direct browser support targets environments where the applicable Web Serial/Web Bluetooth APIs exist. Browser support does not change firmware semantics. Hardened posture deliberately does not expose stock browser/app convenience-management paths.