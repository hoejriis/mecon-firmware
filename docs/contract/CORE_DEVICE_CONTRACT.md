# Core device contract

## Identity

A device reports separately:

- `device_id` — backend-neutral stable MECON device identifier;
- `node_public_key_hex` — native MeshCore node identity;
- `hardware_target` — e.g. `heltec_v3`, `heltec_v4`;
- `role` — `companion` or `repeater`;
- `build_variant` — release artifact variant;
- `mecon_firmware_version`;
- `meshcore_base_version`;
- contract/profile versions.

A backend may maintain its own local record identifier, but that identifier is not the firmware's public identity vocabulary.

## Profiles

`core` is mandatory. Optional independently versioned profiles include:

- `messaging`
- `remote_admin`
- `health`
- `ota`
- `direct_reader`

Additional profiles may be introduced without redefining a role.

## Core capabilities

Every supported build reports:

- identity/version/role;
- capabilities/profiles;
- configuration schema readable through a supported management transport;
- health/status;
- native RF role state.

Observation capability is advertised explicitly. Supported public Companion and Repeater builds advertise it.

## Role semantics

A Companion exposes native MeshCore Companion messaging and contact/channel behavior. A Repeater exposes native MeshCore repeating behavior. MECON clients must not invent a proprietary RF role called Observer; observation is a capability of a Companion or Repeater.

## Resource declaration

Limits that materially affect clients are machine-readable, including Companion contact capacity. The initial public Companion target declares `contact_capacity: 64`.