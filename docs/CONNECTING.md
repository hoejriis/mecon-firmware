# Connect to Wi-Fi and MQTT

Wi-Fi and MQTT are on every role of the Heltec V3 and V4. The nRF52 boards have no Wi-Fi. Set these over a trusted link: the cable, or BLE paired with your own PIN.

## Choose BLE or Wi-Fi

The Heltec Companion runs one radio stack per boot, BLE or Wi-Fi, by owner decision, to keep memory headroom:

```text
get connectivity.mode
set connectivity.mode wifi        # saves and restarts
```

On the device, open the connectivity page and long-press twice to switch. USB works in both modes. The phone app needs BLE mode, or Wi-Fi mode for messaging only ([connectivity mode](CONNECTIVITY_MODE.md)).

## Wi-Fi profiles

Up to 10 profiles (0-9). The device joins the best visible one by priority, fails over when a network goes, and returns to a better one only after a hold-down.

```text
set wifi.profile 0 ssid Home
set wifi.profile 0 pwd <passphrase>
set wifi.profile 0 priority 80
set wifi.profile 1 ssid Workshop
set wifi.profile 1 pwd <passphrase>
get wifi.profiles
get wifi.status
```

Passwords are write-only: `get wifi.profile <n>` shows `pwd=set` or `pwd=unset`, never the value. The first profile on a device that had none applies at the next reboot ([Wi-Fi](WIFI.md)).

## MQTT broker profiles

Up to 6 profiles (0-5), one session at a time.

**A broker with a pinned certificate:**

```text
set mqtt.profile 0 type static
set mqtt.profile 0 host broker.example.net
set mqtt.profile 0 port 8883
set mqtt.profile 0 tls on
set mqtt.profile 0 anchor <SHA-256 fingerprint, 64 hex digits>
set mqtt.profile 0 user <name>
set mqtt.profile 0 pwd <secret>
get mqtt.status
```

**A private CA instead of a fingerprint.** `tools/mqtt_ca_commands.py <profile> <ca.pem>` prints the `ca begin`, `ca add …` and `ca end` lines to paste ([setting a CA anchor](MQTT.md#setting-a-ca-anchor-over-the-console)).

**A broker on the LAN.** `type discovery` with `site <id>` finds a broker by mDNS. The device uses it only if it advertises that site and passes TLS verification against the profile's anchor.

**Over WebSocket.** `transport ws`, with `path` if the broker does not use `/mqtt`.

`get mqtt.status` reports `tls=verified` only when the anchor matched. A TLS link without an anchor reads `tls=unverified`, and a broker on it cannot manage the device. Hardened refuses plain profiles. When a manager has assigned broker roles, the device tries a local broker first, then the primary, then the secondary, and uses priority within a role ([MQTT](MQTT.md)).

## Next

- [Manage a device](MANAGING.md) over MQTT
- [Monitor a device](MONITORING.md)
