# Security policy

Please do not publish exploitable security vulnerabilities as ordinary public issues before maintainers have had a reasonable opportunity to assess them.

Security-sensitive areas include:

- MeshCore private identity handling;
- Wi-Fi and MQTT credentials;
- broker authorization and ACLs;
- remote configuration and replay protection;
- BLE pairing/authentication;
- OTA signing and verification;
- USB/BLE management tunnels;
- any path that could permit unauthorized RF transmission or arbitrary code execution.

When reporting a vulnerability, include affected firmware version/build, hardware target and role, reproduction conditions, expected impact and whether physical access is required.

The project's intended security boundaries are documented in [docs/SECURITY_MODEL.md](docs/SECURITY_MODEL.md) and [docs/contract/SECURITY_AND_AUTHORITY.md](docs/contract/SECURITY_AND_AUTHORITY.md).