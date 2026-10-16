#!/bin/sh
# Mutation check: break one thing per test group, rebuild, expect that group to go red, restore.
# usage: sim/mutate.sh   (run from the repo root; needs a clean tree, restores with git checkout)
set -u
cd "$(dirname "$0")/.."

build() { sim/build.sh > .sim-logs/mut_build.log 2>&1; grep -q "^exit=0" .sim-logs/mut_build.log || { echo "BUILD FAILED"; tail -8 .sim-logs/mut_build.log; }; }

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
mutate fault_hook_disabled variants/sim/sim_fs.cpp "if (g_sim.fault_after >= 0 && _p->fs_path == g_sim.fault_path) {" "if (false) {" sim/tests/test_atomic_storage.py::test_prefs_cut_in_temp_file_keeps_previous_prefs
# --- the stock-gap fixes: undo each fix and its test must go red
mutate transport_guard_removed examples/companion_radio/MyMesh.cpp "if (_serial && !_serial->isTrustedTransport() &&" "if (false && _serial && !_serial->isTrustedTransport() &&" sim/tests/test_companion_transport.py
mutate wifi_password_readable examples/companion_radio/MyMesh.cpp 'strcpy(reply, "> wifi.pwd is write-only");   // the password is never read back, on any transport' 'sprintf(reply, "> %s", _prefs.wifi_pwd);' sim/tests/test_companion_cli_secrets.py
mutate remote_cli_unrestricted examples/companion_radio/MyMesh.cpp "if (sender_timestamp != 0 && !isRemoteCommandAllowed(command)) {" "if (false && !isRemoteCommandAllowed(command)) {" sim/tests/test_companion_remote_cli.py
mutate prvkey_remote_allowed src/helpers/CommonCLI.cpp '} else if (sender_timestamp != 0 && memcmp(config, "prv.key", 7) == 0) {' '} else if (false && memcmp(config, "prv.key", 7) == 0) {' sim/tests/test_repeater_prvkey.py
mutate swap_not_recovered src/helpers/AtomicFile.h "  if (fs->exists(nw)) {
    if (!fs->exists(path)) fs->rename(nw, path);" "  if (false) {
    if (!fs->exists(path)) fs->rename(nw, path);" sim/tests/test_atomic_storage.py
mutate damaged_identity_deleted src/helpers/IdentityStore.cpp "_fs->rename(filename, damaged);" "_fs->remove(filename);" sim/tests/test_atomic_storage.py::test_damaged_identity_is_kept_aside_not_overwritten
mutate prefs_written_in_place src/helpers/CommonCLI.cpp 'return AtomicFile::save(fs, "/prefs.json", [this](Stream& s) { return _prefs->saveSerial(s); });' 'File f = fs->open("/prefs.json", "w", true); bool ok = f && _prefs->saveSerial(f); if (f) f.close(); return ok;' sim/tests/test_atomic_storage.py::test_prefs_cut_in_temp_file_keeps_previous_prefs
mutate trace_short_payload_accepted src/Mesh.cpp "pkt->path_len < MAX_PATH_SIZE && pkt->payload_len >= 9" "pkt->path_len < MAX_PATH_SIZE" sim/tests/test_malformed_input.py::test_truncated_trace_packets_do_not_crash_the_node
mutate radio_parse_unbounded src/helpers/CommonRadioPrefs.cpp "StrHelper::strncpy(tmp, &command[10], sizeof(tmp));" "strcpy(tmp, &command[10]);" sim/tests/test_malformed_input.py::test_overlong_radio_argument_on_repeater_console
build
echo "RESTORED; final clean run:"
sim/run.sh python3 -m pytest sim/tests -q -p no:cacheprovider 2>&1 | tail -3
