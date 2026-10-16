# Standalone security postures

Companion, Repeater and Room server carry a security posture: **Normal** (the default) or **Hardened**. It needs no Deployment, no enrolment and no backend. It is the standalone line's implementation; the broader managed-fleet model is in [Security model](SECURITY_MODEL.md).

What the simulated-board suite checks here is software behaviour. BLE needs a radio stack the simulator does not have: its rule is covered by unit tests of the decision functions, not by a simulated pairing (see [Simulated-board test harness](SIMULATION.md)).

## What each posture does

| | **Normal** (default) | **Hardened** |
|---|---|---|
| Management over USB / UART | yes | yes |
| Management over BLE | non-default PIN only | non-default PIN only; with a default PIN (0 or 123456) BLE does not start |
| TCP companion server (Wi-Fi, Ethernet) | messaging only; management commands are refused | not started (Wi-Fi is joined only for MQTT over TLS, see [MQTT broker profiles](MQTT.md)) |
| MQTT broker profiles (Wi-Fi) | plain or TLS static profiles, discovery profiles | plain static profiles refused (at the command and when used), TLS static and discovery profiles as in Normal |
| Remote admin over RF (repeater, room server) | refused while the admin password is the default; otherwise as stock minus `set prv.key` | read-only plus `reboot`; no remote settings changes |
| Remote CLI on a Companion (contact flag) | reads plus `reboot` | off |
| Command line over MQTT (Companion, Wi-Fi) | TLS-verified broker session only: reads, settings and `reboot`; never the posture, the key or a factory reset | TLS-verified broker session only: reads; settings and `reboot` only when the broker profile is flagged for management (`set mqtt.profile <n> manage on`, set locally) |
| Private key export / import | trusted local transports | refused on every transport, USB included |
| Factory reset | trusted local transports | USB / UART only; always works there and returns the device to Normal |

In both postures, while the admin password is the stock default (`password`, the build's own `ADMIN_PASSWORD`, or empty), a remote admin login is refused and so is every remote admin command, even from a session that was opened before. A password set on the local console opens remote admin in Normal; in Hardened it opens a read-only session. Guest access is as stock in both postures (it is read-only by design). The Hardened column is a refusal with an explicit reply (a Companion frame answers `ERR_CODE_NOT_PERMITTED`, 7), never a silent drop, and each refusal is audited.

Every row of the two tables above is a cell of one privilege table, shared by all the ways a command reaches the device: see [Command-line engine](CLI.md).

An attempted password is never logged or stored: stock's debug print of it is removed.

Connectivity-mode switching (BLE or Wi-Fi, [Connectivity mode](CONNECTIVITY_MODE.md)) is allowed from trusted local transports and the device button in both postures, refused for remote callers, and audited.

## Commands

| Command | Who may run it |
|---|---|
| `get posture` | anyone who may run `get` commands, including a remote reader |
| `set posture hardened` | a trusted local transport: the USB / UART console, or BLE paired with a non-default PIN. Never a remote caller |
| `set posture normal confirm` | the USB / UART console only. A remote caller, a paired phone and an unauthenticated TCP client are refused, and the refusal is audited. Without `confirm` the command explains itself and changes nothing |
| `get audit [n]` | a trusted local transport only (two entries per reply, newest first, `... next:<n>` continues) |

The posture is one byte kept with the prefs and saved atomically, so it survives reboots and a power cut during a save: a cut while lowering leaves the device Hardened, a cut while raising leaves the previous complete prefs (Normal). Any stored value other than Normal is treated as Hardened, so a damaged byte can only make the device stricter.

On a Hardened Companion that is Wi-Fi-only (no USB, no BLE), the app has no transport left: recover through the USB console, or factory-reset over USB.

## Recovery

Factory reset over USB / UART works in both postures: the Companion's `CMD_FACTORY_RESET` frame over the cable, and the console `erase` on a Repeater or Room server (the rescue console `erase` on a Companion). It returns the device to Normal. In Hardened a factory reset from BLE is refused, so a paired phone cannot strip the posture.

## Audit log

A persistent ring of 32 security events, readable with `get audit` on a trusted local transport. It records:

- posture changes (the new posture),
- refused commands: the command class and who asked (cable, BLE, network, remote admin, remote CLI, MQTT),
- failed admin logins (wrong password, or the default password) and logins refused because the password is the default,
- factory resets,
- a transport that Hardened did not start (BLE, TCP server).

It records no secret, no password and no command arguments. Repeats of the same kind (event, caller, command class) fold into one entry with a count and the latest timestamp, so a caller over the air cannot push a posture change out of the ring: it would need 32 distinct kinds. The audit log survives a factory reset: the reset erases the files, and the log is written again with the reset on it. Timestamps are the node's clock, which may be unset on a device that has never been synchronised.

Flash cost: one 268-byte file (`/audit.bin`, fixed record size: 8-byte header, 32 entries of 8 bytes, CRC32), saved through the atomic file path (a temporary copy of the same size exists for a moment). A posture change or factory reset is written at once; every other event at most once every 10 minutes (the first after a boot at once), so a sustained attack costs at most 144 writes a day. A power cut loses at most the last interval's events, never the file; a damaged or truncated file is ignored and the log starts empty. RAM cost: about 270 bytes.

## Known limits

- The audit log is evidence for the owner, not tamper-proof: someone with the USB console can factory-reset the device. The reset itself is recorded.
- Boot-time non-starts (BLE with a default PIN, TCP servers) are recorded once, not on every boot, so a crash loop cannot become a flash-write loop.
- A device that is Hardened and has only the BLE transport with a default PIN cannot be reached over the air; use USB.
- The Hardened rules for key export and import apply to the Companion frames and to the Repeater and Room console `get prv.key` / `set prv.key`.
