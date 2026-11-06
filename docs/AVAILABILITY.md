# Availability

The other pages describe the finished product. This page and the release notes say what you can actually get. [Hardware evidence](HARDWARE_VERIFIED.md) says what has run on which board.

**Reviewed 6 November 2026, against `main`.** Anything newer is in the [Issues](https://github.com/hoejriis/mecon-firmware/issues) and the [releases](https://github.com/hoejriis/mecon-firmware/releases).

## What you can get

- **Source, under MIT.** You can build it yourself ([install and set up a device](GETTING_STARTED.md)).
- **Source-only pre-releases**, [v0.1.0 to v0.1.2](https://github.com/hoejriis/mecon-firmware/releases). They predate most of the features described here.
- **No supported release and no downloadable images.** No release has been signed with production keys. No date is announced.

## Not available yet

| Capability | Where it stands | Issue |
|---|---|---|
| Device-interface operations over LoRa, point to point and on the management channel | Contract drafted. Not built, so a manager cannot use this path. Stock remote admin over LoRa works, with this project's restrictions | [#115](https://github.com/hoejriis/mecon-firmware/issues/115) |
| Compact telemetry over LoRa | Not built | [#116](https://github.com/hoejriis/mecon-firmware/issues/116) |
| Standing instructions and periodic reports | Not built, and no contract yet | [#266](https://github.com/hoejriis/mecon-firmware/issues/266) |
| The `screen` telemetry stream | Being built. The other streams are on `main` | [#96](https://github.com/hoejriis/mecon-firmware/issues/96) |
| Signed updates with production release keys | The update path is built and proven with test keys. The keys themselves do not exist yet | [#110](https://github.com/hoejriis/mecon-firmware/issues/110) |
| Every feature checked on all four boards | In progress. [Hardware evidence](HARDWARE_VERIFIED.md) lists what has been checked. A feature not listed there for a board is not verified on it | [#117](https://github.com/hoejriis/mecon-firmware/issues/117) |

Work on the management authority is still open too ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), so its contract can still gain detail.

## What only a manager brings

MECON connected messaging, backfill, RF health monitoring, autonomous outage forwarding and firmware-image delivery over LoRa are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)).
