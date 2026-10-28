# Signed firmware updates (contract)

> **Status: contract recorded before the code ([#110](https://github.com/hoejriis/mecon-firmware/issues/110)).** Nothing on this page is built yet. It defines one update lifecycle that a Backend, a Reader bridge or any conforming manager drives over IP, USB and BLE, and says honestly, per target, what each delivery path can verify. LoRa delivery of image bytes is [#123](https://github.com/hoejriis/mecon-firmware/issues/123) and is out of scope here. It will reuse this lifecycle.

It builds on three pages:
- the [device interface](DEVICE_INTERFACE.md) carries every request, result and event;
- the [authority contract](AUTHORITY.md) says who may authorise an install;
- the [managed operations](OPERATIONS.md) give the job, its states, owed results and confirm-or-revert.

## 1. Two signatures, two questions

An install needs two independent signatures:

| | Signed by | Answers | Checked against |
|---|---|---|---|
| **release manifest** | a **release key**, held by whoever builds the firmware | "is this image a genuine build, for this board and role, and how big is it" | the build's compiled-in **release anchors** (section 2) |
| **install request** (`update.offer`) | a current **Deployment member**, or a trusted local caller on a device with no record | "may this device install it now" | the Deployment authority set (AUTHORITY.md), or the caller table (section 5) |

Neither stands in for the other. A valid manifest without an authorised request installs nothing, and an authorised request for a manifest the device cannot verify installs nothing. A URL, a paired link or a broker session is never authority by itself.

## 2. Release anchors

- A build carries a list of release public keys, each `{key_id, public key}`, compiled in. `key_id` is the first 8 bytes of SHA-256 over the public key, as 16 lowercase hex.
- **No private release key is ever in the repository or a build.** The repository's CI uses a test key derived from a public label, valid only for test vectors and the simulated boards, as the authority vectors do. A build made without configured anchors has none, and refuses every update, with `no-release-anchor`.
- Anchors change only with a firmware update: the new image brings its own list. There is no remote command to add an anchor. That would let whoever can authorise an install also choose what counts as a genuine build.

## 3. The release manifest

One JSON object, signed as text: Ed25519 over `MECON-RELEASE-v1` 0x00 `<text>`. AUTHORITY.md section 2's encoding rules apply: the bytes verified are the bytes sent. The manifest travels as a string with its signature.

| Field | Meaning |
|---|---|
| `v` | 1 |
| `key_id` | the release key that signed it |
| `line` | `public` (this repository) or another line's name. A device installs only its own line, because versions do not order across lines ([VERSIONING_AND_RELEASES](../VERSIONING_AND_RELEASES.md)) |
| `version` | the firmware version, as the build's `firmware` in `hello` |
| `upstream` | the MeshCore base, as `hello`'s `upstream` |
| `interface` | the device-interface version the image speaks |
| `hardware`, `role`, `variant` | must equal the device's own `hello` `hardware` and `role` and its build environment name. A mismatch is `wrong-target` |
| `image` | `{"size": <bytes>, "sha256": "<64 hex>"}` of exactly the bytes that are written |
| `kind` | `app` (an application image for the device's own updater) or `bootloader-dfu` (an image for the target's bootloader, section 6) |
| `min_from` | the lowest running version that may install it |
| `downgrade` | `false` unless the release allows going below the running version (section 4) |
| `issued_at` | seconds since 1970, for the record only. Freshness is the authority layer's job, not the manifest's |

## 4. Checks before anything is written (preflight)

In this order, each a refusal with the code shown and nothing written:
1. The request is authorised (section 5): `unauthenticated` or `refused`.
2. Another state-changing job is open: `busy`, `job-open:<job>` (OPERATIONS.md 3.3).
3. `key_id` is a compiled-in anchor: `untrusted-release-key`. The signature verifies: `bad-release-signature`.
4. `line`, `hardware`, `role`, `variant` and `kind` match the device and a path it supports: `wrong-target`.
5. The running version is at least `min_from`: `below-min-from`.
6. `version` is not below the running version, unless the manifest says `downgrade:true` **and** the request is Deployment-signed: `downgrade-refused`. This protects against reinstalling an older signed release that has known flaws.
7. `image.size` fits the target's update slot: `too-large`.
8. Enough free heap for the transfer buffers: `busy`, `low-memory`. Nothing is written, and the offer can be retried.

Passing them makes the request a job in state `accepted` (OPERATIONS.md 3.1).

## 5. Who may offer an update

`update.offer` is a state-changing, signed-or-local operation:
- **A device that holds a Deployment record** accepts it only Deployment-signed, with a `job_id` (AUTHORITY.md section 7). An unsigned offer is `unauthenticated` on every transport, the cable included. The cable keeps the target's physical recovery path (section 6), which is outside this lifecycle.
- **A device with no record** accepts an unsigned offer from the cable and from BLE paired with the device's own PIN. Never from a broker session, a LoRa caller or an untrusted link.
- The class in the CLI privilege table is `destructive`: it replaces the firmware.

## 6. Delivery and what each target can verify

| Target | Kind | IP | USB | BLE | Image verified by | Rollback | Physical recovery |
|---|---|---|---|---|---|---|---|
| Heltec V3, V4 (ESP32-S3) | `app` | yes | yes | yes | the running firmware, before activation (SHA-256 against the manifest) | the bootloader's: the new app runs pending-verify, and an unconfirmed or failed boot returns to the previous slot | the ROM serial bootloader, unsigned, physical possession |
| T1000-E, Wio Tracker L1 (nRF52840) | `bootloader-dfu` | no IP on these boards | yes, through the Adafruit bootloader | **not supported** | the running firmware checks the signed manifest and the offer before handing over. The bootloader checks no signature and receives the image itself, so the bytes written are not verified on the device | none in the bootloader. The firmware that boots attests its own image (section 7), and a wrong image is replaced the same way | the same USB bootloader |

- **IP.** The offer's `source` is `{"url": "https://..."}`. The device downloads it, hashing as it writes, and the manifest's digest decides. The URL is where to fetch, never permission. TLS is used where the profile has a CA anchor, for privacy; integrity never depends on it.
- **USB and BLE on ESP32.** The offer's `source` is `{"push": true}`, and the bytes come in through the device interface on the link that sent the offer, or on any authorised link for a signed offer:
  - `update.chunk {"job", "offset", "data"}`, with `data` base64 of up to 3,072 bytes, so a chunk fits a 4,096-byte request;
  - each chunk answers `{"next": <offset>}`. A chunk at any other offset is answered with the offset expected, which makes the transfer resumable after a disconnect, a reconnect or a re-enumeration;
  - chunks are not signed one by one. The manifest's digest over the whole image is the integrity check, and the offer is the authority.
- **nRF52 over USB.** The firmware verifies the manifest and the offer, records the job (it must span the bootloader step), and then enters the bootloader (the 1200-baud touch). The Reader then sends the image to the bootloader with its own serial DFU. This is the 0.9 contract's "offer, bootloader update, attest on boot, acknowledge". **What this does not verify** is that the bytes the bootloader wrote are the bytes the manifest names. The firmware that boots attests them (section 7), so a mismatch is detected after the fact, never prevented.
- **BLE on nRF52 is not supported.** The Adafruit bootloader verifies no signature, so its BLE DFU would accept any image from any paired client. Interface builds never register it ([#143](https://github.com/hoejriis/mecon-firmware/issues/143)). A signed BLE path there needs a bootloader that checks signatures, which is a separate decision, not part of this contract.

## 7. After the image is written

1. **Verified.** On ESP32 the firmware compares the received image's SHA-256 with the manifest, marks the new slot bootable, and reports `progress` with step `verified`. A mismatch is `failed` with `digest-mismatch`, and nothing is activated.
2. **Activation.** The device reboots into the new image. The job spans the reboot in the owed-results table (OPERATIONS.md 3.2).
3. **Attestation.** The firmware that boots reports, in `hello`, in the status snapshot and in a `job` event, `update {"job", "version", "sha256"}`. `version` is its own, and `sha256` is the hash of its own running application image, computed at boot. A manager compares these with the manifest. On nRF52 this is the only evidence that the bootloader wrote the right bytes.
4. **Awaiting confirmation.** The job is `awaiting-confirm` (OPERATIONS.md 3.4). `job.confirm`, from a caller allowed `update.offer` and arriving after the new image runs, makes it `confirmed`. Reachability alone never confirms. The deadline is the offer's `confirm.within_s` (30 to 3,600, default 900), or a second boot while the job is still pending.
5. **Revert.** On ESP32, an unconfirmed, crashing or failed-to-boot image returns to the previous slot: the job is `reverted`, and the result is owed over whichever path works. nRF52 has no slot to return to: an unconfirmed job there is `failed` with `unconfirmed`, and the firmware stays as installed.
6. **Superseded.** A new valid offer supersedes a pending job whose image already attested but whose confirmation was lost. That job is reported `confirmed` first, with `confirmed_by: "superseded"`. A pending image that never attested never blocks a new offer.

## 8. Other operations

| Operation | Does |
|---|---|
| `update.status` | the open update job: state, bytes received, the offset expected next, the manifest's `version` and `sha256` |
| `job.cancel` | before activation: stops the transfer and erases the partial slot. After activation it is the revert rule (OPERATIONS.md 3.3) |
| `job.confirm`, `job.get`, `job.list`, `job.ack` | as OPERATIONS.md |

## 9. What an update keeps

An update replaces the application only. It keeps:
- the node identity and `device_id`;
- the configuration, contacts and channels;
- the Deployment record and its marks, and the provisioned marker;
- the posture and the local unlock secret.

A release that needs a data migration says so in its release notes, migrates at first boot, and a failed migration leaves the previous data readable by the previous version, so a revert still works. The public and private lines keep separate data formats where they differ ([BACKEND_MIGRATION](../BACKEND_MIGRATION.md)).

## 10. Resource rules

- The transfer never holds the main loop: one chunk is written per loop pass, and the IP download runs in bounded pieces.
- One update job at a time, as for every state-changing job.
- Free heap at the transfer's peak, the largest chunk buffer, the flash written and the effect on `loop_gap_ms.max` are measured per target and per path, and published with the first implementation.

## 11. Conformance

- Public test vectors, generated by a tool in this repository from public labels:
  - a valid manifest and its signature;
  - one manifest for each preflight refusal of section 4 (wrong key, bad signature, wrong target, below `min_from`, a downgrade without `downgrade:true`, too large).
- Sim tests over every transport for the lifecycle: offer, chunks, interruption and resume, verify, the activation reboot, attestation, confirm, revert on the deadline and on a second boot.
- Real-board tests on the Heltec V3 and V4 for IP, USB and BLE. On the T1000-E and Wio, the USB bootloader path, with attestation catching a wrong image.
