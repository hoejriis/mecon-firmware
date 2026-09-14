# Device settings contract

Configuration is one logical API across MQTT, USB and BLE.

## Operations

- `get_config` — current non-secret values plus schema.
- `apply_config` — validate and atomically apply a set of changes.

Transport adapters carry these operations without redefining their semantics.

## Namespace

Canonical settings use neutral paths:

```text
mesh.name
mesh.radio.*
mesh.location.*
mesh.channels.*
mecon.wifi.profiles.*
mecon.mqtt.brokers.*
mecon.security.*
```

Private deployment names and the legacy `deimos.*` namespace are not part of the public target.

## Schema metadata

Each setting advertises as applicable:

- type and constraints;
- readable/writable;
- secret/write-only;
- supported role(s);
- effect: `live`, `reconnect_required`, `restart_required`, or `reboot_required`;
- whether it disrupts RF or management connectivity.

## Transaction behavior

An apply job validates the complete requested set before persistence. Invalid jobs do not partially apply unrelated values. Successful values are persisted through canonical MeshCore/MECON storage and applied according to the declared effect.

Connection-changing jobs must acknowledge safely and retain/recover a known-working management path where practical. Wi-Fi changes must not destroy the last working profile before replacement connectivity is proven.

## Secrets

Secret settings may be replaced but are never returned. Reads expose only safe metadata such as configured state, SSID, broker host and redacted identifiers.

## Roles

Companion and Repeater use the same settings mechanism. Role-specific settings are expressed through schema applicability, not separate MQTT command families or a second Repeater-only management contract.