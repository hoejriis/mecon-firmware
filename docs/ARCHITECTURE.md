# Architecture

## Layering

mecon-firmware is an extension of upstream MeshCore, not an independent RF stack.

```text
┌─────────────────────────────────────────────┐
│ upstream MeshCore                           │
│  Companion role          Repeater role      │
└──────────────┬──────────────────┬───────────┘
               │ thin adapters    │
┌──────────────┴──────────────────┴───────────┐
│ shared MECON runtime                        │
│ identity · config · health · replay         │
│ Wi-Fi · MQTT · observations · security      │
└───────┬─────────────┬─────────────┬─────────┘
        │             │             │
      Wi-Fi          MQTT          USB/BLE
      ×3             ×2           direct Reader
```

Role adapters translate the generic MECON operations into native MeshCore role APIs. Hardware adapters contain board-specific radio/display/power details. Neither should duplicate the common runtime.

## Native role is authoritative

With every MECON transport unavailable:

- a Companion remains usable by normal MeshCore clients over its supported local interfaces;
- a Repeater continues normal MeshCore repeating/advertising behavior;
- no MECON service is required for RF receive, forwarding or ordinary local Companion use.

The Companion release profile uses 64 contact slots. Contact retention must protect explicitly managed identities and evict ordinary learned contacts deterministically when space is needed.

## Connectivity independence

Wi-Fi, MQTT, USB and BLE are transports, not roles. Device capabilities are advertised explicitly and clients must not infer them merely from board, role or firmware version.

The target supports three Wi-Fi profiles and two concurrent MQTT broker profiles. Broker authority is independent per broker. A second broker is not implicitly a failover clone of the first.

## Direct Reader path

A Companion can expose the same logical MECON operations over USB and BLE to a browser Reader. The direct transport must reuse the same command/configuration envelopes and authorization concepts as MQTT wherever transport characteristics permit.

Desktop Chrome and Edge are the supported browser targets. Repeater direct access is USB in the initial release target; Repeater BLE is not required.

A Reader may operate in two ways:

- **bridge mode:** browser transports device events/jobs to a reachable backend;
- **standalone mode:** browser operates the attached device locally and synchronizes later.

These modes must not require different firmware semantics.

## Contract ownership

`mecon-firmware/docs/contract/` is canonical for the device-facing protocol. MeshContinuum consumes that contract as the reference backend/Reader. Contract changes must remain implementable by third parties without access to MeshContinuum internals.

## Upstream boundary

The repository retains upstream MeshCore ancestry and a documented upstream remote. MECON-owned code should be isolated so upgrading MeshCore normally means:

1. merge a newer upstream MeshCore release;
2. resolve a small documented set of integration hooks;
3. build every supported board/role;
4. run protocol tests;
5. run hardware release gates;
6. publish a new mecon-firmware release.

No third-party MeshCore fork is part of the update chain.