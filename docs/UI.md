# Device UI: screens and button gestures

What the on-device display shows and how the button drives it, for the Companion, Repeater and Room server, with what is **stock MeshCore** and what **this firmware changed**. Written for people using the firmware and for developers of other projects reading or porting the code.

> **Where the pictures come from.** Every screen below marked *captured* is the text the real `ui-new` code drew on the **simulated board** ([Simulated-board test harness](SIMULATION.md)), drawn into a 21-column by 8-row character grid from the x/y coordinates the display driver was given (one column is 6 px, one row is 8 px, as on the 128x64 OLED). **They are text renderings, not photographs.** Graphics (battery icon, page dots, the Bluetooth/advert/power icons, the popup frame) are not recorded by the simulator and are not drawn, except that an alert popup is drawn as a box. Hardware photos will follow after hardware verification. Until then, **nothing here has been seen on a real OLED**, and the real font may break lines slightly differently.
>
> The captures are regenerated, and checked for drift, by `sim/tests/test_ui_captures.py` (see [Regenerating the captures](#regenerating-the-captures)).

## Which builds have which UI

| UI code | Used by | Display | Notes |
|---|---|---|---|
| `examples/companion_radio/ui-new` | Companion builds for boards with a 128x64 OLED or TFT, including every Heltec V3 and V4 Companion env (`_usb`, `_ble`, `_wifi`, `_standalone`) | multi-page home screen, message preview, alerts | the UI this document describes in detail |
| `examples/companion_radio/ui-orig` | older and smaller-screen boards (for example Wio E5 mini, RAK3112, T1000-E, Thinknode M3/M4/M7, Xiao nRF52) | one home screen, message preview, one-line alerts | stock, unchanged |
| `examples/companion_radio/ui-tiny` | the T-Echo card | small-display (U8g2) screens with a scrolling status bar; same pages and gestures as `ui-new` minus the Bluetooth-skip, Wi-Fi line and connectivity additions (pages: home, recent, radio, Bluetooth, advert, GPS, sensors, hibernate) | stock, unchanged |
| none | Companion builds on boards without a display (`DISPLAY_CLASS` not defined) | nothing | the device is driven by the phone app, USB or the CLI; the button, where there is one, still handles the first-8-seconds rescue |
| `examples/simple_repeater/UITask.cpp`, `simple_room_server`, `simple_sensor` | Repeater, Room server, Sensor on boards with a display | boot splash, then node name and radio settings | stock, unchanged |

**What this firmware changed:** only `ui-new`, and only when the build defines `ENABLE_CONNMODE`, which today means the two **standalone Companion** envs (`Heltec_v3_companion_radio_standalone`, `heltec_v4_companion_radio_standalone`). One fork commit touches `ui-new` (it adds the connectivity page and a status line; it removes two lines of stock code, listed below); no fork commit touches `ui-orig`, `ui-tiny` or the Repeater, Room server and Sensor screens. Every other `ui-new` env (`_usb`, `_ble`, `_wifi`) compiles to the stock screens. See [Connectivity mode](CONNECTIVITY_MODE.md) for the feature and [Standalone security postures](STANDALONE_POSTURES.md) for the posture it displays.

## The button

The Heltec V3 and V4 have one user button, and everything is driven by gestures on it. Boards with a joystick, rotary encoder or extra buttons map the same actions onto those; the table is for the single-button case, which is what the simulator replays.

| Gesture | `ui-new` single-button boards (stock, unchanged here) | `ui-orig` (stock) |
|---|---|---|
| **Click** | If the display is off: turn it on and nothing else. Otherwise: next home page; on the message preview, next unread message | wake the display; dismisses a message preview |
| **Double click** | previous home page | send an advert ("Advert sent!" / "Advert failed..") |
| **Triple click** | toggle the buzzer, on boards that have one ("Buzzer: OFF" / "Buzzer: ON"; saved) | toggle the buzzer (or cycle buzz/vibe modes on boards with a haptic driver) |
| **Quadruple click** | not used | toggle GPS ("GPS: Enabled" / "GPS: Disabled") |
| **Long press** (threshold set by the board's button object, about a second) | the page's action: toggle Bluetooth, send advert, toggle GPS, send a repeater discovery, hibernate; **on the connectivity page, arm or confirm the mode switch (new)**. On the message preview: clear all unread | after the first 8 s: power off |
| **Long press in the first 8 seconds after the UI starts** | **opens the CLI rescue console instead of any page action** (`enterCLIRescue`). The press is consumed. Stock behaviour, unchanged | same, stock |

The display turns itself off after 15 s without input (board settings may change this; a board on USB power can be set to keep it on). A click on a dark display only wakes it. (A double or triple click also wakes it, and then acts.)

**Stock versus new for gestures:** nothing about click, double click, triple click or the 8-second rescue changed. The one addition is that the long press, which already meant "do this page's action" on the Bluetooth, Advert, GPS, Discovery and Hibernate pages, now also means "arm the mode switch" on the new connectivity page, and a second long press confirms. That is why the 8-second rule matters in practice: a long press shortly after boot goes to the rescue console, not to the connectivity page.

## Companion home pages, in order

Page order is fixed by the build. Click goes forward, double click goes back, both wrap around. On the simulated standalone board the order was recorded by clicking through (graphics-only pages are only a label and an icon):

```text
home -> recent -> radio -> bluetooth -> connectivity -> advert -> discovery -> hibernate -> (back to home)
```
```text
home -> recent -> radio -> connectivity -> advert -> discovery -> hibernate -> (back to home)
```

| # | Page | Stock `ui-new` | This firmware |
|---|---|---|---|
| 1 | **Home** | node name, battery, unread message count (large `MSG: n` on builds without Wi-Fi, small `Unread Msg: n` on Wi-Fi builds), on Wi-Fi builds the line `IP: a.b.c.d`; `< Connected >` while a phone, USB host or TCP client is attached, otherwise the BLE pairing `Pin:nnnnnn` | on standalone builds the IP line becomes the **status line** (mode, Wi-Fi address or PIN class); the pairing PIN is not drawn in Wi-Fi mode |
| 2 | **Recent adverts** ("Recent adverts" alert on entry) | up to 4 recently heard nodes, with the age of each (`12s`, `4m`, `2h`) | unchanged |
| 3 | **Radio** | frequency, spreading factor, bandwidth, coding rate, TX power, noise floor | unchanged |
| 4 | **Bluetooth** | on/off icon; long press toggles BLE | unchanged in BLE mode; **skipped entirely in Wi-Fi mode** (there is no BLE stack to toggle) |
| 5 | **Connectivity** | does not exist | **new**: mode, PIN class, phone, Wi-Fi, IP, MQTT, and the mode switch |
| 6 | **Advert** | icon; long press sends an advert ("Advert sent!") | unchanged |
| 7 | **GPS** (boards with GPS) | on/off, fix, satellites, position, altitude; long press toggles | unchanged |
| 8 | **Sensors** (boards that define it) | scrolling sensor values | unchanged |
| 9 | **Repeater discovery** ("Repeater disc" alert on entry) | long press sends a discovery request; nearby repeaters are listed with SNR | unchanged |
| 10 | **Hibernate** | long press: "hibernating..." then power off | unchanged |

A new message interrupts any page and shows the **message preview**: `Unread: n` and the age, the sender or channel with its hop count (`(2) Name:`; `(D)` for direct), then the wrapped text. Click shows the next older unread message, long press clears the queue. Unchanged.

### Stock pages, captured

These are the same stock pages as on any MeshCore `ui-new` build; they are captured here so the other pictures have context.

```text
+---------------------+
|ExampleNode          |
|                     |
|FQ: 869.618   SF: 8  |
|BW: 62.50     CR: 5  |
|                     |
|TX: 20dBm            |
|Noise floor: -110    |
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|                     |
|                     |
|                     |
|                     |
|  toggle: long press |
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|                     |
|                     |
|                     |
|                     |
|  advert: long press |
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|                     |
|                     |
|                     |
|                     |
| discover: long press|
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|                     |
|                     |
|                     |
|                     |
| hibernate:long press|
|                     |
+---------------------+
```

## New and changed screens

### Home page status line

On a standalone build the home page shows one line at y=32 that says how the phone or the network reaches the device. **Stock** drew `IP: a.b.c.d` there on Wi-Fi builds and nothing on BLE-only builds. **Changed:**

| State | Line | Colour |
|---|---|---|
| BLE mode, per-device PIN | `BLE` | normal |
| BLE mode, default PIN (0 or 123456) | `BLE (default PIN!)` | warning |
| Wi-Fi mode, connected | `Wi-Fi <address>` | normal |
| Wi-Fi mode, not connected | `Wi-Fi: <state>` (for example `Wi-Fi: backoff`) or `Wi-Fi: off` | normal |
| Wi-Fi mode, no profile | `Wi-Fi: no profile` | normal |

BLE mode, per-device PIN (*captured*; the simulated USB port always counts as a connected host, which is why `< Connected >` shows):

```text
+---------------------+
|ExampleNode          |
|                     |
|    Unread Msg: 0    |
|                     |
|         BLE         |
|    < Connected >    |
|                     |
|                     |
+---------------------+
```

BLE mode, default PIN, the warning case:

```text
+---------------------+
|ExampleNode          |
|                     |
|    Unread Msg: 0    |
|                     |
|  BLE (default PIN!) |
|    < Connected >    |
|                     |
|                     |
+---------------------+
```

Wi-Fi mode, connected (*captured*), and with no profile:

```text
+---------------------+
|ExampleNode          |
|                     |
|    Unread Msg: 0    |
|                     |
|   Wi-Fi 192.0.2.10  |
|    < Connected >    |
|                     |
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
|    Unread Msg: 0    |
|                     |
|  Wi-Fi: no profile  |
|    < Connected >    |
|                     |
|                     |
+---------------------+
```

**BLE pairing PIN on the home page (stock, kept).** With no host attached, in BLE mode the stock firmware draws `Pin:nnnnnn` in the warning colour (size 2, or size 1 on builds with a clock) where `< Connected >` would be. That is how a user pairs a phone, so it is kept. It is **not drawn in Wi-Fi mode** (**changed**: the stock condition gained "and the mode is BLE"). This state is **not captured**: the simulator cannot present a disconnected host, so the layout is described from the code only. Per-device PINs are random numbers shown only here; the connectivity page below never shows digits.

### Connectivity page

A four-line status page and the mode switch. Layout: lines at y=19, 28, 37, 46 (a line is 21 characters, longer text is cut with `...`), and a hint at y=55.

BLE mode, Normal posture, per-device PIN, no phone (*captured*):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: BLE, Normal    |
|Pin: per-device      |
|Phone: not connected |
|Wi-Fi, MQTT: off     |
| to Wi-Fi: long press|
|                     |
+---------------------+
```

| Line | BLE mode | Wi-Fi mode |
|---|---|---|
| 1 | `Mode: BLE, Normal` or `Mode: BLE, Hardened` | `Mode: Wi-Fi, Normal` or `Mode: Wi-Fi, Hardened` |
| 2 | `Pin: per-device` or `Pin: DEFAULT (unsafe)` (warning) | `Wi-Fi p<n> <ssid> <rssi>dBm` when connected; `Wi-Fi p<n> <state>` while joining; `Wi-Fi: <state>`, `Wi-Fi: no profile` or `Wi-Fi: off` |
| 3 | `Phone: connected` or `Phone: not connected` | `IP: <address>` or `IP: -` |
| 4 | `Wi-Fi, MQTT: off` | `MQTT<n> plain`, `MQTT<n> TLS verified`, `MQTT<n> TLS UNVERIFIED` (warning), or `MQTT: <state>`, `MQTT: no profile`, `MQTT: off` |

The bottom hint is `to Wi-Fi: long press` in BLE mode and `to BLE: long press` in Wi-Fi mode (`press Enter` on boards with a joystick or rotary input).

**Posture.** Line 1 names the security posture ([Standalone security postures](STANDALONE_POSTURES.md)); the page redraws when it changes.

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: BLE, Hardened  |
|Pin: per-device      |
|Phone: not connected |
|Wi-Fi, MQTT: off     |
| to Wi-Fi: long press|
|                     |
+---------------------+
```

**Default PIN.** A default PIN is a warning, never a credential: the page says `DEFAULT (unsafe)` and has no field that could hold the digits.

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: BLE, Normal    |
|Pin: DEFAULT (unsafe)|
|Phone: not connected |
|Wi-Fi, MQTT: off     |
| to Wi-Fi: long press|
|                     |
+---------------------+
```

**Phone connected** (the BLE interface here is a stub that the test tells a phone is connected; the screen logic is real, BLE is not, see [What was not reachable](#what-was-not-reachable-in-the-simulator)):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: BLE, Normal    |
|Pin: per-device      |
|Phone: connected     |
|Wi-Fi, MQTT: off     |
| to Wi-Fi: long press|
|                     |
+---------------------+
```

#### Wi-Fi mode

Connected, no MQTT profile:

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExNet -61dBm|
|IP: 192.0.2.10       |
|MQTT: no profile     |
|  to BLE: long press |
|                     |
+---------------------+
```

The line is 21 characters wide. A long network name is cut with `...` and pushes the signal strength off the line:

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExampleNe...|
|IP: 192.0.2.10       |
|MQTT: no profile     |
|  to BLE: long press |
|                     |
+---------------------+
```

Link lost (the controller is retrying; the address is dropped):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi: backoff       |
|IP: -                |
|MQTT: no profile     |
|  to BLE: long press |
|                     |
+---------------------+
```

No profile configured (USB is then the only transport; the button switches back):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi: no profile    |
|IP: -                |
|MQTT: no profile     |
|  to BLE: long press |
|                     |
+---------------------+
```

Hardened with no MQTT profile that Hardened permits: Wi-Fi is not started at all ([MQTT broker profiles](MQTT.md)):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Hardened|
|Wi-Fi: off           |
|IP: -                |
|MQTT: no profile     |
|  to BLE: long press |
|                     |
+---------------------+
```

#### MQTT line

Plain TCP session to broker profile 0:

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExNet -61dBm|
|IP: 192.0.2.10       |
|MQTT0 plain          |
|  to BLE: long press |
|                     |
+---------------------+
```

TLS with the broker certificate verified against the pinned fingerprint:

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExNet -61dBm|
|IP: 192.0.2.10       |
|MQTT0 TLS verified   |
|  to BLE: long press |
|                     |
+---------------------+
```

TLS with no pinned anchor: encrypted but not verified, drawn as a warning and never called "verified":

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExNet -61dBm|
|IP: 192.0.2.10       |
|MQTT0 TLS UNVERIFIED |
|  to BLE: long press |
|                     |
+---------------------+
```

No session yet (the controller state is shown instead; here the broker is unreachable and the controller is backing off):

```text
+---------------------+
|ExampleNode          |
|                     |
|Mode: Wi-Fi, Normal  |
|Wi-Fi p0 ExNet -61dBm|
|IP: 192.0.2.10       |
|MQTT: backoff        |
|  to BLE: long press |
|                     |
+---------------------+
```

### Mode-switch confirmation and alerts

On the connectivity page a **long press** arms the switch. The page is replaced by a confirmation that waits 10 s:

```text
+---------------------+
|ExampleNode          |
|                     |
|   Switch to Wi-Fi?  |
|                     |
|   Device restarts   |
|   yes: long press   |
|      no: click      |
|                     |
+---------------------+
```

In Wi-Fi mode the same screen asks `Switch to BLE?`:

```text
+---------------------+
|ExampleNode          |
|                     |
|    Switch to BLE?   |
|                     |
|   Device restarts   |
|   yes: long press   |
|      no: click      |
|                     |
+---------------------+
```

- A **second long press** within 10 s confirms: the new mode is saved, the audit entry is written, the alert **`Restarting...`** shows for 3 s and the device restarts about 1.5 s later.
- A **click**, a **double click** or the **10 s timeout** cancels and changes nothing. A click or double click shows the alert **`Cancelled`** for 0.8 s. A timeout cancels silently.
- If the mode cannot be changed the alert is **`Not changed`**.
- Leaving the connectivity page also disarms.

Alerts are drawn as a box over the current page (the popup is drawn at y=21 to y=42, text centred). Captured:

```text
+---------------------+
|ExampleNode          |
|                     |
| +-----------------+ |
| |    Cancelled    | |
| |                 | |
| +-----------------+ |
| to Wi-Fi: long press|
|                     |
+---------------------+
```
```text
+---------------------+
|ExampleNode          |
|                     |
| +-----------------+ |
| |  Restarting...  | |
| |                 | |
| +-----------------+ |
|  to BLE: long press |
|                     |
+---------------------+
```

Note the page behind `Restarting...`: the mode has already been saved, so the hint under the popup already names the other mode (`to BLE`), a faithful picture of the device a moment before it reboots.

The switch works in both postures, because it needs physical possession of the device, and is audited. A remote caller cannot trigger it (see [Connectivity mode](CONNECTIVITY_MODE.md)).

### Other alerts

Stock alerts, unchanged: `Recent adverts`, `Repeater disc`, `Advert sent!`, `Advert failed..`, `GPS: Enabled`, `GPS: Disabled`, `Buzzer: ON`, `Buzzer: OFF`. Boards that auto-shut-down on a low battery draw `Low Battery.` / `Shutting Down!` (stock).

## What `ui-new` changed, line by line

For developers porting or auditing: the fork's change to `ui-new/UITask.cpp` is one commit. All of it is under `#ifdef ENABLE_CONNMODE` except the two lines it replaced:

- stock `#ifdef ENABLE_WIFI_INTERFACE ... IP: a.b.c.d` on the home page is now the `#elif` branch, used only when `ENABLE_CONNMODE` is not defined;
- stock `} else if (the_mesh.getBLEPin() != 0) {` (draw the PIN) gained `&& mode == BLE` under `ENABLE_CONNMODE`;
- new `CONNECTIVITY` page in the `HomePage` enum, its render branch, its confirmation state (`_sw_confirm`, 10 s), and the `KEY_ENTER` handling that arms and confirms;
- the Bluetooth page is skipped by the next/previous-page code when the mode is not BLE;
- `ConnStatusScreen.h` reads the live Wi-Fi, MQTT and BLE state into `ConnMode::ConnStatus`, and `src/helpers/connmode/ConnStatus.h` turns that into the text above.

## What is never drawn

No screen of this firmware draws a **Wi-Fi password, an MQTT username, password or token, a private key, an admin or guest password**, or the **digits of a PIN on the connectivity page**. This is structural, not a filter: the status text is built from `ConnMode::ConnStatus`, a plain struct with fields for the SSID (a network name, not a secret), signal strength, address, profile numbers, state names and booleans, and **no field that could hold a secret**, so the screen cannot draw what it is never given. A network name is drawn on purpose (it identifies which profile joined), and MQTT broker host names are not drawn at all.

The one deliberate exception is stock behaviour kept on purpose: the **BLE pairing PIN** appears on the home page of the device's own display while no host is attached, because that is how a person pairs a phone with a device they hold. It is not drawn in Wi-Fi mode, and a default PIN is flagged instead of shown elsewhere.

The simulated suite checks this: `test_no_secret_is_ever_drawn` sets Wi-Fi and MQTT credentials, visits every page in both modes, arms the confirmation and changes the posture, then asserts that no credential appears in any recorded frame of any boot.

## Repeater, Room server and Sensor

**Unchanged from stock.** No fork commit touches these screens, and the Hardened posture is not shown on them. The Repeater (`examples/simple_repeater/UITask.cpp`) has no pages:

- the boot screen for 4 s: logo, `https://meshcore.io`, version and build date, and `< Repeater >` (`< Room Server >`, `< Sensor >` on the others);
- then the node name and two radio lines, `FREQ: 869.618 SF8` and `BW: 62.50 CR: 5`, refreshed every second;
- a **click** wakes the display and does nothing else; the display turns off after 20 s;
- a **long press** shows the logo and `Turning OFF` and powers the board off after 3 s;
- the 8-second rescue rule is a Companion feature and does not apply here; a Repeater is managed through its CLI ([CLI reference](CLI.md)).

These screens were not run in the simulator; they are described from the code.

## What was not reachable in the simulator

- **BLE.** The simulator has no BLE stack. A stub stands in for it, logs when BLE would start and can be told a phone is connected, so the *screens* and the *mode logic* are exercised, but pairing, the PIN prompt a phone shows and the link itself are not. **The `Phone: connected` capture shows the screen reacting to a stub, not a real connection.**
- **The home-page pairing PIN** (`Pin:nnnnnn`, `< Connected >` absent). The simulated USB port always reports a host, so this state cannot be drawn there. Described from code only.
- **Graphics**: battery icon, page dots, the Bluetooth, advert and power icons, the splash logo, and the popup frame (drawn here by the renderer). Not recorded by the simulator.
- **Real OLED rendering and timing**: the real font metrics, contrast, the 15 s auto-off, the real click timing. Not run on hardware.
- **Boards other than the simulated one**: joystick, rotary, TFT and e-paper variants. Not run.
- **`ui-orig`, `ui-tiny`, Repeater and Room server screens**: described from the code, not run. They are unchanged from stock.

Hardware photos will replace these text renderings once a board has passed acceptance.

## Regenerating the captures

Everything in `docs/ui/` is produced by `sim/tests/test_ui_captures.py` from the real `ui-new` code on the `sim_companion_standalone` build, using `sim/ui_render.py` to turn recorded `(x, y, size, text)` strings into the grid. Inside the dev container ([Simulated-board test harness](SIMULATION.md)):

```sh
sim/build.sh                                                   # all sim envs, one pio run
sim/run.sh python3 -m pytest sim/tests/test_ui_captures.py -q  # fail if a screen drifted from docs/ui/
SIM_DOCKER_ARGS="-e UI_CAPTURE_UPDATE=1" \
  sim/run.sh python3 -m pytest sim/tests/test_ui_captures.py -q   # rewrite docs/ui/*.txt
```

The renderer draws every string at its recorded coordinates and never moves one. Cells claimed by two draws are shown as `#` and listed under the grid (`! overlap: ...`); a string running past the 128x64 panel is listed as `! clipped: ...`. No capture currently shows either.

The test also fails if a capture in `docs/ui/` is missing from this page, so the pictures here cannot drift from the firmware. The MQTT captures need `mosquitto` and `openssl`, which the dev image carries. Test values are made up (the network `ExNet`, chosen short so the signal strength fits the line; `ExampleNetwork-5G` for the long-name case; `ExampleNode`, documentation addresses from 192.0.2.0/24).
