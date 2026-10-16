# Wi-Fi profiles

The Companion `_wifi` builds join a network for one purpose: serving the TCP companion port. They hold up to **ten profiles** (SSID, password, priority) and a controller that picks among them. The decision logic is platform-free (`src/helpers/wifi/WifiCore.*`, unit-tested natively); `WifiController.*` is a thin layer over the Arduino `WiFi` API that ESP32 and RP2040 share; `WifiProfiles.*` is the profile store and its CLI. Later modules (MQTT, connectivity-mode switching) ask the same controller for the link instead of joining a network themselves.

What is verified, and how: the decision rules by unit tests against a fake radio and by the simulated-board suite against simulated access points ([Simulated-board test harness](SIMULATION.md)). **Not verified on hardware:** the ESP32 disconnect reasons the controller maps (wrong password, network not found) and the RP2040 status polling are written from the platform sources and compiled for the Heltec V3 and V4 and the Pico W, but no real access point has been joined.

## Selection and failover

- At start the controller scans and joins the highest-priority profile that was seen and is not cooling down. A higher number is tried first; equal priorities go to the lower profile number.
- A wrong password, a network that is not found, a join that times out (20 s) and a link that drops three times before it has lasted 30 s each put that profile into a cooldown and the next visible profile is tried. A wrong password escalates (2 min, then 4, 8, ... up to 1 h per consecutive failure) so a stale password is not retried every minute; editing the profile forgets the cooldown.
- A link that is lost is rescanned before anything is chosen, so a vanished network is not retried blindly.
- While on a profile that is not the best configured one, the controller rescans every 60 s. It switches up only to a profile that has been seen in every scan for the whole **hold-down time (120 s)**; one missed scan restarts the clock. On the best profile there are no scans at all.
- With nothing reachable it backs off (5 s, doubling to 5 min) and scans again.

The timings are `WifiTiming` defaults in `WifiCore.h`.

## Storage

Profiles are stored with the other Companion prefs, so they use the same atomic save. Profile 0 keeps the stock keys (`ssid`, `pwd`, `enabled` in the `wifi` section), so a stock prefs file loads unchanged and the stock commands keep working. Profiles 1 to 9 are `s1`..`s9` (SSID) and `w1`..`w9` (password); every profile has a priority `p0`..`p9` (default 50). With the Wi-Fi section empty the prefs file grows by a few hundred bytes; the whole prefs file with ten profiles of the longest allowed SSID and password is about 1.5 KB. RAM: about 1 KB for the ten profiles (`MECON_WIFI_MAX_PROFILES`, default 10, never below 3).

If profile 0 has no stored SSID the compile-time `WIFI_SSID` / `WIFI_PWD` build defaults apply to it, as in stock.

## Commands

All of them run on a trusted local transport (USB console, or BLE with a non-default PIN); the TCP companion port refuses the CLI, and a remote CLI caller may run the `get` forms only.

| Command | Effect |
|---|---|
| `set wifi.profile <n> ssid <name>` | `n` is 0 to 9; the SSID may contain spaces; 1 to 32 characters |
| `set wifi.profile <n> pwd <passphrase>` | up to 63 characters; with no value, the profile is an open network |
| `set wifi.profile <n> priority <0-255>` | higher is tried first |
| `set wifi.profile <n> clear` | removes the profile, priority back to the default |
| `get wifi.profile <n>` | `<n>: <ssid> prio=<p> pwd=set\|unset last=<reason>` |
| `get wifi.profiles [from]` | every configured profile, paged with `... next:<n>` |
| `get wifi.status` | `connected profile=<n> ssid=<name> rssi=<dBm> ip=<ip> up=<s>`, or the state: `scanning`, `connecting`, `backoff`, `no-profile`, `off` |
| `set wifi.ssid`, `set wifi.pwd`, `set wifi.clear`, `get wifi.ssid`, `get wifi.pwd`, `get wifi.enabled`, `get wifi.ip` | stock commands, profile 0 |

`last=` is the last failure of that profile: `none`, `not-found`, `auth`, `drops`, `timeout` or `other`. A profile edit applies at once (the profile in use is rejoined) with no reboot; `set wifi.enabled` and the first profile on a device that had none still need a reboot to start Wi-Fi.

## Passwords are write-only

No reply, log line or status carries a password; `get` shows only whether one is set. The only stored copy is in the prefs file: the controller asks the radio not to keep its own copy in flash (`WiFi.persistent(false)`). The simulated-board suite scans every file of the node directory and its log for each password it set.

## Postures

Normal serves the TCP companion port on whatever link the controller holds. Hardened starts neither Wi-Fi nor the TCP server, as before ([Standalone security postures](STANDALONE_POSTURES.md)); whether Hardened permits Wi-Fi for MQTT is decided with the MQTT module.

## Known limits

- The controller runs only in the Companion `_wifi` builds, and only if a profile exists at boot.
- A scan while connected briefly competes with traffic on the radio; it happens once a minute and only while a better profile is configured.
- The RP2040 join blocks inside the core for up to 5 s per attempt, as in stock.
- The hold-down and rescan times are fixed at build time.
