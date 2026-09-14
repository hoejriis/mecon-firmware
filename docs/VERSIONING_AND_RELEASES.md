# Versioning and releases

Four identities are reported independently:

- `mecon_firmware_version` — this project's release;
- `meshcore_base_version` — exact upstream MeshCore base;
- MECON contract/profile versions — wire compatibility;
- hardware target, role and build variant — artifact compatibility.

Clients gate protocol behavior on contract/profile versions and capabilities, not on firmware release numbers.

## Release artifacts

Each supported board/role release publishes:

- merged image for USB flashing;
- app image suitable for managed OTA where supported;
- machine-readable manifest;
- SHA-256 digest;
- signed OTA metadata;
- hardware/role/variant identifiers;
- MeshCore base version;
- MECON contract/profile versions;
- important resource limits such as Companion contact capacity.

## Release gate

A release is not generally supported merely because CI built it. Every advertised supported board/role must pass the hardware gate in `SUPPORTED_HARDWARE.md` against realistic persisted state.

Partial releases must be explicit. A release must never publish an artifact under a supported target's name if that artifact was built for another board revision or role.

## OTA

Managed OTA uses approved signed release metadata, validates hardware/role/variant compatibility and uses the platform's safe update slots. Failed update/reboot must have a documented recovery or rollback path. USB flashing remains the ultimate recovery path.

## Development builds

Development/dirty/unknown builds must identify themselves as such and must not be mistaken for signed release artifacts.