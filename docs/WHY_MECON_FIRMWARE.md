# Why mecon-firmware?

MeshCore is valuable because the mesh continues to work without cloud infrastructure. mecon-firmware starts from that property rather than replacing it.

A stock-style Companion or Repeater is excellent at its RF role, but a permanently or semi-permanently deployed device also benefits from several optional paths around the radio: remote configuration, packet observation, remote message access, health reporting, and a direct browser connection when no network exists.

mecon-firmware adds those paths while keeping the native MeshCore role authoritative.

## Design goals

- **Offline first:** RF behavior does not depend on Wi-Fi, MQTT, Internet or a backend.
- **One everyday firmware:** Wi-Fi, USB and BLE are available without choosing a special connectivity build; the Companion memory profile is deliberately bounded to 64 contacts.
- **Redundant access:** three Wi-Fi profiles, two MQTT brokers and direct USB/BLE access reduce dependence on any single network path.
- **Backend neutrality:** the wire contract belongs to this firmware project, not to MeshContinuum or any other service.
- **Least authority:** observing packets, sending messages and administering a device are separate permissions.
- **Upstream friendliness:** new MeshCore releases should be mergeable with a small, documented MECON patch surface.

## Relationship to MeshContinuum

[MeshContinuum](https://github.com/hoejriis/MeshContinuum) is the sister project and reference implementation of a MECON-compatible backend and web Reader. The projects release together initially, but neither is intended to be a private API of the other.

A third-party project should be able to implement the contracts in `docs/contract/`, provision a device with its own brokers and credentials, and use the supported functionality without running MeshContinuum.