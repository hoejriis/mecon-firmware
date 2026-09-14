# Contributing

mecon-firmware is an upstream-derived MeshCore firmware project with a public backend-neutral integration contract.

## Before changing code

- Preserve native MeshCore behavior and offline independence.
- Prefer changes in the shared MECON runtime over role-specific duplication.
- Keep Companion/Repeater and hardware adapters thin.
- Do not introduce MeshContinuum-specific hosts, credentials, database concepts or private deployment names into firmware.
- Treat `docs/contract/` as the public device API.

## Contract changes

A contract change must:

1. explain why the existing profile/version cannot represent the behavior;
2. remain implementable by a third-party backend/Reader;
3. define compatibility behavior;
4. update contract tests and documentation;
5. coordinate with the MeshContinuum reference implementation without making it normative.

Legacy compatibility belongs in migration adapters, not in the target contract.

## Upstream changes

Keep upstream MeshCore ancestry intact and document unavoidable hooks. New MeshCore releases should be merged and tested across every supported board/role.

## Hardware claims

Do not describe a target or behavior as hardware-verified unless it was tested on the named physical board. CI/build success is not hardware verification.