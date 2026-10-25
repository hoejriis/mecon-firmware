# Upstream and porting

> **Target documentation (planned and MECON-specific work; not implemented here unless a section says so).** The tagged releases v0.1.0 to v0.1.2 are source-only pre-releases without the device interface. Current `main` implements the generic [device interface v1](contract/DEVICE_INTERFACE.md) and the standalone features in the [README](../README.md), with real-board evidence in [HARDWARE_VERIFIED](HARDWARE_VERIFIED.md). The generic work still planned (Issues #95, #96, #109, #110, #114 to #117) and the MECON-specific work (Issues #118 to #123) described here are not implemented. Release timing is a separate decision. This page describes the target for the public line, not the private 0.9x reference firmware, whose shipped behaviour differs in places (see [Current-to-target adjustments](CONTRACT_MIGRATION_NOTES.md#reference-firmware-today-09x-line)). See the [shared capability matrix](https://github.com/hoejriis/MeshContinuum/blob/main/docs/CAPABILITIES.md) for current availability and acceptance; specifications do not certify hardware or release readiness.

## MECON target dependency

The canonical upstream is `meshcore-dev/MeshCore`. **The public line is built on MeshCore 1.18.0**, released on upstream `main` on 2026-10-15 (the `companion`, `repeater` and `room-server` v1.18.0 tags, commit 81dbae5). The `dev` branch is suitable for analysis and early validation only. This repository is pinned at upstream commit `81dbae5` (v1.18.0), merged with upstream history. Public releases so far (v0.1.0 to v0.1.2) are standalone source-only pre-releases; release timing for anything more is a separate decision.

The private reference firmware (the 0.9x line, built on MeshCore 1.17.1) is a requirements/test oracle, not the source baseline. The public line (from 0.1.0) and the private 0.9x line are separate lines; version numbers do not order across them.

## What upstream 1.18.0 provides

Verified in the 1.18.0 code, and relevant to the overlap with MECON:

- the companion protocol carries a CLI: `CMD_RUN_CLI_COMMAND` (66) answered by `RESP_CODE_CLI_REPLY` (29), and a remote `TXT_TYPE_CLI_COMMAND` for repeater administration;
- radio and related preferences are shared through `CommonRadioPrefs`, and preferences are persisted in `/prefs.json`;
- ESP32 BLE is still Bluedroid-based upstream; NimBLE is a MECON choice (the private reference gateways already use NimBLE-Arduino 2.5.1).

**Known collisions.** The private reference firmware allocates its own companion response codes at 29, 30 and 31, and 29 is now upstream's `RESP_CODE_CLI_REPLY`. A public build must renumber its extension codes, which are frozen only after the 1.18.0 mapping is complete. Dry-run merges of the private tree also found overlap in its `ENABLE_WIFI_INTERFACE` NodePrefs Wi-Fi fields and in `CommonCLI`; these are reconciled against 1.18.0 rather than ported.

## Initial fork sequence

1. MeshCore 1.18.0 on upstream `main` (done: released 2026-10-15);
2. pin a specific 1.18.0 revision (done: `81dbae5`, upstream history kept);
3. prove stock V3/V4 Companion and Repeater behaviour (done in simulation for the stock behaviour the tests touch, for Companion and Repeater; not on hardware);
4. migrate V3/V4 to pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x;
5. replace upstream's Bluedroid-based ESP32 BLE with NimBLE (the reference gateways show the behaviour to match);
6. prove stock-derived behaviour again;
7. measure memory/runtime headroom and set resource limits;
8. add the shared MECON runtime and transport adapters;
9. extend 1.18 Wi-Fi support to ordered profiles and add one-session local-first MQTT (done as the standalone Wi-Fi and MQTT controllers, simulated only: [Wi-Fi profiles](WIFI.md), [MQTT broker profiles](MQTT.md); the local-first/cloud-fallback policy is part of the MECON stage);
10. add resilience and managed OTA features.

## Patch-surface rule

Reuse 1.18 configuration, command/CLI, board preference, UI, radio, identity and role logic. MECON additions belong in a shared runtime plus thin role/hardware/transport adapters. Do not port private-MVP parallel implementations where upstream 1.18 now provides the primitive.

## Upgrade procedure

Every change to an upstream file is registered in [Upstream delta register](UPSTREAM_DELTA.md); that register is the starting point for a merge.


For later MeshCore releases: merge upstream, review integration hooks, build all V3/V4 role targets, run contract tests and real-hardware gates, record the exact base, then publish.

## Compatibility

An upstream change must not silently change the public MECON contract. Contract/profile versions are independent from firmware and MeshCore release numbers.