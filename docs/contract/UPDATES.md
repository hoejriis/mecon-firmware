# Signed firmware updates (contract)

> One update lifecycle that any conforming manager drives over IP, USB and BLE, with what each delivery path can and cannot protect against. An nRF52 build takes an offer only when it carries the device interface and a security-floor page. A build with no network refuses `source.url` as `unsupported-source`. Image delivery over LoRa is not part of this contract.

It builds on three pages:
- the [device interface](DEVICE_INTERFACE.md) carries every request, result and event;
- the [authority contract](AUTHORITY.md) says who may authorise an install;
- the [managed operations](OPERATIONS.md) give the job, its states, owed results and confirm-or-revert.

## 1. What this protects, and what it does not

- **Protected:** a remote party, or a bridge that relays traffic (a broker, a Reader), cannot install firmware that a release key did not sign, or install a signed release that a Deployment member did not authorise for this device, or go below the device's security floor (section 4).
- **Not protected: physical access.** Neither target family has a bootloader that checks signatures:
  - the Adafruit nRF52 bootloader writes whatever its host sends;
  - the ESP32-S3 ROM bootloader runs with no secure boot and no flash encryption configured in this repository.
  A person with a USB cable can enter the bootloader (a 1200-baud touch, a double reset, the BOOT button) and install any image without an offer. That image inherits the node key, `device_id`, the Deployment record, the local unlock secret and the provisioned marker, and it can report whatever it likes. This is the documented physical-recovery path (AUTHORITY.md; the 0.9 contract's section 13), not a secure-update chain, and this page never calls it one.
- **Consequence for attestation (section 7):** an image's self-report is evidence against *accidental* corruption or a wrong image, never against a malicious one. A manager can conclude "the reported image is not the manifest's" from a mismatch. A match does not prove the image is genuine.

## 2. Two signatures, two questions

| | Signed by | Answers | Checked against |
|---|---|---|---|
| **release manifest** | a **release key**, held by whoever builds the firmware | "is this image a genuine build for this board, layout and role" | the build's **release anchors** (section 3) |
| **install request** (`update.offer`) | a current **Deployment member**; on a device with no record, the cable instead (section 5) | "may this device install it now" | the authority set (AUTHORITY.md) |

**The request binds the release, by hash.**
- `update.offer` carries `manifest_sha256`, the SHA-256 of the manifest text exactly as signed, inside the request body a member signs (AUTHORITY.md section 3.3). `source` and `confirm` are there too.
- The manifest itself arrives separately, staged with `update.manifest` (section 8). The release key's signature authenticates it, and the signed hash pins exactly which manifest the member authorised. A bridge cannot substitute a different manifest, an older one or a downgrade-allowing one.
- The manifest is not carried inside the signed body because it would not fit. As a string inside a string inside the wrapper, its quotes are escaped twice, and a signed offer carrying a 512-byte manifest and a 160-byte URL measured 1,648 bytes, over the device's MQTT buffer.

Neither signature stands in for the other. A URL, a paired link or a broker session is never authority by itself.

## 3. Release anchors

- **The key list.** `docs/contract/release-keys.json` lists every release public key, one entry each: `{line, key_id, public_key, use: "production" | "test"}`. `key_id` is the first 8 bytes of SHA-256 over the public key, as 16 lowercase hex. A Backend uses the file to check a manifest before offering it.
- **Anchors are generated, not copied.** A build's anchors are generated from that file, so the two cannot drift:
  - a firmware environment compiles in `production` keys only, and the build fails if a `test` key would reach it;
  - the simulated boards and the native tests carry the `test` key.
  The file holds three production keys and the test key, which is derived from a public label like the authority vectors. A firmware build carries the three, and refuses a manifest signed by any other key.
- **No private release key is ever in the repository or a build.** Production keys are held by the release owner, at least two with separate custody, so the loss or compromise of one is recoverable (below). Where each is kept, and the drill: [release keys](../RELEASE_KEYS.md).
- **No remote command adds an anchor.** A Deployment may only narrow the accepted keys: `allowed_release_keys`, a list of `key_id`s, set by a DAK-signed authority update. Empty means every compiled-in anchor. A Deployment that suspects a key drops it there, and the device refuses manifests signed by it at once. *Planned:* the field will be specified in AUTHORITY.md's authority set with the implementation, together with its size budget and acceptance checks. Until then every compiled-in anchor is accepted.
- **Compromise and loss.**
  - **A compromised key** can sign a high `version` (version squatting). Once that key is narrowed out, the genuine fix is a downgrade by version number. It installs with `downgrade:true` and a Deployment-signed offer. It cannot inflate the security floor: the floor follows the version compiled into the running image, never the manifest's claim (section 4).
  - **If every production key is lost,** no device can be updated remotely again. Recovery is physical (section 1). A new release with new anchors then goes on by cable.

### 3.1 How a release publishes its update set

A public release on GitHub carries its update set as release assets, so any manager can find the right image for a device without parsing every manifest:
- **One index per release, `update-set.json`:** `{"set": 1, "line", "version", "variants": [...]}`. Each entry is `{"variant", "hardware", "role", "kind", "manifest", "image", "image_sha256", "image_size", "manifest_sha256"}`, where `manifest` and `image` are asset names in the same release. A release lists every variant it was built for.
- **Per variant, two assets:**
  - `<variant>-<version>.manifest.json`: exactly the argument object of `update.manifest` (section 8), `{"manifest": "<the signed text>", "manifest_sig": "<base64>"}`, so a manager passes it through unchanged.
  - the image: `<variant>-<version>.bin` for an ESP32 app image, or `<variant>-<version>.zip` for an nRF52 serial-DFU package, as the manifest's `kind` says.
- **The index is not trusted.** It only finds the assets. A manager checks `manifest_sig` against `release-keys.json`, then the image's SHA-256 against the manifest's, before offering anything. A wrong or stale index can make a manager offer nothing, never something unsigned.
- **The device never sees a GitHub URL.** A manager serves the image from its own `source.url` (at most 256 bytes, section 6).
- A version tag builds, signs and attaches the set (`.github/workflows/signed-release.yml`). v0.2.0 is the first release that carries one.
- **What a set covers:** the builds listed in `ci/release-variants.json`, every environment of the four hardware-verified boards that carries the managed-device interface. From v0.3.0 that is 14. v0.2.0 and v0.2.1 also signed the Heltec V3 and V4 `_usb`, `_ble` and `_wifi` Companions, upstream builds that cannot take a signed update; a board on one of them moves to the standalone build by cable.

## 4. The release manifest, and preflight

**The manifest** is one JSON object, signed as text: Ed25519 over `MECON-RELEASE-v1` 0x00 `<text>`, under AUTHORITY.md section 2's rules (the bytes verified are the bytes sent; strict Ed25519, small-order keys refused). It is at most 640 bytes. At version 1 a manifest with a duplicate key, or a key not listed here (in the manifest or in `image`), is refused (`bad-manifest`). Every key is required; `bootloader_min` may be `null`. Strings are at most: `line` 16, `version` and `min_from` 24, `upstream` 15, `interface` 7, `hardware` 24, each `hardware_rev` entry 8 (one or two entries), `role` 12, `variant` 48, `build` 40. Hex (`key_id`, `image.sha256`) is lowercase.

| Field | Meaning |
|---|---|
| `v` | 1 |
| `key_id` | the release key that signed it |
| `line` | `public` for this repository. A device installs only its own line, because versions do not order across lines ([VERSIONING_AND_RELEASES](../VERSIONING_AND_RELEASES.md)) |
| `version`, `build` | the firmware version (as `hello`'s `firmware`), and the commit it was built from |
| `upstream`, `interface` | the MeshCore base, and the device-interface version. Both informational; the device does not refuse on them |
| `hardware`, `hardware_rev`, `role`, `variant` | the device's own `hello` `hardware` (or any other name the same build's board can report: a Heltec V4 reports `Heltec V4 OLED` or `Heltec V4.3 OLED` by the front end it finds, and accepts a manifest naming either, so one manifest serves the build) and `role`, the board revisions the image supports (a list; a device's revision is a build setting, `1` unless its environment says otherwise), and the build environment name |
| `layout` | the partition or flash-layout version the image was built for. An image for another layout could overwrite a file system or the record |
| `bootloader_min` | the lowest bootloader version the image needs, where the target reports one |
| `data_format` | the version of the on-flash data the image reads and writes (section 9) |
| `security_version` | a monotonic integer, raised by a release that fixes a security flaw. Never lower than an earlier release's |
| `image` | `{"size", "sha256"}` of exactly the bytes written |
| `kind` | `app` (the device's own updater) or `bootloader-dfu` (the target's bootloader, section 6) |
| `min_from` | the lowest running version that may install it |
| `downgrade` | `false` unless the release may go below the running version |
| `issued_at` | for the record only; freshness is the authority layer's job |

**The security floor.**
- **Where it lives.** Outside every file system and any region a DFU writes, and **a factory reset keeps it**. It is never lowered.
  - ESP32: NVS (namespace `mecon`, key `secfloor`). The Companion's factory reset erases all of NVS, so it reads the floor first and raises it back after the erase. A power cut between the two leaves the floor at the running image's compiled version from the next boot on (below).
  - The simulated boards: a file beside the data directory, as for the provisioned marker.
  - nRF52: its own flash page (`MECON_FLOOR_PAGE`, 0xEB000, or 0xD2000 on the T1000-E, in every role), where a serial DFU leaves it alone and below the provisioned marker's page, so that an older image reading the marker page is not misled. The page is a log of 16-byte slots {magic, floor, its complement, CRC-32}, and it reads fail-closed like the marker: an erased page is a floor of 0, anything else that is not a clean log cannot be read and no install passes. A raise appends a slot and reads it back; a full page is erased first, and a power cut in that erase leaves a floor of 0 until the next boot raises it to the running image's compiled version, the same exposure as the ESP32's factory reset. A build without a floor page (the T1000-E Companion, whose ExtraFS covers it) refuses every offer `unsupported` after the authority step.
- **What raises it.** Only the security version **compiled into the running image**, never a manifest's claim. A manifest's `security_version` is checked before install (refused below the floor), but it does not move the floor.
  - At every boot the floor rises to the running image's compiled version, once that image is proven: at once for an image not on trial, and for a freshly installed ESP32 image once its start-up checks pass or it is confirmed (section 6, stage 1). That covers a cable install, an nRF52 job left unconfirmed but installed, and a job confirmed by supersede, while an image the bootloader rolls back never raises it, nor does an image left running unconfirmed (section 7, step 6).
  - A compromised release key that signs a manifest with an inflated `security_version` over a genuine image therefore raises the floor only to what that genuine image carries.
- **What it refuses.** Anything below the floor, `downgrade:true` or not, so no signed release takes a device back below a security fix.
- **Release-signing policy.** Every release with the authority layer has a security version of at least 1, and a build without it counts as 0. "No update can remove the authority layer" holds because no release key signs a manifest at 0. That is policy for whoever holds the keys, not something the floor itself enforces.

**Preflight**, in this order; each step refuses with the code shown and writes nothing:
1. **Authority.** The request is authorised (section 5): `unauthenticated`, `refused`.
2. **Rate.** An offer that would erase the update slot again within 60 s of the last one: `busy`, `rate`.
3. **One job.** Another state-changing job is open: `busy`, `job-open:<job>` (OPERATIONS.md 3.3). The supersede case is section 7, step 7. While the running image is on trial (installed, its start-up checks not yet passed), no install starts: `busy`, `verifying`. The slot it would write holds the image the bootloader returns to.
4. **The staged manifest.** A manifest is staged whose SHA-256 is the offer's `manifest_sha256`: `manifest-not-staged`. Steps 5 to 7 check those exact bytes, and the job keeps its own copy of them. Its syntax is valid: `bad-manifest`. The offer is within its bound (section 6): `too-large`.
5. **Key.** `key_id` is a compiled-in anchor, and allowed by the record if it narrows them: `untrusted-release-key`.
6. **Signature.** `bad-release-signature`.
7. **Target.** `line`, `hardware`, `hardware_rev`, `role`, `variant`, `layout`, `bootloader_min` and `kind` match the device and a path it supports: `wrong-target`.
8. **Security floor.** `security_version` is not below it: `below-security-floor`.
9. **Starting version.** The running version is at least `min_from`: `below-min-from`.
10. **Downgrade.** `version` is not below the running version, unless `downgrade:true` and the offer is Deployment-signed: `downgrade-refused`.
11. **Slot.** `image.size` fits the target's update slot: `too-large`.
12. **Heap.** Enough free heap for the transfer: `busy`, `low-memory`. A push asks for 20 KB at the check, over any link. On an offer the check runs inside the request, with its buffers taken, which is 12 to 15 KB under what `get mecon.mem` reads idle, so a Heltec V3 beside a verified session reading 35 KB or more is admitted. A download opens its own connection: 64 KB over plain HTTP, and over HTTPS 96 KB, or 80 KB on a Heltec V4, where its measured cost leaves 40 KB beside a verified session. This is internal heap: mbedTLS allocates only there, so PSRAM does not help, and a broker session over TLS counts against it. A device with a session up and too little for a download, any Heltec V3 on a verified broker among them, keeps that session, and that memory will not come back while it is up. So a manager that gets `low-memory` on a download offer pushes the same image over the session (section 6) rather than retrying.

Passing them makes the request a job, `accepted` (OPERATIONS.md 3.1).

## 5. Who may offer an update

`update.offer` and the update's `job.confirm` and `job.cancel` use their own class in the CLI privilege table, **`update`**. `update.chunk` is the exception: the job's transfer token authorises it in place of the class check (section 6), so an enrolled Reader can push over USB or BLE once a member's signed offer has opened the job. The class's cells are:

| Caller | `update` |
|---|---|
| signed request (any transport) | allowed |
| cable, on a device with **no** Deployment record | allowed |
| cable, on a device that holds a record | refused unsigned (the enrolled rule, AUTHORITY.md section 9) |
| everything else (BLE of any PIN, a broker session, LoRa, the TCP port, the Ethernet console) | refused |

BLE is refused even with the device's own PIN, for the reason AUTHORITY.md section 6.1 gives for first provisioning: a device cannot tell its own random PIN from one a user or a build chose.

The class's column is in docs/CLI.md's privilege table, and AUTHORITY.md section 9 states it for an enrolled device.

## 6. Delivery and what each target verifies

**The offer.** `update.offer {"manifest_sha256", "source", "confirm": {"within_s"}}`: the staged manifest by its hash, the source (`{"push": true}`, or `{"url": "https://..."}` for IP), and the confirmation window of section 7 (30 to 3,600 s; 900 when `confirm` is left out). A push offer that passes preflight answers `{"job", "state": "accepted", "transfer_token", "next": 0}`.

**Bounds:**
- an `update.offer` or an `update.manifest` is at most **1,536 bytes over MQTT** and at most 4,096 over USB and BLE. The device's MQTT client buffer is 1,600 bytes for the whole PUBLISH: with at most 7 bytes of header and the 42-byte topic `mecon/<device_id>/req`, a request of up to 1,551 bytes fits. A signed stage of the longest manifest is about 1,480 bytes, and a Backend that stages through a broker which is not TLS-verified has to sign it (an unverified broker session may not read);
- `source.url` is at most 256 bytes;
- a manifest is at most 640 bytes. Every field filled in at its longest measures 637 bytes (the `longest` vector).

Measured worst cases:
- an unsigned `update.manifest` with a 640-byte manifest is 882 bytes (`test/fixtures/updates_public_v1.json`);
- a signed offer with a 256-byte URL: published with `update.offer`.

| Target | `kind` | IP | USB | BLE | Image check before activation | Rollback |
|---|---|---|---|---|---|---|
| Heltec V3, V4 (ESP32-S3) | `app` | yes | yes | yes | SHA-256 of the written slot, read back from flash, against the manifest | two stages, below the table |
| T1000-E, Wio Tracker L1 (nRF52840) | `bootloader-dfu` | no IP on these boards | yes, through the Adafruit bootloader | **not supported** | none on the device: the bootloader receives the bytes itself | none |

- **ESP32 rollback, in two stages.**
  1. **Until the new image's start-up checks pass** (radio, mesh, the interface, within 60 s of boot), it is left pending-verify. The firmware takes them as passed once its loop has run for 20 s after boot, with the radio, the mesh and the interface set up before it; a `job.confirm` also ends the window, so a reset cannot take a confirmed image back. With the bootloader's app rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`), any reset in that window returns to the previous slot. That catches an image that crashes or hangs before it can run.
     On a Heltec V3, a host that sets DTR and RTS low when it opens the USB port causes such a reset, through the USB bridge: on Linux, before or after the open, 3 of 3 each. An open that leaves both lines alone did not reset it, 3 of 3, nor did the close after it, 3 of 3. A browser's Web Serial was not measured. So a host that pushed over USB either keeps the port open or leaves the lines alone when it reopens it, or waits until the window has passed.
  2. **Once the checks pass,** the image marks itself valid, so the bootloader stops reverting it, and the confirmation deadline (section 7) takes over. The previous slot is kept untouched until the job is `confirmed`. An app-level revert, on the deadline or an abnormal reset, boots it again.
  - **What stage 2 gives up:** if the image later fails so badly that it cannot run its own revert (a crash loop that begins after the checks), the bootloader no longer rolls it back. Recovery is then physical.
  - **Prerequisite:** the bootloader enables rollback. The pinned Arduino-ESP32 core (2.0.17) builds the ESP32-S3 bootloader with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, and the firmware overrides the core's `verifyRollbackLater()` so that the core does not mark an image valid at boot. A board whose bootloader was flashed without rollback has no stage 1, and an image that fails before its checks needs physical recovery there.
- **IP.** The offer's `source` is `{"url": "https://..."}` or `http://`: at most 256 bytes, a host name or an IPv4 address, an optional port, a path. No user info, fragment or IPv6 literal (`invalid`, `bad-args`).
  - The connection, the request and the response head run off the main loop; the device then takes the body one piece per loop pass into the slot, at most `image.size` bytes, and stops there. The digest decides.
  - The response must be `200` (else `download-status`, with `http_status`), with a `Content-Length` equal to `image.size` where it sends one (else `download-size`), and not chunked (`download-encoding`). A body that ends before `image.size` is `download-short`; a name that does not resolve or a connection that fails, `download-dns` or `download-connect`.
  - A download that has not finished 30 minutes after `accepted` is `failed` (`download-timeout`), and the slot is erased. That includes waiting for the network link.
  - The URL is where to fetch, never permission. `https` is encrypted with the host name as SNI, and the server is not authenticated: integrity is the digest's, the image is public firmware, and checking the server against a CA would only make a download fail where the digest would have passed. A server that serves other bytes, or an attacker in between, can at most make the download fail, which any relay could already do.
  - A download job has no transfer token and takes no `update.chunk`.
- **USB, BLE and a broker session on ESP32.** The offer's `source` is `{"push": true}`. Its result carries a **`transfer_token`** (16 random bytes, hex) for that job.
  - **Chunks.** The bytes come as `update.chunk {"job", "token", "offset", "data"}`. `data` is canonical base64 (padded; leftover bits zero) of at most **2,304 bytes** (3,072 characters), so a chunk in a signed wrapper still fits the 4,096-byte request bound. A chunk is never cached: a retry asks again, and gets the offset expected next. **A chunk also has to fit its transport's request bound**, and 2,304 bytes fits only the 4,096-byte message of the Companion framing over USB and BLE. **Over a cable on a board whose `hello` reports `limits.rx_bytes`** (a Heltec V3: 1,869; a Heltec V4: 4,429), the whole framed request, each 168-byte frame plus its 3-byte header, must fit that figure, or part of it can be lost while the device is busy. On a V3 that is about 1,200 bytes of image per chunk under a signed wrapper (DEVICE_INTERFACE section 8). A console line on the Repeater and Room server holds 1,600 bytes (DEVICE_INTERFACE section 7), and a request through a broker 1,551, so there a sender keeps `data` to about 1,000 bytes, less under a signed wrapper. A request over the bound is never read: the console answers `busy`, `line-too-long`, with the id `""`, and nothing runs. *(Added 2026-11-11: a 2,304-byte push over a V4 Repeater's console moved 0 bytes and the job ended `stalled`; 1,024-byte chunks sent the whole image in 85 s.)*
  - **Over a broker,** a chunk is at most **1,024 bytes** (1,368 characters): with any job id, any offset and a request id of up to 31 characters, that is at most 1,549 bytes of request, inside the 1,551 the client buffer leaves. A bigger one never reaches the device, so it gets no answer. No second connection opens, and the session keeps answering other requests. A session that is not TLS-verified may not read, so stage and offer are signed there; the token then crosses an unchecked broker, which can only spoil chunks, and the digest catches that.
  - **Sending.** One chunk at a time, with no other request in flight beside it ([DEVICE_INTERFACE](DEVICE_INTERFACE.md)), each at the `next` of the answer before. With no answer in about 8 s, send the same offset again: a resend writes nothing twice. `busy` means nothing was written. After the last chunk, the transfer is over and the token dead, so a resend gets `bad-token`, or no answer once the device has gone down to activate: either tells a manager whose answer was lost that the last chunk landed. A restarted manager sends any chunk with the token and learns the offset from `next`.
  - **Chunks are not jobs.** They need no `job_id` and no signature, and never answer `busy job-open`. A chunk without the job's token is refused on every link.
  - **The token is a credential for the job's write path.**
    - It is returned only in the offer's own result (and its owed copy). It never appears in `job` events, `job.get`, `job.list`, `update.status`, the status snapshot or any log.
    - It dies when the job reaches a terminal state, and is compared in constant time.
    - A chunk with a wrong token is a refusal, `bad-token`, under the refusal budget of AUTHORITY.md section 3.4; so is a chunk for a job with no transfer open, so a caller without the token learns nothing from the answer. A relayer that reads it can at most withhold or spoil chunks, which the digest catches. That is a denial of service it could cause anyway.
  - **Bridged transfers.** The offerer can pass the token to the bridge that pushes the bytes. For example, a Backend offers over MQTT and a Reader pushes over USB.
  - **Resuming.** Each chunk answers `{"next": <offset>}`, and a chunk at any other offset gets the expected offset back. A transfer survives a disconnect, a reconnect or a re-enumeration.
  - **Integrity** is the manifest's digest over the whole image.
  - **Stall deadline.** No chunk for 10 minutes makes the job `failed` (`stalled`), the slot is erased, and the one-job lock is released.
- **nRF52 over USB.** `update.offer` takes `source.push` only (`source.url` is `unsupported-source`) and runs the whole of preflight. It opens the job already `awaiting-confirm` (it spans the bootloader step), answers `{"job", "state", "dfu":"serial"}` with no transfer token, and 1.5 s after that answer resets into the bootloader's serial DFU. A `job.cancel` before that reset stops it, and the device stays in its image. The Reader then sends the package with the bootloader's serial DFU. A second offer while that job is open is `busy`, `job-open:<job>`: no supersede here. The next boot attests by version: when the running image's version is the manifest's, it reports `update` with `sha256:null` (the device cannot hash a package the bootloader installed); otherwise the DFU did not happen, and the job ends `failed`, `error:"not-installed"`. This is the 0.9 contract's "offer, bootloader update, attest on boot, acknowledge". Against a cable holder it is **advisory** (section 1): the same cable could skip the offer. It exists so an honest manager and Reader install the right image on the right device, with the result reported through the job.
- **BLE on nRF52 is not supported.** The Adafruit bootloader verifies no signature, and interface builds never register its BLE DFU. A signed BLE path there needs a bootloader that checks signatures. That is a separate decision.

## 7. After the image is written

1. **Verified.** On ESP32 the firmware reads the slot back, compares its SHA-256 with the manifest, and only then marks it bootable: `progress`, step `verified`. A mismatch is `failed` (`digest-mismatch`), and nothing is activated.
2. **Activation.** The device reboots into the new image. The job spans the reboot in the owed-results table (OPERATIONS.md 3.2), already `awaiting-confirm`, so that the boot rule treats it as an installed image and not as a cut transfer. A reboot before activation ends a transfer as `failed`, `error:"interrupted"`.
3. **Attestation.** The firmware that boots reports `update {"job", "version", "sha256"}` in `hello`, in the status snapshot and in a `job` event. `sha256` is the hash of its own running application image, computed at boot; on the nRF52 it is `null`, and the version is the attestation (section 6). Read it as section 1 says.
4. **Awaiting confirmation.** The job is `awaiting-confirm` (OPERATIONS.md 3.4). `job.confirm`, from a caller with the `update` class, arriving after the new image runs, makes it `confirmed`. Reachability alone never confirms, and nor does a running image that cannot show it is the one installed: its slot must be the one the install wrote, and it must hash to the manifest's digest. The confirmation is written down before the job table is (so a power cut between the two still reads as `confirmed`, `confirmed_by:"record"`), and it ends the bootloader's rollback window. The floor follows the running image's compiled version, as section 4 says, whether or not the job is confirmed.
5. **The deadline.** `confirm.within_s` is 30 to 3,600, default 900. This is wider than OPERATIONS.md 3.4's 900 s ceiling, because an update's confirmation follows a download, a reboot and a health check. It counts the image's **running time across boots**, kept in the job's entry.
   - Revert when the deadline passes, or **at once on an abnormal reset**: a panic, the watchdog or a brownout, as `runtime`'s `reset_reason` reports.
   - A normal reboot or a power cycle does not revert. Otherwise anyone allowed `reboot` (a LoRa admin, a broker session) could veto a security update by rebooting.
   - Blocking every confirming path still ends in a revert at the deadline. That is the price of reverting a broken image, and the manager offers again.
6. **Revert.** On ESP32 the previous slot boots again: `reverted`, `reverted:true`, its `running` naming the previous image, which the reboot that follows the result runs. When the bootloader already returned to the previous slot (stage 1), the boot that finds the old image running ends the job `reverted` with `error:"rolled-back"`. A job cancelled after activation but before its reboot points the next boot back at the running image: `reverted`. Whatever leaves the new image unable to show it is the installed one ends the same way, back on the previous image, `error:"unproven"`: the new slot running an image that does not hash to the digest, the install's record lost, or its job gone from the job table while never confirmed. Only where the device cannot tell which slot holds the previous image (no record, and an image not on trial) is nothing changed: `failed`, `error:"revert-unknown"`, `reverted:false`, with `running` naming what runs. The previous image boots on trial too (the bootloader starts every boot entry it is given that way), so a reset in its first 20 s returns to the new one; the device keeps what it needs until the previous image is proven, and sends itself back. It does so at most three times: if the previous image fails its trial each time, the unconfirmed image stays, `hello` and the status snapshot report it as `update` with `unconfirmed:true`, and the floor does not rise to it. The floor rose once the new image passed its start-up checks, so the previous image may now run below it. That is expected and safe: the floor only refuses installs, never a running image. **This is a platform difference.** On nRF52 there is no slot to return to: the Adafruit bootloader keeps one application image, and a serial DFU overwrites it. A deadline that passes there ends the job `failed`, `error:"unconfirmed"`, `reverted:false`, with `running` naming the new image, which stays installed and keeps running; the device does not reboot. A manager records that job as "installed, never confirmed", not as reverted: the device runs the offered version from then on, `hello` carries no `update` for it, and the floor already rose to its compiled security version when it first booted, since an nRF52 image is never on trial (section 4). To leave it, the manager offers another release (one below the running version needs `downgrade:true`).
7. **Supersede.** A new offer that has **passed all of preflight** supersedes an open update job:
   - one whose image attested but was not confirmed becomes `confirmed`, with `confirmed_by:"superseded"`;
   - one that never activated becomes `cancelled`, `reason:"superseded"`, and its slot is erased.
   An offer that fails preflight changes nothing.
8. **What is running.** Every terminal update result carries `running: {"version", "sha256"}`, the image actually running when the result was written. A result never says only "failed" while a new version runs. The manager still reads attestation (step 3) for evidence.

## 8. Other operations

| Operation | Does |
|---|---|
| `update.manifest` | stages one manifest `{"manifest", "manifest_sig"}` in RAM, unsigned, for any caller with `read`. It changes nothing persistent. It verifies the release signature and the target at once, refusing as preflight steps 5 to 7 would. It answers the manifest's `sha256` and what preflight would say. Two slots, keyed by the manifest's hash and not bound to the link that staged it: a Reader may stage over USB and a Backend offer over MQTT. A third stage evicts the older slot. At most one stage a second (`busy`, `rate`), refused stages included, because each costs a signature check on unsigned input. An offer copies its manifest into the job, so staging never touches an open job, and a superseding offer (section 7) can be staged while one is open. An offer naming a hash in neither slot gets `manifest-not-staged`, so a stage from someone else costs at most a retry, never a wrong install. The slots are RAM, so a reboot empties them |
| `update.offer` | opens the update job after preflight (sections 4 to 6), for a push (`{"push": true}`, answering a transfer token) or a download (`{"url"}`); of the `update` class, and its result is kept, so a retry gets the same job and transfer token back |
| `update.chunk` | one piece of the image, at the offset expected next (section 6); authorised by the job's transfer token |
| `update.status` | the open update job, or `null`: `job` and `state`, and during the transfer `received`, `next`, `size` and the manifest's `version` and image `sha256`, or once its image runs, the running `version` and `sha256`. Then `running`, the `version` and `sha256` of the image running now (`sha256` is `null` where the platform cannot hash it), the security floor, and the staged manifests (`sha256`, `version`) |
| `job.cancel` | before activation: stops the transfer and erases the partial slot. After activation it is the revert rule |
| `job.confirm`, `job.get`, `job.list`, `job.ack` | as in OPERATIONS.md, with the `update` class for update jobs |

## 9. What an update keeps, and migrations

An update replaces the application only. It keeps the node identity and `device_id`, the configuration, contacts and channels, the Deployment record and its marks, the provisioned marker and the security floor, the posture and the local unlock secret.

**No migration before confirmation.** A release whose `data_format` differs from the running one:
- writes the new format alongside the old one;
- keeps the old one authoritative until the job is `confirmed`;
- removes the old one only after that.
A revert therefore always finds data the previous image can read. A forced revert can never corrupt the record or lock the device out. The public and private lines keep separate data formats where they differ ([BACKEND_MIGRATION](../migration/BACKEND_MIGRATION.md)).

## 10. Resource rules

- The transfer never holds the main loop: one chunk or one download piece per loop pass.
- One update job at a time. Every non-terminal state has a deadline: the stall deadline, the download timeout, or the confirmation deadline.
- Measured and published with the first implementation, per target and per path:
  - free heap at the transfer's peak;
  - the largest chunk buffer;
  - the flash written;
  - the effect on `loop_gap_ms.max`.

**Measured, Heltec V3 Companion (`Heltec_v3_companion_radio_standalone`), push over the USB UART at 115,200 baud, 2026-10-30.** Four transfers of a 1,767,376-byte image:
- free heap: 86 KB throughout (largest block 31.7 KB); the lowest it reached during a transfer was 74 KB;
- the largest chunk buffers: the base64 text and its decoded bytes of one chunk, 3,073 and 2,304 bytes, on the heap for the length of one `update.chunk`;
- the flash written: the image's own size, into the inactive slot, each sector erased as the write reaches it;
- throughput: about 5 KB/s, so about six minutes per image; from the last chunk to the reboot into the new image, 5.2 s (the read-back of the whole slot, the image check, and the 1.5 s for the answer to go out);
- `loop_gap_ms.max` was not measured, and neither was BLE delivery.

**Measured, the same board downloading over Wi-Fi, 2026-10-31.** The 1,770,192-byte image from a server on the same LAN:
- plain HTTP: 21.8 s, about 79 KB/s; free heap fell from 74 KB to 50 KB during the download;
- HTTPS: 32.9 s, about 53 KB/s; free heap fell from 74 KB to 11 KB, the largest block to 7 KB, and the lowest point was 3.5 KB. That is why step 12 asks 96 KB for HTTPS.
- There was no broker session open during either.

**Measured, a push over a TLS-verified broker session, 2026-11-05.** Chunks of 1,024 bytes, session up throughout, the image attested and confirmed over the broker in every run; a standalone Companion unless the row says otherwise:

| Board | Runs | Image | Time | Retries | Lowest free | Lowest largest block |
|---|---|---|---|---|---|---|
| Heltec V3 | 3 | 1.74 MB | 184 to 198 s | 0 of 5,091 chunks | 11,740 to 15,468 | 12,788 to 14,836 |
| Heltec V4 | 1 | 1.72 MB | 107 s | 0 of 1,684 chunks | 84,824 | 31,732 |
| Heltec V3, six streams and radio traffic | 2 | 1.74 MB | 186 and 192 s | 0 of 3,395 chunks | 15,604 and 14,716 | 14,324 and 13,300 |

**Other requests during a push, measured 2026-11-06.** Heltec V3, enrolled, all ten streams, the same image and chunks, one run each, 0 retries and confirmed in all of them. The stream renewals came every 60 s, heavier than a manager needs:

| Requests beside the chunks | Lowest free |
|---|---|
| Stream renewals, each waiting for its answer | 15,708 |
| A signed two-page `hello` every 60 s | 13,624 |
| Stream renewals, all sent at once | 3,984 |
| Those and the `hello` | 3,272 |
| `hello` every 5 s, renewals at once every 12 s | 2,472 |

No run reached exhaustion, so how a V3 behaves at zero is not measured.
| Heltec V3 Repeater, six streams and radio traffic | 1 | 1.51 MB | 158 s | 0 of 1,474 chunks | 129,264 | 69,620 |
| Heltec V3 Room server, six streams and radio traffic | 1 | 1.50 MB | 161 s | 0 of 1,469 chunks | 125,576 | 65,524 |

- On a V3 a push costs about 23 KB. In one V3 run the broker ended the session mid-transfer: a new verified session came up in 10 s, at about 32 KB free, and the transfer went on.
- The broker carried about 3.1 MB in, and about the same out, for the V3's image. A second client's ping through a bench broker showed no change, with a 115 ms floor of its own, from one device.
- The streams were health, radio, runtime, observations, connectivity and logs, over the broker. The radio traffic was light: one short channel message from another board every 18 s, on a private setting.
- A V3 on a verified session is idle at 34.7 to 39 KB free, which is the range the 20 KB check admits. One under it is refused; a reboot gives it a fresh session's 39 KB.

**Measured, a download beside a verified session, 2026-11-05.** A V4 over HTTPS, 1.72 MB: about 16 s, lowest free 39,728, session up throughout. A V3 over plain HTTP: finished, but with 2 KB free at its lowest, which is why HTTP asks for 64 KB. A V3 with the session closed: plain HTTP lowest 33,300, HTTPS failed 2 of 3 times and reached 1 KB; so a V3 pushes.

## 11. Conformance

- **Public vectors**, generated by a tool in this repository from public labels:
  - a valid manifest;
  - one manifest per preflight refusal of section 4;
  - a signed offer and an unsigned manifest stage at their measured worst cases (section 6);
  - a signed chunk at the 4,096-byte bound.
- **Sim tests** over every transport:
  - offer, chunks with and without the token, interruption and resume, the stall deadline;
  - verify, activation, attestation, confirm;
  - revert on the deadline and on an abnormal reset, none on a normal reboot;
  - supersede after preflight only;
  - the security floor across an update.
- **Real-board tests.** On the Heltec V3 and V4, IP, USB and BLE, after checking the shipped bootloader's rollback support. On the T1000-E and Wio, the USB bootloader path, with attestation catching a wrong image.
