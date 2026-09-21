# Security and authority contract

## Authority classes

MECON grants explicit capabilities such as observe, read configuration, message, manage, admin and OTA. A weaker class never implies a stronger one.

## MQTT authentication

Only one MQTT session is active at a time. Local broker discovery selects a candidate endpoint but **does not establish trust**. The endpoint must satisfy provisioned authentication and authorization. Switching local/cloud paths must not broaden authority.

## Replay and idempotency

Remote state-changing jobs carry stable identifiers and replay protection across reconnects, local/cloud endpoint changes and direct transports. The same logical job must not execute twice because connectivity changed.

## Credentials

Wi-Fi/MQTT credentials are write-only. BLE pairing credentials are device-specific. On display-capable devices the BLE PIN may be shown locally while unconnected, but is not exposed as an ordinary remotely readable setting.

## OTA

OTA authority permits installation only of approved signed/integrity-checked firmware compatible with hardware, role and variant. MECON 1.0 release firmware is based on the pinned MeshCore 1.18 lineage plus the documented MECON runtime changes; arbitrary URL/binary execution is not an OTA authority.