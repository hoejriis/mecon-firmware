# Update and recover

## Signed firmware updates

An update through the device interface needs two signatures:

- **The release manifest**, signed by a release key. It says which board, layout and role the image is for, and the device checks it against the release keys built into its firmware.
- **The install request** (`update.offer`), signed by a manager of the device's Deployment. A device that is not in a Deployment takes it unsigned, but only over the USB cable.

No other caller may install firmware, a BLE link paired with the device's own PIN included.

### The steps

1. `update.manifest` sends the signed manifest. The device answers what its preflight would decide: the right board, layout and role, a version allowed from the running one, and not below the device's security floor. A downgrade must be signed as one.
2. `update.offer` starts the job, with the image either pushed by the manager in chunks (`update.chunk`) or downloaded by the device from a URL on boards with an IP link.
3. The device checks the image against the manifest, reboots into it, and reports the image it is running.
4. The manager sends `job.confirm` once the new image works. Being reachable is not enough to confirm.

`update.status` shows the open job, the security floor and the staged manifests at any time. The [signed updates](contract/UPDATES.md) contract gives every field, error and resource rule.

### Per board

| Board | Delivery | If the new image fails |
|---|---|---|
| Heltec V3, V4 | pushed over USB or BLE (a manager on MQTT offers it and a bridge pushes it), or downloaded by the device over IP | The image boots on trial. It is reverted unless confirmed in time, and at once after a crash, watchdog or brownout reset |
| T1000-E, Wio Tracker L1 | a signed offer, then the bootloader's serial DFU over USB (USB only, by owner decision) | There is no previous image to return to. The device reports its version. A failed image is replaced over USB |

Stock MeshCore's unsigned Wi-Fi updater (`start ota`) is refused in these builds.

### What signatures do not protect

No board's bootloader checks signatures, and no secure boot or flash encryption is configured. Anyone with the device and a cable can enter the bootloader and install any image, which then inherits the device's keys and records. Signed updates protect the remote and bridged paths. They do not protect a device in someone else's hands.

## Interrupted work

- **A configuration change sent remotely** (radio, transmit power, connectivity mode, Wi-Fi or broker settings, the management channel) takes effect as a job that must be confirmed. It reverts if it is not confirmed in time, if it is cancelled, or if the device boots twice first ([manage a device](MANAGING.md#changes-that-could-cut-you-off)).
- **A save cut by power loss.** Settings, identity, channels and contacts are written to a temporary copy, checked and swapped in. A cut leaves the previous complete file. A damaged identity is set aside as `<file>.damaged`, not replaced.
- **An update cut short.** The job fails with `interrupted`, and the running image is unchanged. On a Heltec board an image already written but not confirmed is reverted.
- **Results across a disconnect.** A job's result is kept until the manager collects it with `job.get`, across a reboot too.

## Resets

Resets come in explicit, named scopes, so clearing one kind of state does not clear the others. The scopes and what each keeps are in [managed operations](contract/OPERATIONS.md#35-reset-scopes).

| Reset | How | What it does |
|---|---|---|
| Factory reset | Companion: the app's factory reset over USB, or `erase` on the rescue console. Repeater and Room server: `erase` on the console | Erases everything, a new node key included, and returns the device to Normal. It always works over USB in both postures. The audit log records it |
| Rebuild (Companion) | `rebuild` on the rescue console | Rewrites identity, settings, channels and the audit log from memory and rebuilds the contact store. Keeps a Deployment record |
| Leave a Deployment | `deployment.reset` | Removes the Deployment record. Keeps the node key, `device_id`, settings, contacts and channels. For a healthy record: signed, over USB, in Normal. For a corrupt record: the unsigned cable in Normal, or after a local unlock in Hardened |

To enter the rescue console on a Companion, long-press the user button within 8 seconds of the logo screen appearing ([CLI reference](CLI_REFERENCE.md#rescue-console)).

## Locked out

- **Over BLE or Wi-Fi.** Use USB. It is trusted in every posture, and a factory reset there always works.
- **A Hardened Companion in Wi-Fi mode** has no app transport, by owner decision. Use USB.
- **A Repeater or Room server refusing a correct password.** The client's clock once ran ahead and left a watermark in the future. A restart of the device clears it and changes no setting ([postures](STANDALONE_POSTURES.md#recovery)).
- **A device moved from the private reference firmware** reads that firmware's Deployment record as corrupt and refuses changes until it is cleared. [Backend migration](migration/BACKEND_MIGRATION.md#moving-a-board-from-the-private-090x-line) gives the steps.
