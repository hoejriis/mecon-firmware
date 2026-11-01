#!/usr/bin/env python3
"""Pick the mutants the mutation workflow runs (owner's rule, 2026-11-01: the full set nightly, a spot check otherwise).

    sim/mutate_select.py full
    sim/mutate_select.py spot <file listing the changed paths, one per line>

Prints mutant names, one per line. `full` is every mutant. `spot` is every mutant that a changed path touches:
- the file it mutates changed;
- its topic file (sim/mutants/NNN_topic.sh) changed;
- a test it names changed (sim/tests/test_x.py, or test/test_x/ for unit:test_x).

When the mutation machinery itself changed (the paths in SMOKE_TRIGGERS), the spot set also takes a smoke set: the first
mutant of every topic file, so a broken driver or harness shows on the PR rather than in the nightly."""
import subprocess
import sys

SMOKE_TRIGGERS = ("sim/mutate.sh", "sim/mutate_select.py", "sim/build.sh", "sim/run.sh", "sim/harness",
                  "sim/tests/conftest.py", "variants/sim/", "platformio.ini", ".github/workflows/mutation.yml")


def mutants():
    out = subprocess.run(["sh", "sim/mutate.sh", "--files"], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        name, topic, path, targets = (line.split("\t") + [""])[:4]
        yield name, topic, path, targets.split()


def tests_of(targets):
    for t in targets:
        if t.startswith("unit:"):
            yield "test/" + t[len("unit:"):] + "/"
        else:
            yield t.split("::")[0]


def touched(changed, path):
    return any(c == path or (path.endswith("/") and c.startswith(path)) for c in changed)


def main():
    mode = sys.argv[1]
    rows = list(mutants())
    if mode == "full":
        names = [r[0] for r in rows]
    else:
        changed = [l.strip() for l in open(sys.argv[2]) if l.strip()]
        names, seen_topics = [], set()
        smoke = any(c.startswith(SMOKE_TRIGGERS) for c in changed)
        for name, topic, path, targets in rows:
            first_of_topic = topic not in seen_topics
            seen_topics.add(topic)
            if (touched(changed, path) or touched(changed, topic) or any(touched(changed, t) for t in tests_of(targets))
                    or (smoke and first_of_topic)):
                names.append(name)
    print("\n".join(names))


if __name__ == "__main__":
    main()
