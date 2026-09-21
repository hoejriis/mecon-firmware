# Upstream and porting

## MECON 1.0 dependency

The canonical upstream is `meshcore-dev/MeshCore`. **MECON 1.0 depends on MeshCore 1.18 being merged/released on upstream `main`.** The current `dev` branch is suitable for analysis and early validation only. The production refactor begins from a pinned 1.18 revision after it reaches `main`.

The private MVP is a requirements/test oracle, not the source baseline.

## Initial fork sequence

1. wait for MeshCore 1.18 on upstream `main`;
2. pin a specific 1.18 revision;
3. prove stock V3/V4 Companion and Repeater behaviour;
4. migrate V3/V4 to pioarduino / Arduino-ESP32 3.x / ESP-IDF 5.x;
5. replace legacy ESP32 BLE assumptions with NimBLE;
6. prove stock-derived behaviour again;
7. measure memory/runtime headroom and set resource limits;
8. add the shared MECON runtime and transport adapters;
9. extend 1.18 Wi-Fi support to three ordered profiles and add one-session local-first MQTT;
10. add resilience and managed OTA features.

## Patch-surface rule

Reuse 1.18 configuration, command/CLI, board preference, UI, radio, identity and role logic. MECON additions belong in a shared runtime plus thin role/hardware/transport adapters. Do not port private-MVP parallel implementations where upstream 1.18 now provides the primitive.

## Upgrade procedure

For later MeshCore releases: merge upstream, review integration hooks, build all V3/V4 role targets, run contract tests and real-hardware gates, record the exact base, then publish.

## Compatibility

An upstream change must not silently change the public MECON contract. Contract/profile versions are independent from firmware and MeshCore release numbers.