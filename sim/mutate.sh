#!/bin/sh
# Mutation check: break one thing per test group, rebuild, expect that group to go red, restore.
# usage: sim/mutate.sh   (run from the repo root; needs a clean tree, restores with git checkout)
set -u
cd "$(dirname "$0")/.."

build() { sim/run.sh sh -c 'pio run -e sim_repeater && pio run -e sim_companion' > .sim-logs/mut_build.log 2>&1 || { echo "BUILD FAILED"; tail -5 .sim-logs/mut_build.log; }; }

# name | file | old text | new text | pytest target
mutate() {
  name="$1"; file="$2"; old="$3"; new="$4"; target="$5"
  python3 - "$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
assert old in s, "mutation target not found: " + old
open(p, "w").write(s.replace(old, new, 1))
PY
  build
  sim/run.sh python3 -m pytest $target -q -x -p no:cacheprovider > ".sim-logs/mut_$name.log" 2>&1
  echo "MUTATION $name -> pytest exit $? : $(tail -1 ".sim-logs/mut_$name.log")"
  git checkout -q -- "$file"
}

mutate toa_constant variants/sim/sim_toa.h "(sf == 5 || sf == 6) ? 6.25 : 4.25" "(sf == 5 || sf == 6) ? 6.25 : 4.0" sim/tests/test_toa.py
mutate radio_drops_all variants/sim/sim_radio.cpp "if (_i->rxq.empty() || _i->tx_active) return 0;" "return 0;" sim/tests/test_mesh.py
mutate fs_write_discarded variants/sim/sim_fs.cpp "size_t w = fwrite(buf, 1, n, _p->fp);" "size_t w = n;" sim/tests/test_repeater_cli.py
mutate fault_hook_disabled variants/sim/sim_fs.cpp "if (g_sim.fault_after >= 0 && _p->fs_path == g_sim.fault_path) {" "if (false) {" sim/tests/test_storage_fault.py
build
echo "RESTORED; final clean run:"
sim/run.sh python3 -m pytest sim/tests -q -p no:cacheprovider 2>&1 | tail -3
