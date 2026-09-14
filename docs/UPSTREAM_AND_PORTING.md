# Upstream and porting

## Canonical upstream

The canonical firmware upstream is [`meshcore-dev/MeshCore`](https://github.com/meshcore-dev/MeshCore).

mecon-firmware should retain upstream Git ancestry and merge upstream releases rather than periodically copying source snapshots. Other MeshCore forks may be consulted as references, but are not part of the dependency or update chain unless explicitly documented.

## Patch-surface rule

MECON additions should live in:

- a shared MECON runtime;
- thin Companion and Repeater adapters;
- thin hardware adapters;
- explicit, documented upstream hooks where unavoidable.

Avoid duplicating upstream configuration, radio, identity or role logic. Translate MECON operations into native MeshCore APIs/storage where practical.

## Upgrade procedure

For each supported upstream MeshCore release:

1. fetch and merge the new upstream release;
2. review the documented integration-hook inventory;
3. resolve only necessary upstream conflicts;
4. build all supported V3/V4 Companion/Repeater targets;
5. run contract and compatibility tests;
6. run real-hardware release gates;
7. record the exact MeshCore base version in manifests/status;
8. publish a signed mecon-firmware release.

## Compatibility rule

A new MeshCore base must not silently change the public MECON contract. If an upstream change requires a contract change, version the affected MECON profile/contract independently from the firmware release.

## Porting to new hardware

New hardware support should primarily supply board/radio/display/power configuration. It should not fork the MECON runtime. A new target is promoted only after the same contract and hardware gates used by existing supported targets pass.