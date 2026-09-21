# MQTT protocol

## Connection model

MECON 1.0 uses **one active MQTT client/session**. The device normally has a provisioned cloud endpoint. When a compatible broker is discovered on the current LAN, that local endpoint is preferred; otherwise the device uses cloud. Local discovery is non-blocking and does not itself grant authority.

The private MVP's two-concurrent-broker model is not part of this contract.

## Namespace

The active authority supplies a backend-neutral namespace. Canonical surfaces include status, observations, events, inventory, results, configuration, commands and OTA beneath `{namespace}/{device_id}/...`.

## Envelope

MECON messages identify protocol/version, device, stable message/job identity and timestamp. State-changing jobs are idempotent and duplicate delivery must not repeat a completed RF send or configuration change.

## Observations

Packet observations preserve raw MeshCore packet bytes plus receiver metadata such as RSSI/SNR and reception time. The device does not promise an unbounded offline packet queue.

## Messaging and resilience

Authorized MQTT may carry DM/channel messaging when the RF mesh path is unavailable. When MQTT/IP is unavailable, configured DM outage forwarding may use the configured private MeshCore channel instead. Implementations must prevent loops and unintended duplicate delivery across path changes.

## Compatibility

Protocol/profile versions determine wire compatibility. Unknown fields are ignored; unsupported operations return explicit failures when a job can be identified.