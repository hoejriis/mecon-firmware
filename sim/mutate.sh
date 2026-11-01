#!/bin/sh
# Mutation check: break one thing per test group, rebuild, expect that group to go red, restore.
# usage: sim/mutate.sh [name]   (run from the repo root; needs a clean tree, restores with git checkout)
#   name: run only that mutant. Exits 1 if any mutant SURVIVES (its tests still pass).
#   sim/mutate.sh --list prints the mutant names.
#   sim/mutate.sh --files prints, per mutant, its name, its topic file, the file it mutates and its test targets
#   (tab-separated), for sim/mutate_select.py to pick the mutants a change touches.
set -u
cd "$(dirname "$0")/.."
mkdir -p .sim-logs
ONLY="${1:-}"
SURVIVED=0
MATCHED=0
DRY=0
if [ "$ONLY" = "--dry" ]; then DRY=1; ONLY=""; fi   # --dry: only check that every mutant's target text exists (no build, no tests)
FILES=0
if [ "$ONLY" = "--files" ]; then FILES=1; ONLY=""; fi
# Each mutant is undone with `git checkout`, which would also throw away uncommitted work: refuse a dirty tree.
if [ "$DRY" = 0 ] && [ "$FILES" = 0 ] && [ "$ONLY" != "--list" ]; then
  if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "REFUSED: uncommitted changes in tracked files; commit or stash them first (mutants are undone with git checkout)"; exit 2
  fi
fi
if [ "$ONLY" = "--list" ]; then cat sim/mutants/*.sh | grep -o "^mutate [a-z0-9_]*" | cut -d" " -f2; exit 0; fi

# --files only lists: the topic files that rebuild after their own mutants (on a full run) must not build here.
build() { [ "$FILES" = 1 ] && return 0; BUILD_OK=1; sim/build.sh > .sim-logs/mut_build.log 2>&1; grep -q "^exit=0" .sim-logs/mut_build.log || { BUILD_OK=0; echo "BUILD FAILED"; tail -8 .sim-logs/mut_build.log; }; }

# name | file | old text | new text | pytest target
mutate() {
  name="$1"; file="$2"; old="$3"; new="$4"; target="$5"
  if [ -n "$ONLY" ] && [ "$ONLY" != "$name" ]; then return 0; fi
  MATCHED=1
  if [ "$FILES" = 1 ]; then shift 4; printf '%s\t%s\t%s\t%s\n' "$name" "$TOPIC" "$file" "$*"; return 0; fi
  if [ "$DRY" = 1 ]; then
    python3 - "$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
if old not in s:
    print("TARGET MISSING: %s in %s" % (sys.argv[0], p)); sys.exit(1)
if old == new:
    print("NO-OP MUTANT in %s" % p); sys.exit(1)
if s.count(old) > 1:
    # mutate() replaces only the first occurrence, which may be the wrong one: make the target unique
    print("AMBIGUOUS TARGET (%d occurrences) in %s" % (s.count(old), p)); sys.exit(1)
PY
    if [ $? -ne 0 ]; then echo "  -> $name"; SURVIVED=1; fi
    return 0
  fi
  python3 - "$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
assert old in s, "mutation target not found: " + old
open(p, "w").write(s.replace(old, new, 1))
PY
  if [ $? -ne 0 ]; then echo "TARGET MISSING: $name (the code it mutates has changed; update the mutant)"; SURVIVED=1; return 0; fi
  build
  if [ "$BUILD_OK" = 0 ]; then
    # a mutant that does not compile proves nothing, and the tests would run the previous binaries: an error
    echo "ERROR: $name does not build (fix the mutant)"; SURVIVED=1
    git show "HEAD:$file" > "$file"; return 0
  fi
  case "$target" in
    unit:test_kiss_modem) sim/run.sh pio test -e native_kiss_modem -f test_kiss_modem > ".sim-logs/mut_$name.log" 2>&1 ;;   # its own env
    unit:*) sim/run.sh pio test -e native -f "${target#unit:}" > ".sim-logs/mut_$name.log" 2>&1 ;;
    *) sim/run.sh python3 -m pytest $target -q -x -p no:cacheprovider > ".sim-logs/mut_$name.log" 2>&1 ;;
  esac
  rc=$?
  echo "MUTATION $name -> exit $rc : $(tail -1 ".sim-logs/mut_$name.log")"
  # Only a failing test counts as caught. pytest exits 1 for failed tests and 2-5 for usage, collection or internal
  # errors; a missing tool is 126/127. The unit runner exits non-zero for a build error too, so it needs a failed case.
  verdict=error
  if [ $rc -eq 0 ]; then verdict=survived
  else case "$target" in
    unit:*) grep -Eq "test cases: [0-9]+ failed|[0-9]+ failed, [0-9]+ succeeded|\[FAILED\]" ".sim-logs/mut_$name.log" && verdict=caught ;;
    *) [ $rc -eq 1 ] && grep -Eq "[0-9]+ failed" ".sim-logs/mut_$name.log" && verdict=caught ;;
  esac; fi
  if [ "$verdict" = survived ]; then echo "SURVIVED: $name (its tests still pass with the code broken)"; SURVIVED=1; fi
  if [ "$verdict" = error ]; then echo "ERROR: $name (exit $rc without a failing test: the test run itself broke)"; SURVIVED=1; fi
  # restore from HEAD by content: `git checkout` does nothing for a same-size edit made within the index timestamp's second
  # (git calls the file unmodified), which left a protocol-level mutant in the tree on 2026-10-17
  git show "HEAD:$file" > "$file"
}

# The mutants live in sim/mutants/*.sh, one file per topic. They are sourced in file-name order into this shell, so a
# variable a file sets is seen by the files after it. A new topic gets its own file, numbered after the ones it uses.
for f in sim/mutants/*.sh; do TOPIC="$f"; . "./$f"; done
[ "$FILES" = 1 ] && exit 0

# --dry ends here, after every mutant above has had its target checked (it used to exit two thirds of the way down)
if [ "$DRY" = 1 ]; then [ "$SURVIVED" = 0 ] && echo "DRY CHECK: every mutant target exists"; exit $SURVIVED; fi
echo "RESTORED; final clean run:"
if [ -z "$ONLY" ] && [ "$DRY" = 0 ]; then build; sim/run.sh python3 -m pytest sim/tests -q -p no:cacheprovider 2>&1 | tail -3; fi
if [ -n "$ONLY" ] && [ "$DRY" = 0 ] && [ "$MATCHED" = 0 ]; then
  echo "NOT RUN: mutant '$ONLY' was listed but never reached (is it after the end of the list?)"; SURVIVED=1
fi
exit $SURVIVED
