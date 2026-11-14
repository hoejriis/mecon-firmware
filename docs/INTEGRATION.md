# Integrating a manager

A manager is any application that manages mecon-firmware devices through the public contracts: a Backend, a desktop tool, a script. None is required, and MeshContinuum is one implementation among any others. The normative pages are listed in the [contract index](contract/README.md).

## Start with capabilities

Send `hello` first. It reports the interface version, `device_id` and the node key with a signature binding them, the role, the hardware and the firmware, and lists every operation (by its bare name, `channel.get`) and stream (`stream.<name>`) this device can run. Do not infer behaviour from the board name, role or firmware version. An operation that is not listed answers `unknown-op`, and a listed one can still be `refused` for the caller ([device interface](contract/DEVICE_INTERFACE.md)).

## Paths to a device

| Path | How | Notes |
|---|---|---|
| USB | `0x7F` app frames on a Companion; `mecon> ` lines on the Repeater and Room server console | The trusted local path, and the one used to enrol a device |
| BLE | the same `0x7F` frames, on a Companion | Trusted only when paired with a PIN other than the default |
| MQTT | `mecon/<device_id>/req`, `resp`, `ev/<stream>` and the retained `status` | Every role on the Heltec boards. Only a TLS-verified broker session counts |
| LoRa | point to point, or on the device's management channel | Authenticated operations and compact telemetry ([management over LoRa](contract/LORA_MANAGEMENT.md)) |

A bridge, such as a browser app relaying USB or BLE to a Backend, forwards envelopes verbatim. It adds no authority, and the device's signatures survive it.

## Enrolment and authority

A device with no Deployment follows its posture rules, under which a trusted link may change settings. A device joins one Deployment over the USB cable. From then on, a state-changing request needs a manager's signature, whatever path it takes. A TLS broker login is not a signature, and the unsigned cable keeps only reads, reboot and the recovery operations ([caller kinds](contract/AUTHORITY.md#9-caller-kinds)). The Deployment's authority key decides who the managers are. The device persists a replay watermark per manager and signs its results and events ([management authority](contract/AUTHORITY.md)).

## What the firmware does, and what a manager does

| Layer | What it is | Examples |
|---|---|---|
| Generic capability | The firmware does what an authorized manager asks, and reports honestly what happened | Native direct and channel send, with correlated outcomes. Collecting received messages. Contact and channel administration. Supplying a message to the local app without transmitting it. Bounded standing instructions and periodic reports. Compact telemetry |
| Autonomous firmware process | The device acts by itself under stated criteria. Later work, and a separate thing from the capabilities above | Outage forwarding |
| Manager orchestration | Policy, which stays in the application | Which gateway to send through, which history to catch up on, how a Deployment is run, RF health policy, carrying favourite channels between devices |

The firmware holds no user favourites database and no routing policy. RF health monitoring is built from compact telemetry and standing instructions, not from a telemetry mechanism of its own. Firmware-image delivery over LoRa is later work too.

## Messages and contacts

On a Companion, `message.send` sends one native message, and `message.get` reads what became of it. `inbox.list` collects received messages, beside the native queue the stock app reads. `message.deliver` hands the stock app a message the device missed, marked, and never transmits it. `contact.*` and `channel.*` work on the native tables.

Each of these is bounded and lives in RAM where the [contract](contract/OPERATIONS.md#6-messages) says so, so plan for a reboot losing what was not collected. An outcome reports what the device knows: `acked` shows the recipient's node had the message, and `timed-out` does not prove it did not. Retries and catching up are the manager's to decide.

## Configuration

Settings are the device's CLI settings, read and written with `config.schema`, `config.get` and `config.apply`. A change that could cut the manager off (radio, transmit power, connectivity, Wi-Fi or broker settings) must ask for confirmation, and reverts unless `job.confirm` follows ([managed operations](contract/OPERATIONS.md)). Wi-Fi profiles, broker profiles and the broker roles a Deployment assigns (local, primary, secondary) are supplied this way or at provisioning. A broker's TLS anchor, a fingerprint or a CA, is set or cleared with `mqtt.profile.anchor`, and a profile is removed with `net.profile.clear`. Broker credentials are write-only.

## Telemetry and updates

Subscribe to streams with `sub.start` and renew them before their lifetime runs out ([telemetry](contract/TELEMETRY.md)). Install firmware with a release manifest signed by a release key and an offer signed by a manager ([signed updates](contract/UPDATES.md)).

## Compatibility

Gate on the interface version and the capabilities in `hello`, never on firmware version numbers. The `upstream` field identifies the MeshCore base. An unknown capability never implies authority. Extension rules belong to each object: in a request envelope or a result, unknown fields are ignored and an unknown operation is refused. Signed objects are verified over their exact bytes, so an added field is either signed or rejected, and a version 1 release manifest refuses any key it does not list. Follow the page that owns the object ([contract principles](contract/README.md#principles)). A Backend moving from the private reference firmware has a [migration guide](migration/BACKEND_MIGRATION.md).
