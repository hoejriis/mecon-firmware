# Availability

The other pages describe the finished product. This page and the release notes say what you can actually get. [Hardware evidence](HARDWARE_VERIFIED.md) says what has run on which board.

**Reviewed 7 November 2026, against `main`, the same commit as v0.1.3.** Anything newer is in the [Issues](https://github.com/hoejriis/mecon-firmware/issues) and the [releases](https://github.com/hoejriis/mecon-firmware/releases).

## What you can get

- **Source, under MIT.** You can build it yourself ([install and set up a device](GETTING_STARTED.md)).
- **Source-only pre-releases**, [v0.1.0 to v0.1.3](https://github.com/hoejriis/mecon-firmware/releases). v0.1.3 carries the features described here, except those in the table below. The earlier three predate most of them.
- **No supported release and no downloadable images.** No release has been signed with production keys. No date is announced.

## Not available yet

| Capability | Where it stands | Issue |
|---|---|---|
| Signed updates with production release keys | The keys exist and builds trust them. No signed release is published yet | [#110](https://github.com/hoejriis/mecon-firmware/issues/110) |
| Every feature checked on all four boards | In progress. [Hardware evidence](HARDWARE_VERIFIED.md) lists what has been checked. A feature not listed there for a board is not verified on it | [#117](https://github.com/hoejriis/mecon-firmware/issues/117) |

Work on the management authority is still open too ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), so its contract can still gain detail.

## What only a manager brings

Managed use needs a manager, and this project publishes none. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) publishes its documentation and target contracts, but no application source or installable release. Standalone use needs no manager.

MECON connected messaging, backfill, RF health monitoring, autonomous outage forwarding and firmware-image delivery over LoRa are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)).
