# CLAUDE.md

Working rules for every session on this laptop are in the global rulebook (`~/.claude/CLAUDE.md`, kept in
`hoejriis/github-backup` as `global-instructions/CLAUDE.md`). This file adds only what is specific to mecon-firmware.

## Build and test

- Simulator: `sh sim/build.sh`, then `sh sim/run.sh python3 -m pytest sim/tests/<file>.py`. The full suites run on CI.
- Native unit tests: `sh sim/run.sh pio test -e native -f <test_dir>`.
- ESP32 targets need their rebuilt IDF libraries first, per environment, and again after building another board:
  `sh sim/run.sh sh bin/idf5_libs.sh <env>`, then `sh sim/run.sh pio run -e <env>`.
- Flash budgets live in `ci/size-budget.txt`. When a build goes over, raise it to the use plus 4 KB, rounded up to a
  whole KB, and say why in the commit.

## Mutants

- `sh sim/mutate.sh <name>` runs one mutant from `sim/mutants/*.sh`. It needs a clean, committed tree.
- Run `sh sim/mutate.sh --dry` before pushing: a change to a line a mutant targets fails CI's list check otherwise.
- Check why a mutant was killed (`.sim-logs/mut_<name>.log`), not only that it was. Note an equivalent mutant in its
  file rather than running it.

## Bench

- The test boards are on the MECON Pi (`ssh mecon`) as `/dev/bench/<board>`. Claim a port in the register before use:
  `bench_port.py` in github-backup's `global-instructions/hooks`. Prefix any command that names a port with
  `BENCH_SESSION=<your session>`.
- Bench tools are in `~/b1105` on the Pi. Flash dumps, exports and saved configs are secrets: mode 600, shredded after use.

## Docs

- `docs/contract/` and its schemas, `docs/AVAILABILITY.md`, `docs/HARDWARE_VERIFIED.md`: kept by the firmware coder, in
  the same PR as the change.
- README and the other user and integrator pages under `docs/`: kept by the mecon-docs session.
- The docs describe what is merged. Mark anything that is on `main` but not yet released.

## Releases

Release keys and their use are described in `docs/RELEASE_KEYS.md`. The keys themselves never enter this repo, a log
or a transcript.
