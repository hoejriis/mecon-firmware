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
| The `screen` telemetry stream | Being built. The other streams are on `main` | [#96](https://github.com/hoejriis/mecon-firmware/issues/96) |
| Enrolling a T1000-E Companion in a Deployment | Refused: its builds have no store for the provisioned marker yet. Its device interface works without enrolment | [#332](https://github.com/hoejriis/mecon-firmware/issues/332) |
| Device interface on the T1000-E Room server | Not in that build yet | [#348](https://github.com/hoejriis/mecon-firmware/issues/348) |
| Signed updates with production release keys | The update path is built and proven with test keys. The keys themselves do not exist yet | [#110](https://github.com/hoejriis/mecon-firmware/issues/110) |
| Every feature checked on all four boards | In progress. [Hardware evidence](HARDWARE_VERIFIED.md) lists what has been checked. A feature not listed there for a board is not verified on it | [#117](https://github.com/hoejriis/mecon-firmware/issues/117) |

Work on the management authority is still open too ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), so its contract can still gain detail.

## What only a manager brings

MECON connected messaging, backfill, RF health monitoring, autonomous outage forwarding and firmware-image delivery over LoRa are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)).
