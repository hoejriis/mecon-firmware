# Connectivity and operating modes

> **Target documentation (planned and MECON-specific work; not implemented here unless a section says so).** The tagged releases v0.1.0 to v0.1.2 are source-only pre-releases without the device interface. Current `main` implements the generic [device interface v1](contract/DEVICE_INTERFACE.md) and the standalone features in the [README](../README.md), with real-board evidence in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). The generic work still planned (Issues #95, #96, #109, #110, #114 to #117) and the MECON-specific work (Issues #118 to #123) described here are not implemented. Release timing is a separate decision. This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

> **Built differently.** The connectivity mode built on `main` has two values, `ble` and `wifi`, one at a time, switched by `set connectivity.mode` or the device button ([CONNECTIVITY_MODE](CONNECTIVITY_MODE.md); checked on a V3 and a V4 in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md)). The `MQTT | BLE | Both` modes below are the target.

Connectivity is additive to the underlying MeshCore role, but MECON security posture controls which management transports may be initialized. RF operation must continue independently of IP connectivity.

## Security posture

> *Built differently.* The standalone Normal and Hardened postures that exist today are described in [Standalone security postures](STANDALONE_POSTURES.md). They differ from this MECON model: Hardened keeps USB management (for a cable holder, including `set posture normal confirm` and factory reset) and needs no Deployment, whereas the text below withholds stock USB management in Hardened and requires Deployment authority. Connectivity is BLE or Wi-Fi one at a time ([Connectivity mode](CONNECTIVITY_MODE.md)).

MECON defines two persistent operating postures plus a fail-secure recovery state:

- **Normal** — stock MeshCore interoperability and MECON convenience access are preserved.
- **Hardened** — a managed-infrastructure posture. Physical possession or proximity does not grant administration or message access.
- **Fail-secure recovery** — entered when persisted posture cannot be validated; it is not user-selectable and must never silently fall back to Normal.

In the private 0.9x reference firmware the posture ships on all four roles: the Heltec Companion gateways and the T1000-E, and, since 0.90.0, the Wio repeater and the ESP32 Repeater Observer. Entering Hardened requires a USB-auth secret to be set first (otherwise the change is refused as `no_usb_auth_secret`), so the device can always be unlocked by an authenticated Reader. The Wio and the T1000-E have a recovery-only image that clears the posture records and nothing else; a gateway recovers through the Reader unlock or a USB `deployment_reset`.

Posture changes require authenticated MECON management, persist across reboot, and take effect through a controlled reboot so services are initialized according to the selected posture rather than dynamically torn down as a security boundary.

## Normal connectivity modes

On Wi-Fi-capable MECON devices the Normal posture exposes three persistent connectivity choices:

- **MQTT** — the default. Wi-Fi/MQTT are initialized and BLE is not. This is the normal managed operating mode and maximizes heap headroom.
- **BLE** — contingency/direct-access mode. BLE is initialized and Wi-Fi/mDNS/MQTT are not.
- **Both** — explicit high-memory mode. BLE and Wi-Fi/MQTT are both initialized where hardware resources permit.

The connectivity selector exists only in Normal posture. Entering Hardened discards the previous Normal mode. Returning Hardened → Normal on a Wi-Fi-capable device starts in **MQTT**, not the previous BLE/Both choice.

Non-Wi-Fi targets expose only modes supported by their hardware. In the reference firmware `Both` is also not available on the Heltec V3 (a measured 9 KB of heap remains), so the V3 offers `MQTT` and `BLE` only, and a V3 found in `Both` is migrated to `MQTT`.

## Wi-Fi

MECON extends the MeshCore Wi-Fi/configuration infrastructure to store ordered profiles (three in the target; the standalone firmware implements up to ten, see [Wi-Fi profiles](WIFI.md)). The device connects to the highest-priority available configured network and falls back as availability changes without interrupting RF operation. Configuration changes should preserve a known-working path where practical.

In Hardened posture Wi-Fi is infrastructure/outbound only: MQTT and supporting DNS/NTP/OTA as required. Stock-app TCP/HTTP/Web UI or other inbound convenience-management surfaces are not exposed.

## MQTT and broker selection

MECON uses **one active MQTT session at a time**.

A provisioned cloud broker is the fallback/default remote path. A compatible local broker may be discovered through mDNS and preferred when reachable, but discovery proves only reachability. Broker trust and management authority are separate and derive from the Deployment authority model.

The device may switch between eligible local and remote endpoints without duplicating state-changing jobs or RF transmissions. MQTT failure never stops RF.

The historical private concurrent-multi-broker architecture is not the target architecture, and has not been the shipped one since reference firmware 0.51.0.

Reference behaviour today: one session, tried in the order local, primary, secondary broker (the device may store up to four broker profiles). A connect that has not completed in 45 s, or an unhealthy session, moves the device down the list; failure cooldowns start at 60 s and double up to 600 s. It moves up only from a settled session and to a path known to be reachable: back to the local broker after 30 s settled (cloud 2 min). A local broker that delivers no signed liveness beacon within 90 s of connecting is untrusted for 900 s; after a local session drops the cooldown before rediscovery is 30, 60, 120, 240, then 300 s. Silence from a cloud broker alone never condemns it. The transport is MQTT over WSS.

## BLE

Normal Companion BLE uses NimBLE and preserves applicable stock MeshCore interoperability. BLE credentials/PIN are per-device.

**Hardened firmware does not initialize BLE at all** — not merely disabled services. This removes the local attack surface and releases the associated heap.

Loss of MQTT/Wi-Fi in Hardened posture must never automatically enable BLE or another convenience interface.

## USB

In Normal posture, Companion retains stock MeshCore USB/WebSerial access alongside MECON Reader access. Repeater direct USB management is capability-driven.

In Hardened posture, stock USB/WebSerial management is unavailable. Unauthenticated USB exposes only minimal non-sensitive identification/status: device/hardware/firmware identity, Hardened state, and guidance that an authenticated MECON Reader is required. An authenticated Reader proving current Deployment authority may use the MECON management interface.

Full flash erase/reflash by a person with physical access is outside the enrolled-device trust model and remains the ultimate physical recovery mechanism.

## Secure RF management

Authenticated MECON management over MeshCore RF is a first-class management path where the target supports it. The reference firmware ships it as a compact Deployment-signed line on the Wio repeater and, for password and reboot, on the Observer, with a per-signer replay watermark and replies of at most 159 characters; at most four signature verifications are made per 10 s. It is particularly important for non-Wi-Fi repeaters. Hardened posture retains secure RF management; posture must not turn a deployed non-Wi-Fi device into an unmanageable appliance.

RF management is part of the same logical management capability model as MQTT and authenticated Reader USB, not a separate configuration language.

## Hardened local behaviour

A Hardened Companion is an infrastructure device rather than a user messaging terminal:

- it does not display received message content or sender/channel identities;
- it cannot initiate DM/channel messages from the local UI;
- local UI may show device name/role, Hardened indicator, power/battery, uptime, RF activity, Wi-Fi/MQTT state, GPS/fix state, aggregate traffic counts and sanitized operational diagnostics;
- no physical UI action may change posture, enable BLE, alter management/security connectivity, or factory-reset an enrolled device.

Authenticated management retains applicable detailed telemetry, diagnostics, contacts/channels administration and authorized actions.

## Outage behaviour

- The reference firmware limits its own transmit airtime to the legal duty cycle of the configured channel (10 % in 869.40-869.65 MHz, 1 % in 868.0-868.6 and 869.7-870.0 MHz, 0.1 % elsewhere); an operator setting can only tighten it. Past the budget it sheds its own adverts and flood forwards first, and reports the limit, budget and shed count in status.

- Wi-Fi/MQTT loss never stops MeshCore RF operation.
- Hardened never weakens posture because infrastructure is unavailable.
- Configured DM outage forwarding may use the configured private MeshCore channel where the role/posture permits the underlying messaging function.
- Authorized DM/channel traffic may use MECON infrastructure as an alternate path where supported.
- Forwarding must prevent loops and unintended duplicate delivery.

## OTA

Posture restricts authority and exposure, not authorized OTA capability. Hardened devices may accept OTA through authenticated network/MQTT management and authenticated Reader USB where supported. LoRa OTA is not a requirement of the target contract. The private 0.9x reference firmware ships it: an ESP32 gateway can take a full image relayed over one LoRa hop (the Deployment-signed manifest still verifies the image), and the Wio repeater stages an image without ever activating it. Whether the public line carries it is decided with the OTA contract.

In Normal `Both` mode an OTA coordinator may temporarily stop BLE to free heap without changing the persisted connectivity choice, then restore it after the OTA/reboot flow when appropriate. MQTT mode never loads BLE in the first place.

## Browser support

Direct browser support targets environments where the applicable Web Serial/Web Bluetooth APIs exist. Browser support does not change firmware semantics. Hardened posture deliberately does not expose stock browser/app convenience-management paths.