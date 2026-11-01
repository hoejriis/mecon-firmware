# Contributing

This repository holds the firmware source (MeshCore 1.18.0 with this project's additions), its simulated-board test suite, the documentation and the public contracts. Useful contributions are hardware evidence on the four test boards, bug reports with a reproduction, tests, documentation corrections and contract questions. To build and test, follow [architecture: testing](docs/ARCHITECTURE.md#testing) and the [test harness](docs/SIMULATION.md). Every change to a file that exists upstream is added to [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md) in the same pull request.

Use [Discussions](https://github.com/hoejriis/mecon-firmware/discussions) for firmware questions and ideas, and [Issues](https://github.com/hoejriis/mecon-firmware/issues) for a specific contract question, documentation correction or hardware evidence. General conversation about MECON lives in [MeshContinuum's Discussions](https://github.com/hoejriis/MeshContinuum/discussions), and beta interest in [its interest form](https://github.com/hoejriis/MeshContinuum/issues/new?template=beta-interest.yml); no invitation or date is promised.

Contributions here use the [MIT licence](LICENSE), with upstream MeshCore's copyright retained. Do not post secrets, private traffic or provisioning exports. See [security reporting](SECURITY.md).

mecon-firmware is an upstream-derived MeshCore firmware project with a public backend-neutral integration contract.

## Before changing code

- Preserve native MeshCore behavior and offline independence.
- Prefer changes in the shared MECON runtime over role-specific duplication.
- Keep Companion/Repeater and hardware adapters thin.
- A new behaviour change needs a test that fails on stock 1.18.0 and passes here, and the mutation check (`sim/mutate.sh`) should show the test can fail.
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