# Second review of the authority layer: decisions

Outcome of the second security review (2026-10-25) of the device-side authority layer. This page records what was decided
for each finding. It describes no exploit detail. The contract itself (AUTHORITY.md) is not changed by this page; where a fix
is stricter than, or differs from, the contract, the contract needs a matching edit, listed under "Contract follow-ups".

## Fixed

| Id | Summary | Decision | Commit |
|---|---|---|---|
| H1 | The Companion rescue console's file commands could read or delete the record and identity files | Fixed: refused with an explicit reply while a record exists, and in Hardened with none | 188dea40 |
| H2 | The nRF52 crypto RNG could wait forever when the SoftDevice was not enabled | Fixed: bounded polling, checked errors, the RNG peripheral when the SoftDevice is off, the caller's RNG on failure | a68b0d1e |
| M1 | An unsigned first provisioning over BLE could not tell a device-drawn PIN from a static or chosen one | Fixed: the BLE path is removed; the cable is the only unsigned first provisioning | 65ebb79d |
| M2 | An enrolled Hardened device could not be lowered; Hardened was allowed without a local unlock secret | Fixed: a signed request may lower the posture; Hardened with a record needs a secret in it | 5d6efb38 |
| L1 | A damaged marks copy fell back silently to the older one | Fixed: every member is blocked until re-admitted, and `marks_fallback` is reported | f02dfeca |
| L2 | The stock-array rescue rebuild restored the provisioned marker late | Fixed: restored right after the format | 7d517312 |
| L3 | The local unlock secret could travel in the clear over any transport | Fixed: accepted only over the USB cable (`usb_only`) | 7846a1ca |

## Not fixed in this slice

| Id | Summary | Decision |
|---|---|---|
| L4 | A USB unlock outlives a change of cable user until it expires | Deferred: follow-up "clear the unlock on a USB line-state change and shorten the window" |
| L5 | An unlock in Normal opens the whole posture class, not only the posture command | Deferred: follow-up "narrow the unlocked cable to `set posture`", with the contract wording |
| L6 | The Ethernet console admin keeps `reboot` on an enrolled server | Deferred: follow-up "decide the Ethernet console's row in the caller table" |
| L7 | The highest `job_id` value is the blocked-mark sentinel | Deferred: follow-up "refuse that `job_id` and say so in section 8"; small, status-only effect |
| L8 | Entropy of the ESP32 RNG on server roles that run no radio | Deferred: follow-up "enable the bootloader random source around boot-time draws, or mix in radio noise" |
| I1 | The duplicate-result cache key uses a truncated instance id | Deferred: follow-up "use the full id or the member index" |
| I2 | The record is stored in plaintext on flash, like the node key | Accepted: same protection class as the node key; the contract wording "stored as a secret" is to be clarified |
| I3 | The unlock lockout counter lives in RAM | Accepted: the secret is 256 bits, so a reset of the counter buys nothing |
| I4 | The unlock HMAC has no domain prefix | Accepted: challenges are random per link; a prefix is a possible later hardening |
| I5 | A server `region load` in progress consumes later console lines | Deferred: follow-up "run the front door before a multi-line load consumer" (predates the branch) |
| I6 | Non-canonical point encodings are not rejected in the weak-key check | Accepted: replay protection keys on `job_id`, not on signature uniqueness |
| I7 | A corrupt record's salvaged unlock secret is trusted on the frame CRC | Accepted: planting one needs flash write access, which has stronger options already |

## Contract follow-ups

- Section 6.1: BLE no longer counts for an unsigned first provisioning (M1).
- Sections 9 and 11: the rescue console's file commands (H1), a signed request lowering the posture (M2), and the
  Ethernet console row (L6) belong in the caller table.
- Section 4: the marks fallback flag and its effect (L1).
- Sections 6.1 and 10: `usb_only` for the unlock secret (L3).
- Section 10: Hardened with a record needs the secret, reported as `no-unlock-secret` (M2).
