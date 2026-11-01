# Upstream and porting

mecon-firmware is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) built on the upstream v1.18.0 release (commit `81dbae5`, tagged `companion-v1.18.0` and `repeater-v1.18.0` upstream), with upstream history kept. Upstream's `dev` branch is used for investigation only: a release is always built on a pinned upstream release.

## What upstream 1.18.0 provides

These are facts about the 1.18.0 code, relevant where this project extends it:

- the Companion protocol carries a CLI: `CMD_RUN_CLI_COMMAND` (66), answered by `RESP_CODE_CLI_REPLY` (29), and a remote `TXT_TYPE_CLI_COMMAND` for repeater administration;
- radio and related preferences are shared through `CommonRadioPrefs`;
- ESP32 BLE upstream is Bluedroid. NimBLE is this project's choice ([runtime platform](PLATFORM.md)).

## Patch-surface rule

Reuse upstream's configuration, CLI, board preferences, UI, radio, identity and role logic. Additions belong in the shared code under `src/helpers/`, with role, hardware and transport adapters kept thin ([architecture](ARCHITECTURE.md)). Do not add a parallel implementation of something upstream provides.

## Upgrading to a later MeshCore release

Every change to an upstream file is registered in the [upstream delta register](UPSTREAM_DELTA.md), and that register is the starting point for a merge:

1. merge the upstream release;
2. review each registered hook against the new code;
3. build every target board and role, and run the unit, simulated-board and contract tests;
4. run the hardware checks on the four test boards;
5. record the new base, then release.

## Compatibility

An upstream change must not silently change the public contract. Contract and profile versions are independent of firmware and MeshCore release numbers.
