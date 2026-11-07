# Availability

The other pages describe the finished product. This page and the release notes say what you can actually get. [Hardware evidence](HARDWARE_VERIFIED.md) says what has run on which board.

**Reviewed 7 November 2026, against `main`, which is ahead of v0.1.3.** Anything newer is in the [Issues](https://github.com/hoejriis/mecon-firmware/issues) and the [releases](https://github.com/hoejriis/mecon-firmware/releases).

## What you can get

- **Source, under MIT.** You can build it yourself ([install and set up a device](GETTING_STARTED.md)).
- **Source-only pre-releases**, [v0.1.0 to v0.1.3](https://github.com/hoejriis/mecon-firmware/releases). v0.1.3 carries the features described here, except those in the table below. The earlier three predate most of them.
- **No supported release and no downloadable images.** No release has been signed with production keys. No date is announced.

## Not available yet

| Capability | Where it stands | Issue |
|---|---|---|
| Device-interface operations over LoRa, point to point and on the management channel | On `main`, in no release. Passes in the simulator on all three roles. On real radios: a Heltec V4 Repeater answered a Heltec V3 Companion on both paths, on a build from before the merge. Not run on a Room server or Companion as the target, a Wio Tracker L1 or a SenseCAP T1000-E, across a hop, or with a lost frame retried. Stock remote admin over LoRa is separate, and works with this project's restrictions | [#115](https://github.com/hoejriis/mecon-firmware/issues/115) |
| Compact reports over LoRa | On `main`, in no release. Passes in the simulator. On real radios: a 99-byte report on request arrived with its signature verified, on both paths, on the same boards and build. Cadence and airtime are not measured | [#116](https://github.com/hoejriis/mecon-firmware/issues/116) |
| Standing instructions | On `main`, in no release. Passes in the simulator. On a board only adding and removing one has run: no scheduled report yet. | [#266](https://github.com/hoejriis/mecon-firmware/issues/266) |
| Signed updates with production release keys | The keys exist and builds trust them. No signed release is published yet | [#110](https://github.com/hoejriis/mecon-firmware/issues/110) |
| Every feature checked on all four boards | In progress. [Hardware evidence](HARDWARE_VERIFIED.md) lists what has been checked. A feature not listed there for a board is not verified on it | [#117](https://github.com/hoejriis/mecon-firmware/issues/117) |

Work on the management authority is still open too ([#95](https://github.com/hoejriis/mecon-firmware/issues/95)), so its contract can still gain detail.

## What only a manager brings

Managed use needs a manager, and this project publishes none. [MeshContinuum](https://github.com/hoejriis/MeshContinuum) publishes its documentation and target contracts, but no application source or installable release. Standalone use needs no manager.

MECON connected messaging, backfill, RF health monitoring, autonomous outage forwarding and firmware-image delivery over LoRa are later work, outside the product these pages describe ([what the firmware leaves to a manager](INTEGRATION.md#what-the-firmware-does-and-what-a-manager-does)).
