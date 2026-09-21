# Versioning and releases

MECON 1.0 has a hard source dependency on **MeshCore 1.18 after it is merged/released on upstream `main`**. Development against the moving `dev` branch does not satisfy the MECON 1.0 release gate.

Four identities are reported independently:

- `mecon_firmware_version`;
- `meshcore_base_version` — exact pinned upstream 1.18-derived revision;
- MECON contract/profile versions;
- hardware target, role and build variant.

Clients gate protocol behaviour on contract/profile versions and capabilities, not firmware release numbers.

## Release artifacts

Each supported board/role publishes flash/OTA artifacts, machine-readable manifest, digest/signature metadata, hardware/role/variant identifiers, exact MeshCore base, MECON contract versions and measured resource limits such as contact capacity.

## Release gate

Before MECON 1.0: MeshCore 1.18 must be on `main`; the pinned stock baseline must be proven on V3/V4; the IDF5/NimBLE migration must preserve stock-derived behaviour; and each advertised board/role must pass the hardware gate with realistic persisted state.

## OTA

Managed OTA validates signed metadata and hardware/role/variant compatibility. USB remains the recovery path.

## Development builds

Builds based on upstream `dev`, dirty trees or unpinned revisions identify themselves as development builds and must not be mistaken for MECON 1.0 release artifacts.