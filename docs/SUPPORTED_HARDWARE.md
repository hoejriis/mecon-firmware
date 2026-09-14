# Supported hardware and roles

This is the **release-target matrix**. A target is called supported only after its real-hardware release gate passes.

| Hardware | Companion | Repeater | Wi-Fi | USB | BLE | MQTT |
|---|---:|---:|---:|---:|---:|---:|
| Heltec V3 | Yes | Yes | Yes | Yes | Companion | Up to 2 |
| Heltec V4 | Yes | Yes | Yes | Yes | Companion | Up to 2 |

## Companion profile

The normal Companion image:

- preserves standard MeshCore Companion behavior;
- uses **64 contact slots**;
- enables Wi-Fi, USB and BLE by default;
- stores up to three Wi-Fi profiles;
- supports up to two MQTT brokers;
- supports normal MeshCore messaging locally;
- supports MECON observations, remote management and messaging according to granted capabilities;
- supports direct browser operation over USB or BLE.

The 64-contact limit is a deliberate resource trade-off to provide memory headroom for simultaneous Wi-Fi, MQTT/TLS and BLE operation. It must be visible in release manifests and status.

## Repeater profile

The normal Repeater image:

- preserves upstream MeshCore repeating behavior;
- enables Wi-Fi and USB by default;
- stores up to three Wi-Fi profiles;
- supports up to two MQTT brokers;
- publishes observations and health where authorized;
- supports remote configuration where authorized;
- supports direct USB configuration and observation streaming.

Repeater BLE is not required for the initial public release.

## Experimental variants

Board revisions or memory/radio variants may be published as experimental artifacts. They must use distinct hardware/variant identifiers and cannot silently share a manifest with a hardware target whose radio or flash layout differs.

## Hardware gate

A supported release target must be verified on real hardware for at least:

- clean boot and upgrade boot;
- native MeshCore role behavior;
- USB operation;
- BLE Companion operation where applicable;
- all three Wi-Fi profile slots;
- both MQTT broker slots;
- packet observations;
- configuration read/write and recovery;
- messaging for Companion;
- reconnect after Wi-Fi/broker loss;
- memory headroom under realistic persisted state;
- signed OTA update and rollback/recovery where OTA is advertised.

Compilation alone is not a hardware gate.