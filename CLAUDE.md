# CLAUDE.md

Notes for working in this repository.

## Build and test

- Simulator: `sh sim/build.sh`, then `sh sim/run.sh python3 -m pytest sim/tests/<file>.py`.
- Native unit tests: `sh sim/run.sh pio test -e native -f <test_dir>`.
- ESP32 targets need their rebuilt IDF libraries first, per environment, and again after building another board:
  `sh sim/run.sh sh bin/idf5_libs.sh <env>`, then `sh sim/run.sh pio run -e <env>` ([PLATFORM](docs/PLATFORM.md)).
- Flash budgets live in `ci/size-budget.txt`. When a build goes over, raise it to the use plus 4 KB, rounded up to a
  whole KB, and say why in the commit.

## Mutation tests

- `sh sim/mutate.sh <name>` runs one mutant from `sim/mutants/*.sh`. It needs a clean, committed tree.
- Run `sh sim/mutate.sh --dry` before pushing: a change to a line a mutant targets fails CI's list check otherwise.
- Check why each mutant failed its test (`.sim-logs/mut_<name>.log`), not only that it did. Note an equivalent mutant
  in its file rather than running it.

## Docs

- A change to the device interface updates `docs/contract/` and its schemas in the same PR.
- The docs describe what is merged. Mark anything that is on `main` but not yet in a release.
