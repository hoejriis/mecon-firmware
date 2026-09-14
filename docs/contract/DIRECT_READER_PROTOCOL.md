# Direct USB/BLE Reader protocol

## Goal

A compatible desktop web Reader can use a Companion without Wi-Fi by connecting over USB or BLE. The direct path exposes the same logical MECON operations as MQTT wherever applicable rather than defining a second management API.

## Companion transports

- USB: standard MeshCore Companion framing remains intact.
- BLE: standard MeshCore Companion service remains intact.
- MECON extension: a reserved, versioned tunnel frame carries MECON JSON envelopes over either transport.

The tunnel supports at least:

- status/capabilities;
- configuration schema/read/apply;
- observations;
- health;
- allowed administrative operations;
- Companion messaging/inventory operations not already adequately represented by the native Companion protocol.

## Repeater direct access

Repeaters expose a documented USB interface for status/configuration and raw observation streaming. The target semantics must map to the same core/configuration envelopes even if the underlying serial framing differs from Companion framing. Repeater BLE is not required for the initial release.

## Browser modes

A Reader may:

- operate the device locally with no backend;
- bridge device envelopes to a backend using the backend's own authenticated browser-agent transport;
- later synchronize locally retained observations/messages without changing their original identities.

The backend bridge itself is not a firmware protocol and is therefore outside this contract.

## Security

Direct access never exposes an unrestricted execution channel. BLE pairing uses device-specific credentials. Sensitive values remain write-only. Native MeshCore private identity export follows the explicit MeshCore/MECON identity-management operation and is never included in ordinary status.