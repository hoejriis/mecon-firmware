# Integrating a manager

A manager is any application that manages mecon-firmware devices through the public contracts: a Backend, a desktop tool, a script. None is required, and MeshContinuum is one implementation among any others. The normative pages are listed in the [contract index](contract/README.md).

## Start with capabilities

Send `hello` first. It reports the interface version, `device_id` and the node key with a signature binding them, the role, the hardware and the firmware, and lists every operation (`op.<name>`) and stream (`stream.<name>`) this device can run. Do not infer behaviour from the board name, role or firmware version. An operation that is not listed answers `unknown-op`, and a listed one can still be `refused` for the caller ([device interface](contract/DEVICE_INTERFACE.md)).

## Paths to a device

| Path | How | Notes |
|---|---|---|
| USB | `0x7F` app frames on a Companion; `mecon> ` lines on the Repeater and Room server console | The trusted local path, and the one used to enrol a device |
| BLE | the same `0x7F` frames, on a Companion | Trusted only when paired with a PIN other than the default |
| MQTT | `mecon/<device_id>/req`, `resp`, `ev/<stream>` and the retained `status` | Every role on the Heltec boards. Only a TLS-verified broker session counts |
| LoRa | point to point, or on the device's management channel | Authenticated operations and compact telemetry ([management over LoRa](contract/LORA_MANAGEMENT.md)) |

A bridge, such as a browser app relaying USB or BLE to a Backend, forwards envelopes verbatim. It adds no authority, and the device's signatures survive it.

## Enrolment and authority

A device joins one Deployment over the USB cable. From then on, a state-changing request needs a manager's signature, whatever path it takes. The Deployment's authority key decides who the managers are. The device persists a replay watermark per manager and signs its results and events ([management authority](contract/AUTHORITY.md)).

## Configuration

Settings are the device's CLI settings, read and written with `config.schema`, `config.get` and `config.apply`. A change that could cut the manager off (radio, transmit power, connectivity, Wi-Fi or broker settings) must ask for confirmation, and reverts unless `job.confirm` follows ([managed operations](contract/OPERATIONS.md)). Wi-Fi profiles, broker profiles with their TLS anchors, and the broker roles a Deployment assigns (local, primary, secondary) are all supplied this way or at provisioning. Broker credentials are write-only.

## Telemetry and updates

Subscribe to streams with `sub.start` and renew them before their lifetime runs out ([telemetry](contract/TELEMETRY.md)). Install firmware with a release manifest signed by a release key and an offer signed by a manager ([signed updates](contract/UPDATES.md)).

## Compatibility

Gate on the interface version and the capabilities in `hello`, never on firmware version numbers. The `upstream` field identifies the MeshCore base. Unknown fields are ignored, and an unknown capability never implies authority. A Backend moving from the private reference firmware has a [migration guide](migration/BACKEND_MIGRATION.md).
