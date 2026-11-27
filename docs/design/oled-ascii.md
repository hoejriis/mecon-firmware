# MECON OLED display — approved ASCII design reference

Source: approved 128×64 PNG mockups, 15 November 2026. **These text diagrams are content/layout transcriptions, not pixel-identical bitmap reproductions.** The approved PNGs remain the visual reference for exact 128×64 spacing and logo shape. All displayed values are examples.

**This page is the approved design, not the shipped behaviour.** What the firmware shows is in [UI](../UI.md#boot-splash) and `src/helpers/ui/` (`MeconSplash.h`, `FrontPage.h`). The six front-page cases below match what ships (the row texts, the labels, `never` and the `..` clip at 21 columns). The splash differs in its values: the shipped URL line is `https://mecon.cloud/`, the MECON line shows the firmware's own version (`MECON 0.3.7` from `v1.18.0-mecon.0.3.7`), and the MeshCore line carries the upstream version without a leading `v` (`MeshCore 1.18.0`). Hostnames, networks and times in the diagrams are examples.

## Boot splash (same for Companion and Repeater)

```text
+--------------------------------+
|  o--\ /--o  MeshContinuum      |
|  o--/ \--o                     |
|--------------------------------|
|      https://example.net/      |
|       MECON 0.90.28            |
|       MeshCore v1.17.1         |
+--------------------------------+
```

The four-node crossed-loop ASCII mark is **schematic only**. Implement using the approved MeshContinuum logo asset, not the ASCII approximation. Versions must come from running firmware. The URL has a trailing slash.

## Common front page — six approved cases

Rows 1–2 retain the stock device-name/battery and page dots. Rows 3–7 contain operational diagnostics. Last LoRa refers **only to received LoRa packets**. Connected SSIDs and broker names have no labels/prefixes; disconnected states use explicit text.

### 1. Repeater — WiFi and MQTT connected
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Normal Repeater                 |
|Last LoRa: 10 sec ago           |
|Mode: WiFi                      |
|HomeNetwork                     |
|mqtt.example.net                |
+--------------------------------+
```

### 2. Repeater — WiFi offline
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Normal Repeater                 |
|Last LoRa: 2 min ago            |
|Mode: WiFi                      |
|WiFi not connected              |
|MQTT not connected              |
+--------------------------------+
```

### 3. Repeater — BLE Reader
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Hardened Repeater               |
|Last LoRa: 3 sec ago            |
|Mode: BLE                       |
|Connected via BLE               |
|MQTT not connected              |
+--------------------------------+
```

### 4. Repeater — USB agent
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Normal Repeater                 |
|Last LoRa: 1 min ago            |
|Mode: USB                       |
|Connected via USB               |
|MQTT not connected              |
+--------------------------------+
```

### 5. Companion — WiFi and MQTT connected
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Normal Companion                |
|Last LoRa: 8 sec ago            |
|Mode: WiFi                      |
|CamperSpot                      |
|mqtt.example.net                |
+--------------------------------+
```

### 6. Companion — BLE Reader
```text
+--------------------------------+
|example.net                 [===]|
|       * . . . . . . . .        |
|Hardened Companion              |
|Last LoRa: never                |
|Mode: BLE                       |
|Connected via BLE               |
|MQTT not connected              |
+--------------------------------+
```

## Navigation and semantics

- Repeater: this replaces the existing front page; stock useful pages remain accessible where available.
- Companion: this is the first/default page; retain all existing stock screens and wireless-mode selection/long-press behaviour.
- Screen dots reflect the actual page count and active page, not the illustrative dots above.
- Battery icon reflects actual battery capability/state; never invent a percentage.
- Line 4 is last received LoRa packet only, not transmit, MQTT, BLE, USB or agent traffic.
- Line 6 displays WiFi SSID or direct Reader/agent BLE/USB status; line 7 displays **direct device MQTT** broker or disconnected. A Reader's own MQTT session must not be presented as the device's.
- Truncate or otherwise safely handle long names while preserving all seven lines.
- Text mockups use more characters than the real 128×64 bitmap may accommodate; verify actual glyph widths, truncation and clipping against approved native PNGs and hardware.

