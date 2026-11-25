# Signed firmware updates over LoRa (contract draft)

> **Status: draft for review, version 0 (2026-11-23).** Built so far, on main and in no release yet: the bulk frame and answer codec (`LoraBulk`), the target's transfer state (`LoraTransfer`), and the target side of the transfer: `update.manifest.part`, an `update.offer` with `source.lora`, the `paused` job state and its resume, the writing of the slot (with durable `next`) and the verify, activate and confirm a push has. Target side built; not yet reachable over the air: the LoRa link and the gateway's `rf.bulk` are the next slice, so until then the frames reach the target only through a simulator test hook, and nothing here is bench-verified. Deflate and the fast preset are not built (an offer naming a preset is `unsupported`). It extends the [signed update](UPDATES.md)
> lifecycle to a LoRa delivery path and [management over LoRa](LORA_MANAGEMENT.md) to carry image bytes. It is written to
> be agreed between this firmware and a public manager (the reference Backend), before either implements it. Section 12
> lists what is still open. Until this draft is accepted, `update.*` stays off LoRa as LORA_MANAGEMENT says.

## 1. What this adds, and what it does not

- **Adds:** one more delivery source for an update job, `{"lora": {...}}`, beside `push` and `url` (UPDATES section 6). The
  same release manifest, the same preflight, the same job, activation, attestation, confirmation and revert (UPDATES
  sections 4 to 7). Only the way the image bytes and the manifest reach the target is new.
- **Does not add:** a new authority, a new signing key, any automatic rollout, any relaying of images by repeaters to
  each other, or any change to who may offer an update (UPDATES section 5: a member's signature). A device never starts
  an update by itself.
- **Decided by the project owner (2026-11-23):** the path is enabled on every board that can hold an image, like other
  capabilities, and claimed as hardware-verified only where the bench has shown it (section 8). A transfer may use one
  fixed fast preset, named in the signed offer, with an automatic revert (section 9).

## 2. Parties

| Party | Is | Does |
|---|---|---|
| **Manager** | a Backend, or the standalone reference manager (section 11) | holds the signed offer and the image, decides when and where, paces the transfer, reads progress, confirms |
| **Gateway** | a MECON Companion the manager reaches over USB, BLE or a broker | relays LoRa frames between the manager and the target, one hop, and nothing else: it holds no image, decides nothing, and is never authority (LORA_MANAGEMENT section 2) |
| **Target** | an enrolled MECON device in Normal posture | verifies the offer and the manifest, takes the image, verifies its digest, activates, attests |

The gateway reuses `rf.request` (OPERATIONS) for signed requests, and adds one relay operation for bulk frames
(section 6). A Repeater is not a gateway in this version.

## 3. The transfer, step by step

1. **Stage the manifest.** The manager sends the release manifest to the target in parts (section 5). The target
   reassembles it, checks the release signature and its own target fields at once, as `update.manifest` does (UPDATES
   section 8), and keeps it in a staging slot by its hash.
2. **Offer.** The manager sends a signed `update.offer` with `source: {"lora": {...}}` (section 4) over the point-to-point
   path. Preflight runs as for any offer. The result carries the job, the transfer token and the transfer's shape.
3. **Transfer.** The manager sends the image in blocks of bulk frames through the gateway (section 6). After each block
   the target answers which frames it holds. The manager re-sends only the missing frames.
4. **Pause and resume.** A transfer pauses at the window's end, when the duty window is full, or when the manager stops.
   A same-job re-offer resumes it (section 7).
5. **Verify, activate, attest, confirm.** Exactly UPDATES section 7. The manager reads the result with a signed `job.get`
   over LoRa, or over any other link the target has.

## 4. The offer

`update.offer {"manifest_sha256", "issued_at": <unix s>, "source": {"lora": {"block": 8, "window_end": <unix s>, "preset": <optional>}},
"confirm": {"within_s"}}`, signed by a member, with a `job_id`, point to point only. `issued_at` is the manager's clock when it
signed the offer; it is inside the signed body, and the window is measured from it. It is required with `source.lora`.

- `block` (**required**): bulk frames per block, 1 to 8; any other value, or no value, is `invalid`, `bad-args`. The target answers once per block (section 6). A `source.lora` with any key beside `block`, `window_end` and `preset` is `invalid`, `bad-args` too, and so is a `source` that names another source as well.
- `window_end` (**required**): the time after which the target takes no more bulk frames for this job, and pauses
  (section 7). It puts the project owner's night window into the signed request, so a manager that stops or loses its
  clock cannot keep a transfer running into the day. Missing, already past, or more than 12 hours after the offer's
  `issued_at` is `invalid`, `bad-args`. The target also bounds it by its own monotonic clock: the window ends at the
  earlier of `window_end` and the offer's receipt plus (`window_end` − `issued_at`). A wrong device clock can therefore
  only shorten a window, never lengthen it.
- `preset`: only if section 9 allows it. The fast preset is a later slice: until it is built any `preset` is `refused`, `unsupported`. It is read after the rest of `source.lora`, so a malformed one is `invalid` first.
- The answer, `{"job", "state": "accepted", "transfer_token", "size", "transfer_size", "encoding": "raw", "next": 0,
  "frame_bytes", "frames_total"}`, travels only point to point, in a result that is itself signed when the target is
  enrolled. `size` is the image as it will run; `transfer_size` the bytes that travel (equal to `size` unless
  `encoding` is `deflate`); `frames_total` is `transfer_size` divided by `frame_bytes`, rounded up. Frames index the
  transferred bytes, never the inflated image.
- **The token is the manager's to keep.** A Backend keeps it with the job, encrypted at rest, and deletes it when the
  job ends. A manager that lost it resumes with a re-offer (section 7), which answers the same token.

An offer over LoRa is refused `unsupported-source` on a board that does not offer this path (one that installs through its
bootloader, the nRF52, or has no second image slot), with `invalid`, `bad-args` for a malformed `source.lora`, and as any
update offer otherwise (UPDATES sections 4 to 6). The checks of this section come before preflight, which then runs as for a
push, rate limit and all. The one-job lock (OPERATIONS section 3.3) holds: while the transfer is open or paused, no other
state-changing job runs. `image.encoding` (a deflate transfer, section 6) is not in the manifest format yet: a manifest that
carries it is refused at staging, `bad-manifest`, so no offer can name one.

## 5. Staging the manifest

A signed stage of the longest manifest is about 1,480 bytes, more than the 1,024 bytes a LoRa message holds
(LORA_MANAGEMENT section 4). The manifest carries its own release signature, so its parts need no Deployment signature:

- `update.manifest.part {"sha256", "part", "parts", "data"}`: `sha256` of the manifest text as UPDATES section 8 keys it,
  at most 4 parts, `part` counted from 1 to `parts`, `data` canonical base64 of at most 176 bytes. The parts, joined in order of
  `part`, are the manifest text followed by its 64-byte signature (raw), so the longest is 640 + 64 = 704 bytes in four parts
  of 176. A joined text whose SHA-256 is not `sha256` is `invalid`, `bad-args`, and the parts are spent. It is a **signed** request like every request over LoRa (LORA_MANAGEMENT
  section 2), with no `job_id` (a read-class staging, as `update.manifest` is).
- The target reassembles in a RAM slot keyed by `sha256`, then runs exactly what `update.manifest` runs. A part for
  another hash, or a part count that changes, discards the slot (the new part starts a new one); a part held already is
  replaced; parts may arrive in any order. While parts are missing it answers `{"sha256", "part", "parts", "held"}`, the
  parts held now; with the last it answers what `update.manifest` answers, `{"sha256", "version", "preflight"}`. A part costs no
  signature check, so only the staging, which the last part runs, counts against the one-stage-a-second limit (`busy`, `rate`);
  the parts stay for the same last part to be sent again. A refused staging (a wrong signature or target) spends the parts.
- `update.manifest` itself (the whole manifest in one message) is also offered over LoRa for a manifest short enough to
  fit.

## 6. Bulk frames

The image travels as raw bytes, never as base64 or JSON, because on LoRa every byte is airtime.

- **The frame.** A bulk frame travels in the same native anonymous datagram as a request frame (LORA_MANAGEMENT
  section 4), point to point only. After the datagram's 4-byte tag, its 132 bytes hold: a marker byte of its own
  (`0x1E`, so neither a request frame nor a native request is ever read as one), the job number (4 bytes, little
  endian), the frame's index in the transfer (4 bytes), the image bytes, and an 8-byte tag. So `frame_bytes` is
  **115**: 132 − 1 − 4 − 4 − 8. Every frame but the last carries exactly 115 bytes, and the last exactly
  `transfer_size` − index × 115. A frame of any other length is dropped and counted, never written.
- **The tag.** HMAC-SHA-256 under a key derived from the transfer token for this direction only
  (`K_bulk = HMAC-SHA-256(token, "mecon lora bulk v1")`), over the marker, the job number, the index and the bytes, cut
  to 8 bytes. The token itself never goes on the air. A frame with a wrong tag is dropped and counted, never answered, so a stranger's frames
  cost the target nothing it can be made to multiply (LORA_MANAGEMENT section 7). Integrity of the whole is still the
  manifest's digest; the tag only keeps strangers' frames out of the slot.
- **Blocks.** The manager sends up to `block` frames, then waits. The target answers in one reply frame, binary as
  the bulk frames are: marker `0x1F`, the job (4 bytes), `base` (4), `next` (4), `have` (1 byte, a bitmask) and an
  8-byte tag over the marker and those fields under a key of its own
  (`K_answer = HMAC-SHA-256(token, "mecon lora answer v1")`): 22 bytes, where a signed JSON result would take two or three reply frames. `base` is
  the frame index the block starts at, and bit i of `have` is frame `base` + i, so an answer that arrives late is
  never read against a later block. The answer needs no Deployment signature: a forged one can only make the manager
  send frames again or skip ahead, and the manifest's digest still decides what is activated. Missing frames are sent
  again; held frames are never written twice. A resend of the block just answered gets that block's answer again (its `base`), so a lost answer costs one block of airtime, not the transfer. A block's answer is the only traffic the target originates during a
  transfer. The progress a manager reports to people comes from a signed `job.get` (section 7).
- **Neither direction can stand in for the other.** The two kinds have different markers, the marker is inside each
  tag, and each kind has its own key. So a captured block answer sent back at the target is not a bulk frame, and a
  short last frame is not an answer. (A shared key and marker would have let anyone who heard one answer write its 13
  bytes into the image at `base`, spoiling the transfer for its digest to catch only at the end.)
- **The gateway.** `rf.bulk {"to", "frames": [<base64 of one frame>, ...]}` on the Companion: queues the frames for the
  target, sends them through the duty window, and returns at once with `{"queued", "bulk_budget_ms"}`: the frames now
  waiting, and the airtime bulk may still spend in the current hour before the reserve (section 9). `rf.status` reads
  the same two figures without queuing anything, so a manager paces itself instead of filling the queue. The gateway
  never reads or keeps the frames. On the USB link to the manager they are base64; on the air they are not.
- **Writing.** On ESP32 the target writes into the inactive slot as frames arrive in order, and keeps out-of-order frames
  of the current block in RAM until the gap is filled (at most `block` × `frame_bytes`). On nRF52 see section 8.
- **Deflate.** A manifest may name a deflate-compressed transfer (`image.encoding: "deflate"`, with `transfer_size`). The
  target inflates as it writes, and the digest is over the inflated image, which is what runs. Section 12 holds the
  question of page-level deltas.

## 7. Pause, resume, deadlines

- **Pauses are a state, not a failure.** `paused` is a non-final job state with a `reason`: `window` (the window
  ended), `duty` (the target's duty window had no room for a block answer for 10 minutes), `manager` (no bulk frame
  for 30 minutes). `job.get` on an open or paused LoRa transfer reports `next`, `frames_total`, `window_end` and, when
  paused, `reason`.
- **What a pause keeps.** On ESP32 the slot as written so far and `next`, in the job's entry, so a pause survives a
  reboot of the target. This is a change from the private 0.9x line, which kept a partial download in RAM only. The job's entry
  also keeps the token and the manifest's hash (in the private bytes of the job table's entry, never in a read). A transfer that
  is open when the target reboots comes back `paused`, reason `manager`, with the `next` last kept, which is saved every 64
  frames and at a pause; frames after it are sent again, and writing them again rewrites the same bytes. The slot is reopened
  where the kept `next` ends without erasing what is there: a sector is erased only when a write first reaches its start.
- **Resume.** A new signed `update.offer` naming the same manifest, with a later `window_end`, while the job is paused,
  resumes it: same job, same token, `next` as kept; the answer is the offer's answer, with `next` as it stands. The offer may
  repeat the job's `job_id` or, since a signed request cannot reuse a job number (AUTHORITY section 8), carry a new one: it is
  matched by the manifest, and the answer names the job it resumes. The manifest has to be staged again after a reboot. A
  window that is not later is `invalid`, `bad-args`, and changes nothing. Any other offer supersedes it (UPDATES section 7).
  The resume skips the 60 s limit on offers, which protects the slot from being erased, since it erases nothing.
- **The job.** `job.get` on an open or paused transfer adds `next`, `frames_total`, `window_end` and, paused, `reason`. A paused
  job is open, so it keeps the one-job lock, and `job.cancel` ends it `cancelled` and erases the slot.
- **Deadlines.** A paused job ends `failed`, `stalled`, after 7 days without a resume, and the slot is erased. The days are
  counted as running time in the job's entry, saved hourly, so a reboot does not start them again (and loses at most an hour of
  them). Every other LoRa state keeps LORA_MANAGEMENT section 8's deadlines.

## 8. Platforms

**Where it is enabled.** On every board whose build can hold a whole second image while the first runs: an ESP32 with
two application slots, or an nRF52840 with external flash large enough to stage one. A board without either answers
`unsupported-source`, and `hello` leaves the LoRa source out of its update capabilities. Hardware verification is
claimed board by board, in [hardware evidence](../HARDWARE_VERIFIED.md), only where the bench has run section 10.

| Kind of board | Where the image goes | Apply | Revert | Bench boards |
|---|---|---|---|---|
| ESP32 with two app slots | the inactive app slot | UPDATES section 7, unchanged | UPDATES section 7, unchanged | Heltec V3, V4 |
| nRF52840 with external QSPI flash | staged in QSPI, then copied into the application by a step to be designed | to be designed (section 12) | none: the bootloader keeps one image | Wio Tracker L1 |
| nRF52840 without enough external flash | not enabled | — | — | SenseCAP T1000-E |

A staged nRF52 image is verified against the manifest's digest in QSPI before anything is copied. Writes to QSPI keep
page alignment, including the image's ragged end.

## 9. Radio and airtime

- **Through the duty window.** Every bulk frame and every block answer goes through the sending node's duty window
  (LORA_MANAGEMENT section 7). The firmware never exceeds the band's limit, whatever the manager asks.
- **Control is not starved by bulk.** A node keeps a reserve of its duty window that bulk frames may not use, so a block
  answer, a `parts-missing`, a `job.get` or a revert can always be sent. Proposed: bulk stops at 80% of the window.
- **What it costs.** A full bulk frame is a packet of about 152 bytes. At EU Narrow (869.618 MHz, SF8, 62.5 kHz,
  CR 4/8, 32-symbol preamble) that is about 1.46 s of airtime (computed, not measured). A 1.1 MB deflated ESP32 image is
  about 9,600 frames: about 3.9 hours of airtime, so about 39 hours of wall time at a 10% duty limit, over several
  nights. The private 0.9x line measured about 40 hours on its normal preset. To be measured on the bench (section 10).
- **The fast preset (project owner's decision, 2026-11-23).** The private 0.9x line moved its image in 6.6 hours on a
  temporary fast preset. Here one preset, named `fast`, is fixed in the firmware: it is never a setting and cannot be
  changed over the air. `source.lora.preset: "fast"` in the signed offer moves the target and the gateway to it for the
  transfer only. Both return to their own setting after 5 minutes without a bulk frame, at the window's end, and at any
  reboot, so a target never stays on a preset it cannot hear its manager on. Any other name is `invalid`, `bad-args`.
  `fast` stays inside the band and duty limit of the device's own setting (869.4–869.65 MHz at 10% in the EU).
  Candidates, computed for the same 152-byte packet:
  - SF7 at 250 kHz, centred at 869.525 MHz (the only 250 kHz placement in the band): about 0.13 s a frame, so about 3.4
    hours for the image at 10%;
  - SF7 at 125 kHz: about 0.25 s a frame, so about 6.7 hours.
  Any placement in this band shares the spectrum of the public MeshCore preset (869.525 MHz, 250 kHz, SF11). SF7 is not
  that setting, and SF11 receivers do not decode it, but it shares the air. The choice waits for a bench measurement
  (section 12).

## 10. Conformance

This path is accepted on a board when the bench shows, on real radios and with measured airtime: a whole image moved,
verified, activated, attested and confirmed; a revert drill (an image left unconfirmed reverts); corrupt bytes, a
wrong digest and a stranger's frames refused; a pause at `window_end` and a resume the next night, with the target
rebooted in between; and the gateway and the target both back on their own radio setting afterwards.

## 11. The reference manager and fixtures

So that the firmware can be built and proven without either Backend:
- `tools/lora_ota_manager.py`: a standalone manager that stages, offers, sends blocks through a USB-attached Companion,
  resumes and confirms, with the public test keys;
- signed fixtures for the offer, the manifest parts, the bulk tag, a block answer that arrives after the next block
  has started (a stale `base`), a pause followed by a resume, a captured block answer replayed at the target as a bulk
  frame (dropped), a bulk frame offered to the manager as an answer (dropped), and frames of a wrong length, short and
  long, before the last (dropped) (`test/fixtures/lora_updates_v1.json`);
- simulator tests for loss, reordering, a stranger's frames, a reboot mid-transfer, a pause and a resume, cancellation,
  and the duty reserve. Until the LoRa link is built the simulator's standalone Companion has a test-only operation,
  `update.lora.frame {"frame": <hex>}`, that hands the target one bulk frame and returns the block answer it gave, if any;
  firmware builds do not have it.

A Backend implements the manager side of sections 3 to 7 against the same fixtures.

## 12. Open for review

**Decided by the project owner (2026-11-23):**
1. Enabled on every board that can hold an image, hardware-verified only where the bench has shown it (section 8).
2. One fixed fast preset, named in the signed offer, with an automatic revert (section 9).

**Settled with the reference Backend's coder (review of 2026-11-23):**
3. The offer's fields as section 4 gives them, `size`, `transfer_size` and `frames_total` explicit; `base` in the block
   answer; `paused` a non-final state, its progress in `job.get`.
4. `window_end` required, and bounded by the target's monotonic clock (section 4).
5. The Backend keeps the transfer token with the job, encrypted, deleted at the end; a resume re-offer is the fallback.
   Pacing figures on `rf.bulk` and `rf.status` (section 6).

**Engineering, to settle before building:**
6. ~~`frame_bytes` and the bulk header; whether a block answer fits one reply frame.~~ Settled 2026-11-24 (section 6):
   115 bytes of image per frame in the request datagram's 132; a 22-byte binary block answer in one reply frame.
7. ~~Durable progress on ESP32 (the slot plus `next`) against its flash wear~~ Settled: `next` is kept every 64 frames and at a pause, and a resume rewrites identical bytes. The nRF52 apply step stays open.
8. Page-level deltas against the full deflated image: benchmark on real release pairs before choosing. Deltas tie a
   transfer to one exact base version.
9. The 80% bulk ceiling of the duty window.
10. The `fast` preset's parameters: SF7 at 250 kHz or at 125 kHz (section 9), chosen on a bench measurement of
    airtime and loss beside the public preset's traffic.
