# CLI reference

Every command line this firmware accepts, on every role, with where it works, who may run it and how it differs from stock MeshCore 1.18.0. The privilege engine behind the "who" columns is described in [Command-line engine](CLI.md); the stock commands are described in upstream's [CLI commands](cli_commands.md).

This page is checked against the source: `sim/tests/test_cli_reference_sync.py` fails when a command registered in `src/helpers/cli/CliRegistry.h`, or a `mecon.` command or a MECON Wi-Fi, MQTT, posture, audit, connectivity or contacts command in the source, has no row below, and when a row marked MECON names a command the source no longer has. It checks names, not behaviour: the prose in the Notes column is maintained by hand.

What has run where: the sections below describe what the code does. The simulated-board suite exercises the Companion and the Repeater and Room server paths; **nothing here has run on a board**, and the Sensor role is not exercised in the simulator at all.

## How a command reaches the device

| Path | Roles | Caller class | Notes |
|---|---|---|---|
| USB / UART serial console | Repeater, Room server, Sensor | **cable** | A line typed at the serial port. The Companion's serial port speaks the binary app protocol instead; its text console is the rescue console below. |
| Companion app frame `CMD_RUN_CLI_COMMAND` (66), reply `RESP_CODE_CLI_REPLY` (29) | Companion | **cable** over USB, **paired phone** over BLE (a PIN other than 0 and 123456), **untrusted** over the Wi-Fi/Ethernet TCP server and over BLE with a default PIN | The frame itself is gated as a `read`; the line inside is then classified. A line may carry a three-character prefix such as `ab\|` that is reflected in the reply. |
| Rescue console | Companion | **cable** | Entered by a long press of the user button in the first 8 seconds after power-up, on boards that have one. Runs the Companion CLI plus a few file commands, see the last table. |
| Remote LoRa admin | Repeater, Room server, Sensor | **remote admin** (login with the admin password), **guest** | A client logs in with the admin or guest password and sends CLI text. While the admin password is the stock default no admin login is accepted. `get acl`, `erase`, `stats-*`, `log` and `set freq` are console only at the handler level whatever the caller. The Sensor role still accepts the default admin password, as stock. |
| Remote LoRa CLI from a flagged contact | Companion | **flagged contact** | A contact the owner flagged for remote CLI sends a CLI text message. Reads and `reboot` in Normal, nothing in Hardened. |
| MQTT request topic `mecon/<node id>/cli/req` | Companion (Wi-Fi + MQTT builds) | **MQTT, TLS verified** or **plain / unverified** | JSON request, JSON reply on `.../cli/resp`; see [CLI.md](CLI.md#the-command-line-over-mqtt). Runs only on a TLS-verified session. |
| Ethernet TCP console (RAK4631 `_ethernet` builds) | Repeater, Room server | **untrusted** until `login <admin password>`, then **Ethernet admin** | A plain-text line protocol on the network, so never the cable. Every line is refused and audited until the connection sends `login`; the login needs Normal posture and an admin password that has been set. Logged in, the connection has the remote admin's rights (not the key, `erase`, `poweroff`, posture or audit). Hardened turns it off. See [CLI.md](CLI.md#the-ethernet-console). |
| Device button | Companion | **device button** | Not a command line; it only reaches the connectivity-mode switch. |

## Who may run what: the privilege table

A compact copy of the table in [Command-line engine](CLI.md#the-table), where the reply texts and the reasons are. Each cell is `Normal / Hardened`; `-` means allowed. The class of each command is the **Class** column of the tables below.

| Caller | read | write | secret | posture | destructive | reboot |
|---|---|---|---|---|---|---|
| cable (USB / UART) | - / - | - / - | - / refused | - / - | - / - | - / - |
| paired phone (BLE, own PIN) | - / - | - / - | - / refused | - / - | - / refused | - / - |
| untrusted (TCP port, default-PIN BLE, Ethernet console before login, unset context) | refused | refused | refused | refused | refused | refused |
| remote admin (LoRa, password set) | - / - | - / refused | local only / refused | refused | local only / refused | - / - |
| remote admin, default password | refused | refused | refused | refused | refused | refused |
| Ethernet console, logged in | - / off | - / off | local only / off | refused / off | local only / off | - / off |
| guest (LoRa) | refused | refused | refused | refused | refused | refused |
| flagged contact (Companion) | - / off | limited / off | limited / off | refused / off | limited / off | - / off |
| MQTT, TLS verified | - / - | - / flag | refused | refused | refused | - / flag |
| MQTT, plain or unverified | refused | refused | refused | refused | refused | refused |
| device button | refused | refused | refused | allowed | refused | refused |

*limited*: a flagged contact reads and reboots only. *off*: Hardened turns the remote CLI off, reads included. *flag*: allowed only when the broker profile is flagged for management (`set mqtt.profile <n> manage on`). *local only*: refused for the remote caller (explicit reply, audited). A command nobody registered is `write`. Lowering the posture (`set posture normal confirm`) and any `set posture` argument other than `hardened` additionally need the **cable**, whatever the cell says.

Classes: `read` changes nothing; `write` changes settings or transmits (also every unregistered command); `secret` is private-key material; `posture` is the posture, the audit log, the connectivity mode and the broker-management flag; `destructive` erases or powers off; `reboot` is exactly `reboot`. A cell reading `read / write` lists a getter and a setter.

In the tables below *Origin* is **stock** (upstream 1.18.0, unchanged), **stock, behaviour changed** (upstream command, different behaviour here, see [Changed from stock](#changed-from-stock)) or **MECON** (added by this project). *console only* means the command handler itself runs the command only for the local console, over and above the class.

## Companion

The commands the Companion CLI accepts, reached by the app frame, the rescue console, a flagged contact over LoRa, or MQTT. Build-dependent rows say so. Commands this role does not have (`advert`, `clock`, `neighbors`, `region`, ...) are not here: the Companion does those through app frames.

<!-- table: companion -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `reboot` | Restart the device. | stock | reboot | No reply. Exactly `reboot`. |
| `poweroff` | Power the device off. | stock, behaviour changed | destructive | Cable and (Normal) paired phone only; never a flagged contact or a broker. Matched by prefix. |
| `shutdown` | Same as `poweroff`. | stock, behaviour changed | destructive | As `poweroff`. |
| `ver` | Firmware version and build date. | stock, behaviour changed | read | Full MECON identity, `v1.18.0-mecon.<tag>[+<n>.g<hash>][.dirty] (Build: <date>)`; stock printed `v1.18.0`. |
| `board` | Manufacturer / board name. | stock | read |  |
| `get name` / `set name <name>` | Node name. | stock | read / write | `Error, bad chars` for invalid names. |
| `set pin <n>` | Set the BLE pairing PIN. | stock | write | The reply shows the new PIN. A PIN of 0 or 123456 is a published default: a BLE session paired with it is treated as untrusted. The app frame `CMD_SET_DEVICE_PIN` is the same class. |
| `get tz.offset` / `set tz.offset <-12..14>` | Time-zone offset in hours. | stock | read / write | `Error, must be from -12 to +14`. |
| `get contacts.capacity` | Contact store figures: kind, used/capacity, cache, pinned, favourites, evictions, refusals, loads, damaged, write errors, RAM. | MECON | read | Companion builds with the adaptive contact store ([CONTACTS](CONTACTS.md)). `> adaptive store <used>/<cap> cache <n>/<n> ...` or `ram-only`. |
| `get dutycycle` | Duty cycle report: the regulatory band the radio is tuned to, the limit in force, the regulatory floor, the user's own setting and the airtime used in the trailing hour. | stock, behaviour changed | read | Stock printed only the user's setting (`> 50.0%`). Now `> band <lo>-<hi> MHz, limit <p>%, floor <p>%, user <p>%, used <ms> ms (<p>%)`; `band none` and `floor none` outside a regulated band. On the Sensor role the stock reply is unchanged. |
| `get prv.key` / `set prv.key` | Registered as `secret`, but the Companion CLI has no such command. | stock | secret | Answers `Unknown command` once allowed. The key moves through the app frames `CMD_EXPORT_PRIVATE_KEY` (23) and `CMD_IMPORT_PRIVATE_KEY`, which carry the `secret` class: refused in Hardened on every transport, and for an untrusted transport (`ERR_CODE_NOT_PERMITTED`). |
| `get posture` | Show the posture. | MECON | read | `> normal` or `> hardened`; a flagged contact may read it (Normal). |
| `set posture hardened` | Raise the posture to Hardened. | MECON | posture | Cable, or a paired phone. Remote callers, the TCP port and a default-PIN BLE session are refused and audited. |
| `set posture normal confirm` | Lower the posture to Normal. | MECON | posture | Cable only (the paired phone is refused too). Without `confirm` nothing changes. |
| `get audit [start]` | Read the audit log. | MECON | posture | Cable or paired phone. Two entries per reply, newest first. |
| `get connectivity.mode` | `> ble` or `> wifi`. | MECON | read | Builds with `ENABLE_CONNMODE`. |
| `set connectivity.mode ble\|wifi` | Switch the Companion between BLE and Wi-Fi; saves and restarts. | MECON | posture | Cable, paired phone, or the device button. Never a remote caller; audited. `OK - connectivity.mode <mode>, restarting`, or `OK - already <mode>`. |
| `get wifi.profile <n>` | One Wi-Fi profile: `<n>: <ssid> prio=<p> pwd=set\|unset last=<reason>`. | MECON | read | `n` is 0 to 9. Wi-Fi builds (`ENABLE_WIFI_INTERFACE`). |
| `get wifi.profiles [from]` | Every configured profile, paged with `... next:<n>`. | MECON | read |  |
| `set wifi.profile <n> ssid <name>` | SSID of a profile (1-32 characters, spaces allowed). | MECON | write | Applies at once; the profile in use is rejoined. |
| `set wifi.profile <n> pwd [passphrase]` | Passphrase of a profile (at most 63 characters); no value = open network. | MECON | write | Write-only: no reply carries it. |
| `set wifi.profile <n> priority <0-255>` | Higher is tried first. | MECON | write |  |
| `set wifi.profile <n> clear` | Remove a profile. | MECON | write |  |
| `get wifi.ssid` | SSID of profile 0. | stock, behaviour changed | read | `> (not set)` when empty. |
| `set wifi.ssid <name>` | SSID of profile 0. | stock, behaviour changed | write | Stock single-network command, now profile 0 of the list; applies without a reboot. |
| `set wifi.pwd <pw>` | Passphrase of profile 0. | stock, behaviour changed | write | Reply `> wifi.pwd updated`. |
| `get wifi.pwd` | Stock returned the passphrase. Now always `> wifi.pwd is write-only`. | stock, behaviour changed | read | Answered by the engine for every caller; no reply on any transport carries a Wi-Fi secret. |
| `set wifi.clear` | Clear profile 0. | stock, behaviour changed | write |  |
| `set wifi.enabled 0\|1` / `get wifi.enabled` | Switch Wi-Fi off or on. | stock | read / write | Reboot to apply. |
| `get wifi.status` | `connected profile=<n> ssid=<name> rssi=<dBm> ip=<ip> up=<s>`, or `connecting profile=<n> ssid=<name>`, `scanning`, `backoff`, `no-profile`, `off`. | stock, behaviour changed | read | Stock answered only `> connected` or `> disconnected`. |
| `get wifi.ip` | Current IP address. | stock | read |  |
| `get mqtt.profile <n>` | One broker profile: `<n>: static <host>:<port> tls=on\|off anchor=fingerprint\|ca\|unset prio=<p> user=set\|unset pwd=set\|unset last=<reason>`, or the discovery form. | MECON | read | `n` is 0 to 5. MQTT builds (`ENABLE_MQTT`, which needs Wi-Fi). `manage=on` appears only when set. |
| `get mqtt.profiles [from]` | Every configured broker profile, paged with `... next:<n>`. | MECON | read |  |
| `set mqtt.profile <n> type static\|discovery` | A fixed broker, or one found by mDNS for a site. | MECON | write |  |
| `set mqtt.profile <n> host <name-or-ipv4>` | Static profile: broker host (1-63 characters). | MECON | write |  |
| `set mqtt.profile <n> port <0-65535>` | Broker port; 0 = 8883 with TLS, 1883 without. | MECON | write |  |
| `set mqtt.profile <n> tls on\|off` | TLS for a static profile. | MECON | write | `off` is refused in Hardened for every caller (`ERR: plain MQTT is refused in the Hardened posture`). A discovery profile is always TLS. |
| `set mqtt.profile <n> anchor <64-hex>\|clear` | Pin the broker by the SHA-256 fingerprint of its certificate; replaces a CA anchor. | MECON | write | Colons and capitals are accepted. `clear` removes either kind of anchor. |
| `set mqtt.profile <n> ca begin` / `set mqtt.profile <n> ca add <base64>` / `set mqtt.profile <n> ca end` / `set mqtt.profile <n> ca clear` | Trust a CA certificate instead of a fingerprint, sent in pieces and installed whole. | MECON | write | Nothing changes until `ca end` has seen one complete DER certificate. `tools/mqtt_ca_commands.py` prints the lines from a PEM file. |
| `set mqtt.profile <n> site <id>` | Discovery profile: site ID (1-23 characters). | MECON | write |  |
| `set mqtt.profile <n> user [name]` / `set mqtt.profile <n> pwd [secret]` | Broker credentials; no value clears. | MECON | write | Write-only: the listing shows only `set` or `unset`. |
| `set mqtt.profile <n> priority <0-255>` | Higher is tried first. | MECON | write |  |
| `set mqtt.profile <n> manage on\|off` | Let this broker manage the device over MQTT while Hardened. | MECON | posture | Only a caller that may change the posture (cable, paired phone); a broker cannot flag itself. Audited as `mgmt-flag` when refused. |
| `set mqtt.profile <n> clear` | Remove the profile and its credentials and CA. | MECON | write |  |
| `set mqtt.enabled 0\|1` / `get mqtt.enabled` | Stop or resume MQTT without losing the profiles. | MECON | read / write |  |
| `get mqtt.status` | `connected profile=<n> broker=<ip>:<port> tls=verified\|unverified\|plain up=<s>`, or `connecting`, `backoff`, `no-link`, `idle`, `off`. | MECON | read |  |
| `get mecon.mem` | Heap free now, largest allocatable block and lowest free since boot, and the state of the heap floor check. | MECON | read | `> heap free=<n> largest=<n> min_free=<n> heap_low=0\|1 floor=<bytes>`; `min_free=0` where the platform does not track it, `> heap: not measurable on this platform` where there is no heap API. `heap_low=1` while the largest block is below `floor` ([Heap floor](CONNECTIVITY_MODE.md#heap-floor)); `floor=0` on a role or platform that runs no check (Repeater, Room server, non-ESP32). |
| `get mecon.version` | The build's full identity and the upstream base. | MECON | read | `> <full version> (describe <git describe>, upstream <base>)`. |
| `get mecon.boot` | When boot reached each stage, in ms since reset: contact store and prefs loaded, mesh started, end of setup. | MECON | read | `> store_ms=<n> mesh_ms=<n> setup_ms=<n>`; a stage the role does not mark reads `n/a` (the Repeater and Room server have no separate store stage). |
| `get mecon.cli.who` | How the engine sees this caller: transport, authentication, posture and principal. | MECON | read | `> transport=usb auth=cable remote=0 posture=normal principal=cable`; `mgmt=on` is appended for a MQTT session whose broker profile is flagged for management. |
| `get mecon.cli.check <line>` | What the privilege table says to `<line>` for this caller, without running it and without an audit entry. | MECON | read | `> class=write remote-limited`: the class, then `allowed` or the reason. |
| `get radio` / `set radio <freq>,<bw>,<sf>,<cr>` | Show or set frequency (MHz, 150-2500), bandwidth (kHz, 7-500), spreading factor (5-12), coding rate (5-8). | stock | read / write | `OK - reboot to apply`. Saved at once, applied at reboot. |
| `get freq` | Show the frequency. | stock | read |  |
| `get af` / `set af <0-9>` | Airtime factor: after a transmission the node stays silent for `af` times its airtime. | stock | read / write | A duty cycle of `1/(1+af)`. The regulatory floor can make the effective limit stricter, see `get dutycycle`. |
| `set dutycycle <1-100>` | Set the duty-cycle limit in percent (stored as an airtime factor). | stock | write | The command is unchanged; the limit that applies is the stricter of this value and the regulatory floor of the band in use. |
| `get int.thresh` / `set int.thresh <n>` | Interference threshold (dB) for channel-busy detection. | stock | read / write |  |
| `get cad` / `set cad on\|off` | Hardware channel-activity detection. | stock | read / write |  |
| `get radio.rxgain` / `set radio.rxgain on\|off` | Boosted receive gain (SX12xx, LR1110). | stock | read / write | `Error: unsupported` on a radio without it. |
| `get tx` / `set tx <dBm>` | Transmit power. | stock | read / write |  |
| `get rxdelay` / `set rxdelay <0-20>` | Receive-delay base for flood traffic (experimental). | stock | read / write |  |
| `get agc.reset.interval` / `set agc.reset.interval <seconds>` | How often the receiver AGC is reset (rounded to 4 s steps, 0 = never). | stock | read / write |  |
| `get path.hash.mode` / `set path.hash.mode 0\|1\|2` | Size of the path hash in adverts and packets. | stock | read / write |  |
| `get multi.acks` / `set multi.acks <n>` | Extra acknowledgements for direct messages. | stock | read / write |  |
| `get txdelay` / `set txdelay <0-2>` | Retransmit delay factor, flood traffic. | stock | read / write |  |
| `get direct.txdelay` / `set direct.txdelay <0-2>` | Retransmit delay factor, direct traffic. | stock | read / write |  |

The Companion also accepts `get`/`set` lines of the **board** (vendor commands a board class adds through `handleCommand`); those are tried after the MECON commands and are not described here.

### Rescue console

<!-- table: rescue -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `(every Companion CLI command above)` | The rescue console runs the same handler as the app frame, as the cable. | stock | as listed | Posture and class rules are those of the cable row. |
| `rebuild` | Rewrite identity, prefs, channels and audit from memory and rebuild the contacts from the persistent store. | stock, behaviour changed | write | Stock reformatted and re-saved. With the adaptive contact store it does not format; damaged buckets are kept as `/ct<N>.bad` ([CONTACTS](CONTACTS.md)). |
| `erase` | Format the file system. | stock, behaviour changed | destructive | Also returns the posture to Normal and writes a factory-reset audit entry. |
| `ls [path]` | List files (`UserData/...`, `ExtraFS/...`). | stock | write |  |
| `cat <path>` | Print a file in hex. | stock | write | Path must start with `UserData/` or `ExtraFS/`. |
| `rm <path>` | Remove a file. | stock | destructive | Refuses an empty path and `/`. |
| `reboot` | Restart. | stock | reboot |  |

## Repeater, Room server and Sensor

These three roles share one command set (`CommonCLI`, with `MyMesh`/`SensorMesh` in front). The first table is common to all three; the next tables are what only a role adds. "Repeater" below includes any build of the repeater firmware, wherever the command is gated by compile flags it says so.

### Common

<!-- table: common -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `reboot` | Restart the node. | stock | reboot | No reply is sent. Only the exact line `reboot` is the reboot class; the handler also reboots on a line that merely starts with `reboot` (for example `reboot now`), and that line is classified `write`. |
| `poweroff` | Power the board off. | stock, behaviour changed | destructive | Stock let any remote admin run it. Now the cable only; a remote admin gets `ERR: not permitted`. Needs someone on site to power the board on again. Matched by prefix. |
| `shutdown` | Same as `poweroff`. | stock, behaviour changed | destructive | As `poweroff`. |
| `clkreboot` | Reset the clock to 15 May 2024 and reboot. | stock | write | Classified `write`, not `reboot`: refused for a Hardened remote admin. |
| `advert` | Send a flood advert. | stock | write | Matched by prefix (any line starting `advert` that is not `advert.zerohop`). |
| `advert.zerohop` | Send a zero-hop advert. | stock | write |  |
| `clock` | Show the time (UTC). | stock | read | Exactly `clock`. |
| `clock sync` | Set the clock from the sender's timestamp; the clock only moves forward. | stock | write | Meant for a remote admin. From the console the sender timestamp is 0 and the answer is `ERR: clock cannot go backwards`. |
| `time <epoch-seconds>` | Set the clock; forward only. | stock | write | `(ERR: clock cannot go backwards)` otherwise. |
| `start ota` | Start the board's over-the-air update mode. | stock | write | Board dependent. |
| `erase` | Format the file system (factory reset). | stock, behaviour changed | destructive | Console only (the handler requires the local console, a remote caller gets `ERR: not permitted`). Now also returns the posture to Normal and writes a factory-reset audit entry after the format. |
| `ver` | Firmware version and build date. | stock, behaviour changed | read | Stock printed `v1.18.0 (Build: <date>)`; now the full MECON identity, `v1.18.0-mecon.<tag>[+<n>.g<hash>][.dirty] (Build: <date>)`. |
| `board` | Manufacturer / board name. | stock | read | Exactly `board`. |
| `power` | Battery voltage, USB, solar and charger state. | stock | read | Exactly `power`. |
| `password <new>` | Change the admin password. | stock | write | The reply echoes the new password in clear (stock). While the admin password is the stock default (`password`, the build's own default, or empty) remote admin is off entirely; setting it here switches remote admin on. |
| `clear stats` | Reset the statistics counters. | stock | write |  |
| `stats-core` | Core statistics: battery, uptime, queue, debug flags. | stock | write | Console only. Classified `write` because it is not registered; the handler refuses a remote caller anyway. |
| `stats-radio` | Radio statistics: noise floor, RSSI/SNR, airtime, receive errors. | stock | write | Console only, as `stats-core`. |
| `stats-packets` | Packet counters. | stock | write | Console only, as `stats-core`. |
| `log start` | Start capturing the receive log to storage. | stock | write |  |
| `log stop` | Stop capturing. | stock | write |  |
| `log erase` | Erase the captured log. | stock | write |  |
| `log` | Print the captured log to the serial console. | stock | write | Console only. |
| `neighbors` | List nearby repeaters heard. | stock | read | Repeater only; the room server and the sensor answer `not supported`. Exactly `neighbors`. |
| `neighbor.remove <pubkey-hex>` | Forget a neighbor. | stock | write | Repeater only in effect. |
| `tempradio <freq> <bw> <sf> <cr> <minutes>` | Use other radio parameters for a number of minutes, then return to the saved ones. | stock | write | Space-separated, unlike `set radio`. |
| `setperm <pubkey-hex> <permissions>` | Add, change or (permissions 0) remove an ACL entry. | stock | write | Handled by each role's own front end. |
| `get acl` | Print the ACL to the serial console. | stock | read | Console only; the output goes to the serial port, the reply is empty. |
| `get public.key` | This node's public key (hex). | stock | read |  |
| `get role` | The firmware role (repeater, room_server, sensor). | stock | read |  |
| `get prv.key` | Print the private key (hex). | stock, behaviour changed | secret | Console only. Stock already restricted it to the console; now also refused for every caller in Hardened (`ERR: key export is refused in hardened posture`), the cable included. A remote admin: `ERR: not permitted`. |
| `set prv.key <hex>` | Replace the node identity; reboot to apply. | stock, behaviour changed | secret | Local console, Normal posture only. Stock let a remote admin do this. Remote: `ERR: prv.key can only be set from the local console` (explained, not audited); Hardened: `ERR: key import is refused in hardened posture`. |
| `get name` / `set name <name>` | Node name. | stock | read / write | `Error, bad chars` for names with characters the advert format does not allow. |
| `get lat` / `set lat <deg>` | Latitude. | stock | read / write |  |
| `get lon` / `set lon <deg>` | Longitude. | stock | read / write |  |
| `get repeat` / `set repeat on\|off` | Forward other nodes' packets. | stock | read / write | Room server: `set repeat on` answers `ERR - repeat now deprecated`. |
| `get allow.read.only` / `set allow.read.only on\|off` | Room server: let guests read without posting. | stock | read / write |  |
| `get guest.password` / `set guest.password <pw>` | Guest password. | stock | read / write | Not write-only: `get guest.password` returns it in clear to any caller allowed to read (stock). |
| `get owner.info` / `set owner.info <text>` | Owner text shown to clients; `\|` stands for a line break. | stock | read / write |  |
| `get advert.interval` / `set advert.interval <minutes>` | Zero-hop advert interval; 0 = off, otherwise 60-240. | stock | read / write |  |
| `get flood.advert.interval` / `set flood.advert.interval <hours>` | Flood advert interval; 0 = off, otherwise 3-168. | stock | read / write |  |
| `get flood.max` / `set flood.max <0-64>` | Hop limit for flood messages. | stock | read / write |  |
| `get flood.max.unscoped` / `set flood.max.unscoped <0-64>` | Hop limit for unscoped flood messages. | stock | read / write |  |
| `get flood.max.advert` / `set flood.max.advert <0-64>` | Hop limit for advert floods. | stock | read / write |  |
| `get loop.detect` / `set loop.detect off\|minimal\|moderate\|strict` | Loop detection strength. | stock | read / write |  |
| `set freq <MHz>` | Set the frequency, applied at reboot. | stock | write | Console only (`unknown config: freq ...` over the air). Use `set radio` for the full set. |
| `get adc.multiplier` / `set adc.multiplier <f>` | Battery-reading fine tuning; 0 = board default. | stock | read / write | `Error: unsupported` on boards without it. |
| `get extra.sf` / `set extra.sf <sf>[ <sf>...]` | Extra spreading factors to listen on (LR2021 builds only). | stock | read / write |  |
| `get bootloader.ver` | Bootloader version (nRF52 only). | stock | read | `Error: unsupported` elsewhere. |
| `get pwrmgt.support` / `get pwrmgt.source` / `get pwrmgt.bootreason` / `get pwrmgt.bootmv` | nRF52 power management: support, source, boot/shutdown reason, boot voltage. | stock | read | `ERROR: Power management not supported` where the build has none. |
| `get bridge.type` / `get bridge.enabled` / `set bridge.enabled on\|off` / `get bridge.delay` / `set bridge.delay <ms>` / `get bridge.source` / `set bridge.source rx\|tx` / `get bridge.baud` / `set bridge.baud <bps>` / `get bridge.channel` / `set bridge.channel <1-14>` / `get bridge.secret` / `set bridge.secret <text>` | Packet bridge to RS-232 or ESP-NOW; present only in builds with a bridge. | stock | read / write | `get bridge.secret` returns the secret in clear (stock). Not available in the MECON default builds. |
| `powersaving` / `powersaving on\|off` | Show or set power saving. | stock | read / write | Exactly `powersaving` is `read`; `on` and `off` are `write`. nRF52 and ESP32 only. |
| `gps` / `gps on\|off` / `gps sync` / `gps diag` / `gps setloc` / `gps advert [none\|share\|prefs]` | GPS state, switch, time sync, diagnostics, copy the fix to the node position, advert location policy. | stock | read / write | Only in builds with GPS. Exactly `gps` is `read`; every other `gps ...` line is `write`, so `gps diag` and a bare `gps advert` need write rights. |
| `sensor get <key>` / `sensor set <key> <value>` / `sensor list [start]` | Read, set and list the sensor board's settings. | stock | read / write | `sensor get` and `sensor list` are `read`; `sensor set` is `write`. |
| `region` / `region get <name>` / `region list allowed\|denied` | List the region tree, show a region, list allowed or denied regions. | stock | read / write | Exactly `region` and `region get <name>` are `read`. `region list ...`, `region home`, `region default` (the queries without an argument too) are `write`, so a Hardened remote admin cannot ask them. |
| `region load` / `region save` / `region put <name> [parent]` / `region remove <name>` / `region allowf <name>` / `region denyf <name>` / `region home [name]` / `region default [name\|<null>]` | Edit the region tree, flood permissions, home and default scope. | stock | write | `region load` reads the following lines until a blank one. |
| `eth.status` | Ethernet address and port. | stock | write | RAK4631 `_ethernet` builds of the repeater and room server only. Not registered, hence `write` for the gate: the Ethernet console answers it only after `login`, the serial console as the cable. |
| `get radio` / `set radio <freq>,<bw>,<sf>,<cr>` | Show or set frequency (MHz, 150-2500), bandwidth (kHz, 7-500), spreading factor (5-12), coding rate (5-8). | stock | read / write | `OK - reboot to apply`. Saved at once, applied at reboot. |
| `get freq` | Show the frequency. | stock | read |  |
| `get af` / `set af <0-9>` | Airtime factor: after a transmission the node stays silent for `af` times its airtime. | stock | read / write | A duty cycle of `1/(1+af)`. The regulatory floor can make the effective limit stricter, see `get dutycycle`. |
| `set dutycycle <1-100>` | Set the duty-cycle limit in percent (stored as an airtime factor). | stock | write | The command is unchanged; the limit that applies is the stricter of this value and the regulatory floor of the band in use. |
| `get int.thresh` / `set int.thresh <n>` | Interference threshold (dB) for channel-busy detection. | stock | read / write |  |
| `get cad` / `set cad on\|off` | Hardware channel-activity detection. | stock | read / write |  |
| `get radio.rxgain` / `set radio.rxgain on\|off` | Boosted receive gain (SX12xx, LR1110). | stock | read / write | `Error: unsupported` on a radio without it. |
| `get tx` / `set tx <dBm>` | Transmit power. | stock | read / write |  |
| `get rxdelay` / `set rxdelay <0-20>` | Receive-delay base for flood traffic (experimental). | stock | read / write |  |
| `get agc.reset.interval` / `set agc.reset.interval <seconds>` | How often the receiver AGC is reset (rounded to 4 s steps, 0 = never). | stock | read / write |  |
| `get path.hash.mode` / `set path.hash.mode 0\|1\|2` | Size of the path hash in adverts and packets. | stock | read / write |  |
| `get multi.acks` / `set multi.acks <n>` | Extra acknowledgements for direct messages. | stock | read / write |  |
| `get txdelay` / `set txdelay <0-2>` | Retransmit delay factor, flood traffic. | stock | read / write |  |
| `get direct.txdelay` / `set direct.txdelay <0-2>` | Retransmit delay factor, direct traffic. | stock | read / write |  |
| `get dutycycle` | Duty cycle report: the regulatory band the radio is tuned to, the limit in force, the regulatory floor, the user's own setting and the airtime used in the trailing hour. | stock, behaviour changed | read | Stock printed only the user's setting (`> 50.0%`). Now `> band <lo>-<hi> MHz, limit <p>%, floor <p>%, user <p>%, used <ms> ms (<p>%)`; `band none` and `floor none` outside a regulated band. On the Sensor role the stock reply is unchanged. |
| `get mecon.mem` | Heap free now, largest allocatable block and lowest free since boot, and the state of the heap floor check. | MECON | read | `> heap free=<n> largest=<n> min_free=<n> heap_low=0\|1 floor=<bytes>`; `min_free=0` where the platform does not track it, `> heap: not measurable on this platform` where there is no heap API. `heap_low=1` while the largest block is below `floor` ([Heap floor](CONNECTIVITY_MODE.md#heap-floor)); `floor=0` on a role or platform that runs no check (Repeater, Room server, non-ESP32). |
| `get mecon.version` | The build's full identity and the upstream base. | MECON | read | `> <full version> (describe <git describe>, upstream <base>)`. |
| `get mecon.boot` | When boot reached each stage, in ms since reset: contact store and prefs loaded, mesh started, end of setup. | MECON | read | `> store_ms=<n> mesh_ms=<n> setup_ms=<n>`; a stage the role does not mark reads `n/a` (the Repeater and Room server have no separate store stage). |
| `get mecon.cli.who` | How the engine sees this caller: transport, authentication, posture and principal. | MECON | read | `> transport=usb auth=cable remote=0 posture=normal principal=cable`; `mgmt=on` is appended for a MQTT session whose broker profile is flagged for management. |
| `get mecon.cli.check <line>` | What the privilege table says to `<line>` for this caller, without running it and without an audit entry. | MECON | read | `> class=write remote-limited`: the class, then `allowed` or the reason. |

### Repeater and Room server (posture and audit)

The posture and audit commands are handled in the Repeater and Room server front door only. The **Sensor has none of them**: `get posture` answers `??: posture` and the Sensor has no posture to set.

<!-- table: admin -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `get posture` | Show the posture. | MECON | read | `> normal` or `> hardened`; a remote reader may ask. |
| `set posture hardened` | Raise the posture to Hardened. | MECON | posture | Cable. Remote callers are refused and audited (`ERR: not permitted`). Saved atomically with its audit entry. |
| `set posture normal confirm` | Lower the posture to Normal. | MECON | posture | Cable only. Without `confirm` the reply is `ERR: lowering the posture needs: set posture normal confirm` and nothing changes. An argument other than `hardened` or `normal` gives `ERR: set posture hardened \| set posture normal confirm`. |
| `get audit [start]` | Read the audit log: two entries per reply, newest first, `... next:<n>` continues. | MECON | posture | `> <n> entries` then `<time> <event> <caller> <what> x<count>`. Cable only on these roles; every remote caller is refused (`ERR: not permitted`). |

### Repeater only

<!-- table: repeater -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `discover.neighbors` | Send a zero-hop discovery request. | stock | write | Takes no options (`Err - discover.neighbors has no options`). |
| `login <admin password>` / `logout` | Ethernet console only: log this connection in as the Ethernet admin, or out again. | MECON | read | Not a handler command and not registered: the Ethernet console answers it before the gate. Refused in Hardened (`ERR: the Ethernet console is off in hardened posture`) and while the admin password is the stock default or empty; a wrong password answers `ERR: wrong password` and is audited as `login-fail net` without the attempt; five wrong attempts lock logins for a minute. The login ends with the connection. |
| `eth.status` | Ethernet address and port (RAK4631 `_ethernet` builds). | stock | write | On the Ethernet console only after `login` (class `write`); on the serial console as the cable. |

### Room server only

<!-- table: room -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `room.post <text>` | Post a system message to the room. | stock | write | `ERR empty message` for an empty text. |
| `login <admin password>` / `logout` | Ethernet console only: log this connection in as the Ethernet admin, or out again. | MECON | read | Not a handler command and not registered: the Ethernet console answers it before the gate. Refused in Hardened (`ERR: the Ethernet console is off in hardened posture`) and while the admin password is the stock default or empty; a wrong password answers `ERR: wrong password` and is audited as `login-fail net` without the attempt; five wrong attempts lock logins for a minute. The login ends with the connection. |
| `eth.status` | Ethernet address and port (RAK4631 `_ethernet` builds). | stock | write | On the Ethernet console only after `login` (class `write`); on the serial console as the cable. |

### Sensor only

<!-- table: sensor -->
| Command and syntax | What it does | Origin | Class | Notes |
|---|---|---|---|---|
| `io [r\|s\|t] <hex>` | Read the board GPIO word, or write it (value, `r` reset bits, `s` set bits, `t` toggle bits). | stock | write | Handled by the sensor's own front end; the reply is the word after the write. |

## Changed from stock

Every stock command whose behaviour differs. Commands that only exist here are in the tables above, marked MECON.

| Command | Stock 1.18.0 | This firmware |
|---|---|---|
| any command, any role | Each path (serial, app frame, remote text, repeater admin) had its own checks. | One gate, one table ([CLI.md](CLI.md)). A refusal is an explicit reply and an audit entry. An unregistered command is `write`. |
| admin login (Repeater, Room server) | The default password `password` logged in. | Refused while the admin password is the stock default, the build's default, or empty; every remote admin command is refused too. The attempted password is never logged. |
| `poweroff`, `shutdown` | Any remote admin could power the board off. | `destructive`: the cable only (and a paired phone in Normal); a remote admin gets `ERR: not permitted`. |
| `erase` | Console only; formats the file system. | Also returns the posture to Normal and records a factory-reset audit entry. A remote `erase` now answers `ERR: not permitted` and is audited (stock: unknown command). |
| `get prv.key` | Console only. | Console only, and refused in Hardened for every caller including the cable. Remote: `ERR: not permitted`. |
| `set prv.key <hex>` | No caller check: a remote admin could replace the identity. | Local console only, Normal only. Remote: `ERR: prv.key can only be set from the local console` (not audited); Hardened: refused everywhere. |
| remote admin in Hardened | n/a | Reads and `reboot` only; every `write` is `ERR: not permitted in hardened posture`. |
| Companion remote CLI (flagged contact) | Any command the CLI had. | Reads and `reboot` in Normal; off in Hardened. Settings, secrets and `poweroff` are refused. |
| `get wifi.pwd` (Companion) | Printed the passphrase. | `> wifi.pwd is write-only` for every caller. |
| `set wifi.ssid`, `set wifi.pwd`, `set wifi.clear`, `get wifi.ssid` (Companion) | One network. | Profile 0 of a list of ten; applies at once, no reboot. |
| `get wifi.status` (Companion) | `> connected` or `> disconnected`. | Profile, SSID, signal, address and uptime when connected; otherwise the state (`connecting`, `scanning`, `backoff`, `no-profile`, `off`). |
| `get dutycycle` | `> <percent>%` from the airtime factor. | Regulatory report (band, limit in force, floor, user setting, airtime used); Sensor unchanged. `set dutycycle` and `set af` are as before but the limit that applies is the stricter of the setting and the regulatory floor of the band. |
| `ver` | `v1.18.0 (Build: <date>)` | `v1.18.0-mecon.<tag>[+<n>.g<hash>][.dirty] (Build: <date>)`. The upstream base stays first. |
| `rebuild` (Companion rescue console) | Reformatted the file system and re-saved. | Rebuilds the contacts from the persistent store without formatting; keeps damaged buckets aside. |
| prefs saving (all settings) | Rewritten in place. | Written to a temporary file, verified and swapped in: a power cut keeps the previous complete prefs. No change to any command's syntax or reply. |
| `unknown config: ...`, `??: ...` replies | Echoed the whole line into the reply buffer. | Truncated to fit; text unchanged otherwise. |

## Examples

Values below are made up. Replace every `example-` and `broker.example.net` value with your own; never post a real passphrase, key or fingerprint-pinned broker credential into a ticket or chat.

### Add a Wi-Fi network (Companion)

```
set wifi.profile 1 ssid Example Net
set wifi.profile 1 pwd example-passphrase
set wifi.profile 1 priority 10
get wifi.profile 1
> 1: Example Net prio=10 pwd=set last=none
get wifi.status
```

### Add an MQTT broker pinned by fingerprint (Companion)

The fingerprint is the SHA-256 of the broker's certificate (DER); `openssl x509 -in broker.crt -noout -fingerprint -sha256` prints it.

```
set mqtt.profile 0 type static
set mqtt.profile 0 host broker.example.net
set mqtt.profile 0 port 8883
set mqtt.profile 0 tls on
set mqtt.profile 0 anchor 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
set mqtt.profile 0 user example-user
set mqtt.profile 0 pwd example-secret
set mqtt.enabled 1
get mqtt.status
> connected profile=0 broker=192.0.2.10:8883 tls=verified up=12s
```

### Add an MQTT broker trusted through a CA (Companion)

A CA certificate is too long for one line, so it goes in pieces; the profile is unchanged until `ca end` has seen one complete certificate. `tools/mqtt_ca_commands.py <profile> <ca.pem>` prints these lines from a PEM file.

```
set mqtt.profile 1 type static
set mqtt.profile 1 host broker.example.net
set mqtt.profile 1 tls on
set mqtt.profile 1 ca begin
set mqtt.profile 1 ca add MIIBszCCAVmgAwIBAgIUexample...first-chunk
set mqtt.profile 1 ca add ...next-chunk...
set mqtt.profile 1 ca end
get mqtt.profile 1
> 1: static broker.example.net:0 tls=on anchor=ca prio=0 user=unset pwd=unset last=none
```

### Raise the posture and read the audit log

```
get posture
> normal
set posture hardened
OK - posture hardened
get audit
> 2 entries
1760000100 refused remote-admin remote-write x3
1760000000 posture cable hardened x1
```

Lowering needs the cable and the word `confirm`: `set posture normal confirm`. Allow a broker to manage a Hardened Companion with `set mqtt.profile 0 manage on` (from the cable or a paired phone), then send requests on `mecon/<node id>/cli/req`.

### Check the duty cycle

```
get dutycycle
> band 869.400-869.650 MHz, limit 10.00%, floor 10.00%, user 50.00%, used 3200 ms (0.08%)
```

`limit` is what the node enforces: the stricter of `user` (from `set dutycycle`/`set af`) and the regulatory `floor` of the band the radio is tuned to.

### Read the heap, the version and the boot times

```
get mecon.mem
> heap free=142336 largest=110592 min_free=98304 heap_low=0 floor=20480
get mecon.version
> v1.18.0-mecon.0.1.2 (describe v0.1.2, upstream v1.18.0)
get mecon.boot
> store_ms=1180 mesh_ms=1420 setup_ms=2650
ver
v1.18.0-mecon.0.1.2 (Build: 17 Oct 2026)
```

### Ask what a caller may run

```
get mecon.cli.who
> transport=lora auth=contact-flag remote=1 posture=normal principal=remote-cli
get mecon.cli.check set name x
> class=write remote-limited
```

## Known gaps

- **Sensor role.** Shares `CommonCLI` and therefore the gate for remote callers, but has no posture or audit commands and the stock login (the default admin password still logs in). Not exercised in the simulator or on hardware.
- **Rescue console `ls`, `cat`, `rm`** are not routed through the engine: they exist only on the cable and are not registered, so a remote caller never reaches them.
- **Board commands.** Vendor commands added by a board class are not described here.
