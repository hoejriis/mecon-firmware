# Supported hardware and roles

MECON 1.0 depends on released **MeshCore 1.18 from upstream `main`**. A target is called supported only after the 1.18-derived, IDF5/NimBLE-based build passes its real-hardware gate.

| Hardware | Companion | Repeater | Wi-Fi | USB | BLE | MQTT |
|---|---:|---:|---:|---:|---:|---:|
| Heltec V3 | Yes | Yes | Yes | Yes | capability-dependent | one active session |
| Heltec V4 | Yes | Yes | Yes | Yes | capability-dependent | one active session |

V3 and V4 are first-class targets from initial bring-up.

## Companion

The Companion image preserves MeshCore 1.18 behaviour, adds three ordered Wi-Fi profiles, local-first/cloud-fallback MQTT, MECON management/observations, and direct USB/BLE where advertised. BLE uses NimBLE.

Companion contact capacity is **not fixed in advance at the private MVP's historical 32/64 values**. It is set from the measured memory budget after pioarduino/Arduino-ESP32 3.x/IDF5 and NimBLE migration and published in status/manifests.

## Repeater

The Repeater image preserves MeshCore 1.18 repeating behaviour and adds the same shared MECON runtime where hardware/role capabilities permit. USB remains the recovery/configuration path. BLE support is explicitly advertised rather than assumed.

## Device UI

Supported display targets retain the stock 1.18 front screen/button model with only the documented MECON 1.0 additions: MECON version at boot, Wi-Fi/MQTT/BLE status, BLE PIN while unconnected, and DM/favourite wake filtering.

## Hardware gate

Each supported V3/V4 Companion/Repeater target must verify stock-derived role behaviour, USB, advertised BLE/NimBLE, all three Wi-Fi slots, local broker discovery and cloud fallback, packet observations, configuration/recovery, reconnect behaviour, memory headroom, UI additions and managed OTA. Compilation alone is not a hardware gate.