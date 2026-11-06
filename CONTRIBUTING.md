# Contributing

This repository holds the firmware source (MeshCore 1.18.0 with this project's additions), its simulated-board test suite, the documentation and the public contracts. Useful contributions are hardware evidence on the four test boards, bug reports with a reproduction, tests, documentation corrections and contract questions. To build and test, follow [architecture: testing](docs/ARCHITECTURE.md#testing) and the [test harness](docs/SIMULATION.md). Every change to a file that exists upstream is added to [`docs/UPSTREAM_DELTA.md`](docs/UPSTREAM_DELTA.md) in the same pull request.

Use [Discussions](https://github.com/hoejriis/mecon-firmware/discussions) for firmware questions and ideas, and [Issues](https://github.com/hoejriis/mecon-firmware/issues) for a specific contract question, documentation correction or hardware evidence. General conversation about MECON lives in [MeshContinuum's Discussions](https://github.com/hoejriis/MeshContinuum/discussions), and beta interest in [its interest form](https://github.com/hoejriis/MeshContinuum/issues/new?template=beta-interest.yml); no invitation or date is promised.

Contributions here use the [MIT licence](LICENSE), with upstream MeshCore's copyright retained. Do not post secrets, private traffic or provisioning exports. See [security reporting](SECURITY.md).

mecon-firmware is an upstream-derived MeshCore firmware project with a public backend-neutral integration contract.

## Before changing code

- Preserve native MeshCore behavior and offline independence.
- Prefer changes in the shared MECON runtime over role-specific duplication.
- Keep Companion/Repeater and hardware adapters thin.
- A new behaviour change needs a test that fails on stock 1.18.0 and passes here, and the mutation check (`sim/mutate.sh`) should show the test can fail. A new mutant goes in its topic's file under `sim/mutants/`, or a new numbered file for a new topic ([mutation check](docs/SIMULATION.md#mutation-check)).
- Do not introduce MeshContinuum-specific hosts, credentials, database concepts or private deployment names into firmware.
- Treat `docs/contract/` as the public device API.

## Build and test

Build and test inside the project container:

```sh
docker build -t mecon-firmware-dev .
sim/run.sh bin/idf5_libs.sh Heltec_v3_companion_radio_standalone   # Heltec V3/V4: board libraries, before the first build
sim/run.sh pio run -e Heltec_v3_companion_radio_standalone         # build a target board
sim/build.sh                                                       # build the simulated boards
sim/run.sh python3 -m pytest sim/tests -q                          # simulated-board suite
sim/run.sh pio test -e native                                      # native unit tests
```

[Architecture](docs/ARCHITECTURE.md#testing) lists every check. Installing the firmware on a device is in [install and set up a device](docs/GETTING_STARTED.md), and needs none of these tests.

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