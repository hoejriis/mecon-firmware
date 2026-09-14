# Connectivity and operating modes

Connectivity is additive. The underlying MeshCore role does not depend on a MECON connection.

## Local/offline

With no Wi-Fi and no backend, the device performs its native MeshCore role. Companion USB and BLE remain available. A compatible desktop Chrome/Edge Reader may connect directly and provide local messaging, configuration and observation access.

## Wi-Fi

A device stores **up to three independent Wi-Fi profiles**. Profiles contain SSID, secret and optional preference metadata. Selection must be deterministic, avoid needless roaming, and fall back among known networks when the active network disappears.

Changing Wi-Fi configuration must not destroy the last known-working profile before the replacement is proven usable.

## MQTT

A device stores **up to two broker profiles** and may maintain both concurrently. Each profile contains endpoint, authentication, namespace and authority grants.

Broker grants are independent. A broker may be authorized for some combination of:

- status/health;
- packet observations;
- decoded Companion events where permitted;
- messaging jobs;
- configuration reads;
- configuration writes;
- administrative actions;
- OTA management.

A broker that may observe must not automatically gain administration or transmit authority.

## USB

USB is the universal recovery/configuration path. Companion builds preserve the standard MeshCore Companion protocol and add a versioned MECON tunnel for MECON-specific operations. Repeater builds expose a documented direct USB management/observation interface without pretending to be a Companion.

## BLE

BLE is enabled by default on Companion builds and must be usable independently of Wi-Fi/MQTT state. It preserves normal MeshCore Companion interoperability and exposes the same MECON direct-operation tunnel as USB.

Credentials must be device-specific; a universal compiled-in passkey is not an acceptable release target.

## Browser support

Direct browser connectivity targets desktop **Chrome and Edge** using Web Serial and Web Bluetooth. Safari and iOS browsers are not part of the initial direct-connect compatibility target because the required browser transports are unavailable there.

## Failure behavior

Transport failure degrades only the capabilities carried by that transport. In particular:

- MQTT loss does not stop RF;
- Wi-Fi loss does not stop USB/BLE or RF;
- backend loss does not stop direct Reader operation;
- direct Reader disconnect does not stop MQTT or RF;
- one broker failure does not require disconnecting the other broker.