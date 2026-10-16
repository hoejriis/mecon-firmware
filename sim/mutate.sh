#!/bin/sh
# Mutation check: break one thing per test group, rebuild, expect that group to go red, restore.
# usage: sim/mutate.sh [name]   (run from the repo root; needs a clean tree, restores with git checkout)
#   name: run only that mutant. Exits 1 if any mutant SURVIVES (its tests still pass).
#   sim/mutate.sh --list prints the mutant names.
set -u
cd "$(dirname "$0")/.."
mkdir -p .sim-logs
ONLY="${1:-}"
SURVIVED=0
if [ "$ONLY" = "--list" ]; then grep -o "^mutate [a-z0-9_]*" "$0" | cut -d" " -f2; exit 0; fi

build() { sim/build.sh > .sim-logs/mut_build.log 2>&1; grep -q "^exit=0" .sim-logs/mut_build.log || { echo "BUILD FAILED"; tail -8 .sim-logs/mut_build.log; }; }

# name | file | old text | new text | pytest target
mutate() {
  name="$1"; file="$2"; old="$3"; new="$4"; target="$5"
  if [ -n "$ONLY" ] && [ "$ONLY" != "$name" ]; then return 0; fi
  python3 - "$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1:4]
s = open(p).read()
assert old in s, "mutation target not found: " + old
open(p, "w").write(s.replace(old, new, 1))
PY
  build
  case "$target" in
    unit:*) sim/run.sh pio test -e native -f "${target#unit:}" > ".sim-logs/mut_$name.log" 2>&1 ;;
    *) sim/run.sh python3 -m pytest $target -q -x -p no:cacheprovider > ".sim-logs/mut_$name.log" 2>&1 ;;
  esac
  rc=$?
  echo "MUTATION $name -> exit $rc : $(tail -1 ".sim-logs/mut_$name.log")"
  if [ $rc -eq 0 ]; then echo "SURVIVED: $name (its tests still pass with the code broken)"; SURVIVED=1; fi
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
# --- duty-cycle rules (these run the 1.25 virtual hour load test, about 2.5 minutes each)
DC_LOAD="sim/tests/test_duty_cycle.py::test_sustained_load_stays_within_the_regulatory_limit"
mutate dutycycle_window_is_a_bucket src/Dispatcher.cpp "      if (duty_sum + est > limit_ms) {
        uint32_t wait = dutyWaitMs(now, duty_sum + est - limit_ms, limit_ms);" "      static unsigned long bk_t = 0; static double bk_level = 0; static unsigned long bk_prev = 0;
      double bk_rate = (double)limit_ms / DUTY_WINDOW_MS;
      bk_level += (double)(total_air_time - bk_prev); bk_prev = total_air_time;
      bk_level -= (double)(now - bk_t) * bk_rate; if (bk_level < 0) bk_level = 0; bk_t = now;
      if (bk_level + est > limit_ms) {
        uint32_t wait = (uint32_t)((bk_level + est - limit_ms) / bk_rate) + 1;" $DC_LOAD
mutate dutycycle_floor_ignored src/Dispatcher.cpp "if (floor_bp < limit) limit = floor_bp;" "if (false) limit = floor_bp;" $DC_LOAD
mutate dutycycle_floor_clamped_like_the_cli src/Dispatcher.cpp "if (floor_bp < limit) limit = floor_bp;" "if (floor_bp < limit && floor_bp >= 1000) limit = floor_bp;" $DC_LOAD
mutate dutycycle_straddle_uses_the_looser_band src/helpers/RegulatoryDutyCycle.cpp "if (ov_hi - ov_lo > EDGE_TOLERANCE_KHZ && b.limit_bp < out.limit_bp) {" "if ((double)freq_mhz * 1000.0 >= b.lo_khz && (double)freq_mhz * 1000.0 < b.hi_khz && b.limit_bp < out.limit_bp) {" $DC_LOAD
mutate dutycycle_straddle_unit src/helpers/RegulatoryDutyCycle.cpp "if (ov_hi - ov_lo > EDGE_TOLERANCE_KHZ && b.limit_bp < out.limit_bp) {" "if ((double)freq_mhz * 1000.0 >= b.lo_khz && (double)freq_mhz * 1000.0 < b.hi_khz && b.limit_bp < out.limit_bp) {" unit:test_regulatory_duty_cycle
mutate dutycycle_radio_change_not_applied examples/companion_radio/MyMesh.cpp "_radio_freq = _prefs.freq; _radio_bw = _prefs.bw;   // the duty-cycle floor follows at once" "" sim/tests/test_duty_cycle.py::test_frequency_change_applies_without_reboot
mutate dutycycle_hour_not_seeded_at_boot src/Dispatcher.cpp "  if (duty_seeded) return;
  duty_seeded = true;" "  duty_seeded = true;
  return;" sim/tests/test_duty_cycle.py::test_reboots_do_not_reset_the_hour
mutate dutycycle_room_server_floor_missing examples/simple_room_server/MyMesh.h "    return radio_freq > 0;" "    return false;" sim/tests/test_duty_cycle.py::test_room_server_gets_the_floor_too
# --- standalone postures: undo each rule and its tests must go red (docs/STANDALONE_POSTURES.md)
mutate posture_damaged_value_fails_open src/helpers/Posture.h "inline bool isHardened(uint8_t posture) { return posture != NORMAL; }" "inline bool isHardened(uint8_t posture) { return posture == HARDENED; }" unit:test_posture
mutate posture_ble_default_pin_starts src/helpers/Posture.h "return !isHardened(posture) || (active_pin != 0 && active_pin != 123456);" "return true;" unit:test_posture
mutate posture_tcp_server_starts_unit src/helpers/Posture.h "inline bool tcpServerMayStart(uint8_t posture) { return !isHardened(posture); }" "inline bool tcpServerMayStart(uint8_t posture) { return true; }" unit:test_posture
mutate posture_wifi_started_in_hardened examples/companion_radio/main.cpp "if (wifi_enabled && wifi_ssid[0] && !Posture::tcpServerMayStart(posture)) {" "if (false) {" sim/tests/test_postures_companion.py::test_hardened_wifi_companion_does_not_serve_the_tcp_port
mutate posture_key_transfer_allowed_unit src/helpers/Posture.h "inline bool keyTransferAllowed(uint8_t posture) { return !isHardened(posture); }" "inline bool keyTransferAllowed(uint8_t posture) { return true; }" unit:test_posture
mutate posture_companion_keys_allowed examples/companion_radio/MyMesh.cpp "!Posture::keyTransferAllowed(_prefs.posture)) ||" "false) ||" sim/tests/test_postures_companion.py::test_hardened_refuses_key_export_and_import_on_usb
mutate posture_repeater_key_export_allowed src/helpers/CommonCLI.cpp "} else if (sender_timestamp == 0 && memcmp(config, \"prv.key\", 7) == 0 && !Posture::keyTransferAllowed(_prefs->posture)) {" "} else if (false) {" sim/tests/test_postures_admin.py::test_key_export_and_import_are_refused_in_hardened_even_on_the_console
mutate posture_repeater_key_import_allowed src/helpers/CommonCLI.cpp "} else if (memcmp(config, \"prv.key\", 7) == 0 && !Posture::keyTransferAllowed(_prefs->posture)) {" "} else if (false) {" sim/tests/test_postures_admin.py::test_key_export_and_import_are_refused_in_hardened_even_on_the_console
mutate posture_ble_factory_reset_allowed src/helpers/Posture.h "return !isHardened(posture) || caller == CALLER_CABLE;" "return true;" unit:test_posture
mutate posture_remote_admin_writes_allowed src/helpers/Posture.h "if (remote && isHardened(posture) && !isRemoteReadOnly(command)) {" "if (false) {" sim/tests/test_postures_admin.py::test_hardened_remote_admin_is_read_only_plus_reboot
mutate posture_remote_admin_list_unit src/helpers/Posture.h "strcmp(c, \"reboot\") == 0 ||
         strcmp(c, \"clock\") == 0" "strcmp(c, \"reboot\") == 0 || memcmp(c, \"set \", 4) == 0 ||
         strcmp(c, \"clock\") == 0" unit:test_posture
mutate posture_default_password_login_allowed examples/simple_repeater/MyMesh.cpp "const bool admin_open = !Posture::isDefaultAdminPassword(_prefs.password, ADMIN_PASSWORD);" "const bool admin_open = true;" sim/tests/test_postures_admin.py::test_stock_default_password_does_not_open_remote_admin
mutate posture_default_password_login_allowed_room examples/simple_room_server/MyMesh.cpp "const bool admin_open = !Posture::isDefaultAdminPassword(_prefs.password, ADMIN_PASSWORD);" "const bool admin_open = true;" sim/tests/test_postures_admin.py::test_stock_default_password_does_not_open_remote_admin
mutate posture_stored_admin_relogin_allowed examples/simple_repeater/MyMesh.cpp "} else if (client->isAdmin() && !admin_open) {" "} else if (false) {" sim/tests/test_postures_admin.py::test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session
mutate posture_default_password_commands_allowed src/helpers/Posture.h "if (remote && isDefaultAdminPassword(admin_pw, build_default)) {" "if (false) {" sim/tests/test_postures_admin.py::test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session
mutate posture_empty_password_counts_as_set src/helpers/Posture.h "return pw[0] == 0 || strcmp(pw, \"password\") == 0" "return strcmp(pw, \"password\") == 0" unit:test_posture
mutate posture_attempted_password_logged examples/simple_repeater/MyMesh.cpp "// never log or store the attempted password" "fprintf(stderr, \"bad login %s\\n\", (char *)data);" sim/tests/test_postures_admin.py::test_attempted_passwords_are_audited_but_never_kept
mutate posture_remote_can_lower src/helpers/Posture.h "bool allowed = to_hardened ? local : caller == CALLER_CABLE;" "bool allowed = true;" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate posture_ble_can_lower src/helpers/Posture.h "bool allowed = to_hardened ? local : caller == CALLER_CABLE;" "bool allowed = local;" unit:test_posture
mutate posture_lowering_without_confirm src/helpers/Posture.h "if (to_normal && strcmp(arg, \"normal confirm\") != 0) {" "if (false) {" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate posture_not_persisted_repeater src/helpers/CommonCLI.h "    def(\"posture\", posture);
" "" sim/tests/test_postures_admin.py::test_posture_and_audit_survive_a_reboot
mutate posture_not_persisted_companion examples/companion_radio/NodePrefs.h "    def(\"posture\", posture);
" "" sim/tests/test_postures_companion.py::test_hardened_survives_a_reboot
mutate posture_factory_reset_keeps_posture src/helpers/CommonCLI.cpp "_prefs->posture = Posture::NORMAL;" "" sim/tests/test_postures_admin.py::test_factory_reset_on_the_console_recovers_a_hardened_repeater
mutate posture_audit_not_recorded src/helpers/Posture.h "inline void recordRefusal(uint32_t ts, uint8_t caller, uint8_t what) { audit().add(ts, EV_REFUSED, caller, what); }" "inline void recordRefusal(uint32_t ts, uint8_t caller, uint8_t what) { }" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate posture_audit_readable_remotely src/helpers/Posture.h "    if (!local) {
      recordRefusal(now, caller, W_AUDIT);" "    if (false) {
      recordRefusal(now, caller, W_AUDIT);" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate posture_audit_not_folded src/helpers/AuditRing.h "int at = find(event, caller, what);
    AuditEntry rec" "int at = -1;
    AuditEntry rec" unit:test_posture
mutate posture_audit_flush_unbounded src/helpers/AuditRing.h "return _urgent || !_flushed_once || (uint32_t)(now_ms - _last_flush) >= (uint32_t)FLUSH_INTERVAL_MS;" "return true;" unit:test_posture
mutate posture_audit_crc_unchecked src/helpers/AuditRing.h "if (crc32(in, FILE_SIZE - 4) != want) return false;" "" unit:test_posture
mutate posture_remote_cli_stays_on_in_hardened examples/companion_radio/MyMesh.cpp "if (from.isRemoteCLIAllowed() && Posture::isHardened(_prefs.posture)) {" "if (false) {" sim/tests/test_postures_companion.py::test_hardened_turns_the_remote_cli_off
mutate posture_untrusted_refusal_not_audited examples/companion_radio/MyMesh.cpp "Posture::recordRefusal(now, Posture::CALLER_NET, auditWhatForCmd(cmd_frame[0]));" "" sim/tests/test_postures_companion.py::test_untrusted_transport_refusals_are_audited
mutate posture_companion_remote_lowering examples/companion_radio/MyMesh.cpp "const uint8_t caller = sender_timestamp != 0 ? Posture::CALLER_REMOTE_CLI : _cli_caller;" "const uint8_t caller = Posture::CALLER_CABLE;" sim/tests/test_postures_companion.py::test_remote_cli_cannot_change_the_posture_and_it_is_audited
build
echo "RESTORED; final clean run:"
if [ -z "$ONLY" ]; then build; sim/run.sh python3 -m pytest sim/tests -q -p no:cacheprovider 2>&1 | tail -3; fi

exit $SURVIVED
