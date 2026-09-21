# Device settings contract

Configuration is one logical model across supported MQTT, USB and BLE paths, while native MeshCore 1.18 configuration primitives are reused wherever they already provide the required semantics.

## Operations

MECON-specific configuration uses `get_config` and atomic `apply_config`. Transport adapters do not redefine their meaning.

## Namespace

Canonical MECON paths use neutral names such as `mecon.wifi.profiles.*`, `mecon.mqtt.*` and `mecon.security.*`. Native mesh/radio/channel settings should map to MeshCore 1.18's canonical configuration model rather than being duplicated under MECON solely for transport convenience.

## Wi-Fi

MECON extends upstream Wi-Fi support to **three ordered profiles**. Applying Wi-Fi changes should preserve a known-working management path where practical.

## MQTT

Settings describe a **single active-session architecture**: configured cloud endpoint/credentials/namespace plus local-broker discovery/preference and authority information. The public target does not expose two concurrent broker slots.

## Schema metadata

Settings advertise type/constraints, readability/writability, secret status, role/capability applicability, application effect and connectivity impact.

## Secrets

Secrets may be replaced but are never returned. Reads expose safe metadata only.

## Roles

Companion and Repeater share the logical MECON settings mechanism; applicability is capability/schema-driven.