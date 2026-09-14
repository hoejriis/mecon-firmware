# Integrating with mecon-firmware

mecon-firmware is designed to be usable by MeshContinuum and by independent projects.

## Start with capabilities

Do not infer behavior from board name, role or firmware version. Read the device's protocol version, role, hardware target, build variant, profiles and capabilities, then expose only supported operations.

## Integration paths

A project may integrate through:

1. **MQTT** — remote status, observations, messaging and management.
2. **USB** — direct Companion/Repeater access and recovery.
3. **BLE** — direct Companion access.
4. **Hybrid browser bridge** — a browser talks USB/BLE to the device and carries the same logical envelopes to a backend.

The MQTT and direct paths intentionally share the same logical operation names and payload semantics wherever possible.

## Backend-neutral provisioning

Nothing identifying MeshContinuum is compiled into a supported public build. Provisioning supplies:

- Wi-Fi profiles;
- up to two MQTT broker profiles;
- per-broker credentials;
- per-broker topic namespace;
- backend-assigned device identifier where needed;
- authorization grants/tokens.

Default examples in public releases must be neutral and must not silently enroll a device into a MeshContinuum-operated service.

## Minimum read-only integration

A useful read-only backend needs only the core profile: device status/capabilities plus packet observations. Messaging and administration are optional profiles and require explicit grants.

## Reference implementation

[MeshContinuum](https://github.com/hoejriis/MeshContinuum) is the reference backend/Reader. Its behavior may be used as interoperability evidence, but this repository's contract is normative when the two disagree.

## Compatibility

Integrations should gate on contract/profile versions, not mecon-firmware release numbers. Unknown fields must be ignored unless a profile version explicitly changes their semantics. Unknown capabilities must not be treated as authorization.