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
MATCHED=0
DRY=0
if [ "$ONLY" = "--dry" ]; then DRY=1; ONLY=""; fi   # --dry: only check that every mutant's target text exists (no build, no tests)
# Each mutant is undone with `git checkout`, which would also throw away uncommitted work: refuse a dirty tree.
if [ "$DRY" = 0 ] && [ "$ONLY" != "--list" ]; then
  if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "REFUSED: uncommitted changes in tracked files; commit or stash them first (mutants are undone with git checkout)"; exit 2
  fi
fi
if [ "$ONLY" = "--list" ]; then grep -o "^mutate [a-z0-9_]*" "$0" | cut -d" " -f2; exit 0; fi

build() { BUILD_OK=1; sim/build.sh > .sim-logs/mut_build.log 2>&1; grep -q "^exit=0" .sim-logs/mut_build.log || { BUILD_OK=0; echo "BUILD FAILED"; tail -8 .sim-logs/mut_build.log; }; }

# name | file | old text | new text | pytest target
mutate() {
  name="$1"; file="$2"; old="$3"; new="$4"; target="$5"
  if [ -n "$ONLY" ] && [ "$ONLY" != "$name" ]; then return 0; fi
  MATCHED=1
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

mutate toa_constant variants/sim/sim_toa.h "(sf == 5 || sf == 6) ? 6.25 : 4.25" "(sf == 5 || sf == 6) ? 6.25 : 4.0" sim/tests/test_toa.py
mutate radio_drops_all variants/sim/sim_radio.cpp "if (_i->rxq.empty() || _i->tx_active) return 0;" "return 0;" sim/tests/test_mesh.py
mutate fs_write_discarded variants/sim/sim_fs.cpp "size_t w = fwrite(buf, 1, n, _p->fp);" "size_t w = n;" sim/tests/test_repeater_cli.py
mutate fault_hook_disabled variants/sim/sim_fs.cpp "if (g_sim.fault_after >= 0 && _p->fs_path == g_sim.fault_path) {" "if (false) {" sim/tests/test_atomic_storage.py::test_prefs_cut_in_temp_file_keeps_previous_prefs
# --- the stock-gap fixes: undo each fix and its test must go red
mutate swap_not_recovered src/helpers/AtomicFile.h "  if (BootPresence::mayExist(nw) && fs->exists(nw)) {" "  if (false) {" sim/tests/test_atomic_storage.py
mutate damaged_identity_deleted src/helpers/IdentityStore.cpp "_fs->rename(filename, damaged);" "_fs->remove(filename);" sim/tests/test_atomic_storage.py::test_damaged_identity_is_kept_aside_not_overwritten
mutate prefs_written_in_place src/helpers/CommonCLI.cpp 'return AtomicFile::save(fs, "/prefs.json", [this](Stream& s) { return _prefs->saveSerial(s); });' 'File f = fs->open("/prefs.json", "w", true); bool ok = f && _prefs->saveSerial(f); if (f) f.close(); return ok;' sim/tests/test_atomic_storage.py::test_prefs_cut_in_temp_file_keeps_previous_prefs
mutate trace_short_payload_accepted src/Mesh.cpp "pkt->path_len < MAX_PATH_SIZE && pkt->payload_len >= 9" "pkt->path_len < MAX_PATH_SIZE" sim/tests/test_malformed_input.py::test_truncated_trace_packets_do_not_crash_the_node
mutate radio_parse_unbounded src/helpers/CommonRadioPrefs.cpp "StrHelper::strncpy(tmp, &command[10], sizeof(tmp));" "strcpy(tmp, &command[10]);" sim/tests/test_malformed_input.py::test_overlong_radio_argument_on_repeater_console
# --- duty-cycle rules (these run the 1.25 virtual hour load test, about 2.5 minutes each)
DC_LOAD="sim/tests/test_duty_cycle.py::test_sustained_load_stays_within_the_regulatory_limit"
mutate dutycycle_window_is_a_bucket src/Dispatcher.cpp "      if (duty.sum() + est > limit_ms) {
        uint32_t wait = duty.waitMs(now, duty.sum() + est - limit_ms);" "      static unsigned long bk_t = 0; static double bk_level = 0; static unsigned long bk_prev = 0;
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
mutate dutycycle_hour_not_seeded_at_boot src/helpers/DutyWindow.h "    if (is_seeded) return;
    is_seeded = true;" "    is_seeded = true;
    return;" sim/tests/test_duty_cycle.py::test_reboots_do_not_reset_the_hour
mutate dutycycle_room_server_floor_missing examples/simple_room_server/MyMesh.h "    return radio_freq > 0;" "    return false;" sim/tests/test_duty_cycle.py::test_room_server_gets_the_floor_too
# --- standalone postures: undo each rule and its tests must go red (docs/STANDALONE_POSTURES.md)
mutate posture_damaged_value_fails_open src/helpers/Posture.h "inline bool isHardened(uint8_t posture) { return posture != NORMAL; }" "inline bool isHardened(uint8_t posture) { return posture == HARDENED; }" unit:test_posture
mutate posture_ble_default_pin_starts src/helpers/Posture.h "return !isHardened(posture) || (active_pin != 0 && active_pin != 123456);" "return true;" unit:test_posture
mutate posture_tcp_server_starts_unit src/helpers/Posture.h "inline bool tcpServerMayStart(uint8_t posture) { return !isHardened(posture); }" "inline bool tcpServerMayStart(uint8_t posture) { return true; }" unit:test_posture
mutate posture_default_password_login_allowed examples/simple_repeater/MyMesh.cpp "const bool admin_open = !Posture::isDefaultAdminPassword(_prefs.password, ADMIN_PASSWORD);" "const bool admin_open = true;" sim/tests/test_postures_admin.py::test_stock_default_password_does_not_open_remote_admin
mutate posture_default_password_login_allowed_room examples/simple_room_server/MyMesh.cpp "const bool admin_open = !Posture::isDefaultAdminPassword(_prefs.password, ADMIN_PASSWORD);" "const bool admin_open = true;" sim/tests/test_postures_admin.py::test_stock_default_password_does_not_open_remote_admin
mutate posture_stored_admin_relogin_allowed examples/simple_repeater/MyMesh.cpp "} else if (client->isAdmin() && !admin_open) {" "} else if (false) {" sim/tests/test_postures_admin.py::test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session
mutate posture_empty_password_counts_as_set src/helpers/Posture.h "return pw[0] == 0 || strcmp(pw, \"password\") == 0" "return strcmp(pw, \"password\") == 0" unit:test_posture
mutate posture_attempted_password_logged examples/simple_repeater/MyMesh.cpp "// never log or store the attempted password" "fprintf(stderr, \"bad login %s\\n\", (char *)data);" sim/tests/test_postures_admin.py::test_attempted_passwords_are_audited_but_never_kept
mutate posture_lowering_without_confirm src/helpers/Posture.h "if (to_normal && strcmp(arg, \"normal confirm\") != 0) {" "if (false) {" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate posture_not_persisted_repeater src/helpers/CommonCLI.h "    def(\"posture\", posture);
" "" sim/tests/test_postures_admin.py::test_posture_and_audit_survive_a_reboot
mutate posture_not_persisted_companion examples/companion_radio/NodePrefs.h "    def(\"posture\", posture);
" "" sim/tests/test_postures_companion.py::test_hardened_survives_a_reboot
mutate posture_factory_reset_keeps_posture src/helpers/CommonCLI.cpp "_prefs->posture = Posture::NORMAL;" "" sim/tests/test_postures_admin.py::test_factory_reset_on_the_console_recovers_a_hardened_repeater
mutate posture_audit_not_folded src/helpers/AuditRing.h "int at = find(event, caller, what);
    AuditEntry rec" "int at = -1;
    AuditEntry rec" unit:test_posture
mutate posture_audit_flush_unbounded src/helpers/AuditRing.h "return _urgent || !_flushed_once || (uint32_t)(now_ms - _last_flush) >= (uint32_t)FLUSH_INTERVAL_MS;" "return true;" unit:test_posture
mutate posture_audit_crc_unchecked src/helpers/AuditRing.h "if (crc32(in, FILE_SIZE - 4) != want) return false;" "" unit:test_posture
# --- Wi-Fi controller rules (docs/WIFI.md): undo each rule and its tests must go red
WIFI_CORE=src/helpers/wifi/WifiCore.cpp
WIFI_SIM=sim/tests/test_companion_wifi.py
mutate wifi_priority_ignored $WIFI_CORE "if (pick == NONE || _profiles->priority(i) > _profiles->priority(pick)) pick = i;" "if (pick == NONE) pick = i;" $WIFI_SIM::test_joins_the_highest_priority_visible_profile
mutate wifi_priority_ignored_unit $WIFI_CORE "if (pick == NONE || _profiles->priority(i) > _profiles->priority(pick)) pick = i;" "if (pick == NONE) pick = i;" unit:test_wifi_core
mutate wifi_tie_goes_to_the_higher_number $WIFI_CORE "if (pick == NONE || _profiles->priority(i) > _profiles->priority(pick)) pick = i;" "if (pick == NONE || _profiles->priority(i) >= _profiles->priority(pick)) pick = i;" unit:test_wifi_core
mutate wifi_auth_failure_not_cooled $WIFI_CORE "      startCooldown(i, now, ms);
      break;
    }
    case WifiFail::NotFound:" "      break;
    }
    case WifiFail::NotFound:" $WIFI_SIM::test_fails_over_on_a_wrong_password
mutate wifi_auth_cooldown_not_escalating $WIFI_CORE "for (uint8_t k = 0; k < p.auth_fails && ms < _t.auth_cooldown_max_ms; k++) ms *= 2;" "" unit:test_wifi_core
mutate wifi_notfound_retried_at_once $WIFI_CORE "      p.vis = false;   // it is not there; the next scan decides whether it is back" "" unit:test_wifi_core
mutate wifi_join_timeout_ignored $WIFI_CORE "      if ((uint32_t)(now - _state_since) >= _t.connect_timeout_ms) {" "      if (false) {" unit:test_wifi_core
mutate wifi_quick_drops_not_counted $WIFI_CORE "else if (++_p[was].quick_drops >= _t.max_quick_drops) failProfile" "else if (false) failProfile" unit:test_wifi_core
mutate wifi_stable_link_keeps_drop_count $WIFI_CORE "    if (!quick) _p[was].quick_drops = 0;" "    if (!quick) { }" unit:test_wifi_core
mutate wifi_drop_not_failed_over $WIFI_CORE "  } else if (_state == WifiState::Connected) {
    uint8_t was = _cur;" "  } else if (false) {
    uint8_t was = _cur;" $WIFI_SIM::test_fails_over_when_the_network_disappears
mutate wifi_no_hold_down $WIFI_CORE "    if (require_hold_down && (uint32_t)(now - _p[i].vis_since) < _t.hold_down_ms) continue;" "" $WIFI_SIM::test_returns_to_the_better_network_after_the_hold_down_and_only_once
mutate wifi_no_hold_down_unit $WIFI_CORE "    if (require_hold_down && (uint32_t)(now - _p[i].vis_since) < _t.hold_down_ms) continue;" "" unit:test_wifi_core
mutate wifi_hold_down_survives_a_miss $WIFI_CORE "      if (!p.vis) p.vis_since = now;   // a new run of sightings begins" "      if (p.vis_since == 0) p.vis_since = now;" unit:test_wifi_core
mutate wifi_scans_while_on_the_best $WIFI_CORE "    if (i != _cur && _profiles->configured(i) && _profiles->priority(i) > _profiles->priority(_cur) && !cooling(i, now))
      return true;" "    if (i != _cur) return true;" unit:test_wifi_core
mutate wifi_backoff_not_growing $WIFI_CORE "_backoff_ms = _backoff_ms == 0 ? _t.backoff_min_ms : (_backoff_ms * 2 > _t.backoff_max_ms ? _t.backoff_max_ms : _backoff_ms * 2);" "_backoff_ms = _t.backoff_min_ms;" unit:test_wifi_core
mutate wifi_backoff_not_reset $WIFI_CORE "  _backoff_ms = 0;
  _connect_count++;" "  _connect_count++;" unit:test_wifi_core
mutate wifi_edit_keeps_the_link $WIFI_CORE "  if (on_it && credentials) {" "  if (false) {" $WIFI_SIM::test_editing_the_profile_in_use_applies_without_a_reboot
mutate wifi_edit_keeps_the_cooldown $WIFI_CORE "  p.cooling = false;
  p.auth_fails = 0;" "  p.auth_fails = 0;" unit:test_wifi_core
mutate wifi_own_leave_counted_as_failure src/helpers/wifi/WifiController.cpp "if (_ev_reason != REASON_ASSOC_LEAVE) _core.onDisconnected" "_core.onDisconnected" $WIFI_SIM::test_returns_to_the_better_network_after_the_hold_down_and_only_once
mutate wifi_auth_reason_unmapped src/helpers/wifi/WifiController.cpp "if (r == REASON_AUTH_FAIL || r == REASON_4WAY_HANDSHAKE_TIMEOUT || r == REASON_HANDSHAKE_TIMEOUT) return WifiDrop::AuthFailed;" "" $WIFI_SIM::test_fails_over_on_a_wrong_password
mutate wifi_stock_ssid_key_changed examples/companion_radio/NodePrefs.h 'def("ssid", w.ssid_[0], sizeof(w.ssid_[0]));   // profile 0 keeps the stock keys' 'def("ssid0", w.ssid_[0], sizeof(w.ssid_[0]));' $WIFI_SIM::test_a_stock_format_prefs_file_loads
mutate wifi_stock_ssid_key_changed_unit examples/companion_radio/NodePrefs.h 'def("ssid", w.ssid_[0], sizeof(w.ssid_[0]));   // profile 0 keeps the stock keys' 'def("ssid0", w.ssid_[0], sizeof(w.ssid_[0]));' unit:test_wifi_prefs
mutate wifi_stock_set_ssid_misrouted examples/companion_radio/MyMesh.cpp "StrHelper::strncpy(_prefs.wifi_profiles.ssid_[0], &command[14]" "StrHelper::strncpy(_prefs.wifi_profiles.ssid_[1], &command[14]" $WIFI_SIM::test_stock_wifi_commands_still_work
mutate wifi_priorities_not_persisted examples/companion_radio/NodePrefs.h "        def(key, w.prio_[i]);
" "" $WIFI_SIM::test_ten_profiles_persist_across_a_reboot
mutate wifi_profiles_beyond_three_not_persisted examples/companion_radio/NodePrefs.h "      for (uint8_t i = 0; i < WifiProfiles::COUNT; i++) {
        char key[4]" "      for (uint8_t i = 0; i < 3; i++) {
        char key[4]" $WIFI_SIM::test_ten_profiles_persist_across_a_reboot
mutate wifi_passwords_not_persisted_unit examples/companion_radio/NodePrefs.h "        key[0] = 'w';
        def(key, w.pwd_[i], sizeof(w.pwd_[i]));" "" unit:test_wifi_prefs
mutate wifi_password_in_listing src/helpers/wifi/WifiProfiles.cpp 'pr.hasPwd(i) ? "set" : "unset"' 'pr.hasPwd(i) ? pr.pwd(i) : "unset"' unit:test_wifi_prefs
mutate wifi_password_in_listing_sim src/helpers/wifi/WifiProfiles.cpp 'pr.hasPwd(i) ? "set" : "unset"' 'pr.hasPwd(i) ? pr.pwd(i) : "unset"' $WIFI_SIM::test_passwords_are_nowhere_but_the_prefs_file
mutate wifi_password_logged src/helpers/wifi/WifiController.cpp '  WIFICTL_LOG("joining profile %u (%s)", (unsigned)i, _profiles->ssid(i));' '  fprintf(stderr, "joining with %s\n", _profiles->pwd(i));' $WIFI_SIM::test_passwords_are_nowhere_but_the_prefs_file
mutate wifi_tcp_served_without_a_link variants/sim/sim_wifi_if.cpp "return WiFi.status() == WL_CONNECTED ? inner().checkRecvFrame(dest) : 0; }" "return inner().checkRecvFrame(dest); }" $WIFI_SIM::test_the_tcp_companion_server_serves_only_while_the_controller_has_a_link
mutate wifi_started_in_hardened examples/companion_radio/main.cpp "if (wifi_enabled && wifi_has_profile && !tcp_may_start && !mqtt_may_run) {" "if (false) {" $WIFI_SIM::test_hardened_starts_no_wifi_at_all
# --- MQTT controller rules (docs/MQTT.md): undo each rule and its tests must go red
MQ_CORE=src/helpers/mqtt/MqttCore.cpp
MQ_CTRL=src/helpers/mqtt/MqttController.cpp
MQ_PROF_H=src/helpers/mqtt/MqttProfiles.h
MQ_PROF_C=src/helpers/mqtt/MqttProfiles.cpp
MQ_NET=variants/sim/sim_mqtt_net.cpp
MQ_SIM=sim/tests/test_companion_mqtt.py
mutate mqtt_priority_ignored $MQ_CORE "if (best == NONE || better(i, best)) { best = i; best_cand = a == STATIC_OK ? NONE : a; }" "if (best == NONE) { best = i; best_cand = a == STATIC_OK ? NONE : a; }" unit:test_mqtt_core
mutate mqtt_priority_ignored_sim $MQ_CORE "if (best == NONE || better(i, best)) { best = i; best_cand = a == STATIC_OK ? NONE : a; }" "if (best == NONE) { best = i; best_cand = a == STATIC_OK ? NONE : a; }" $MQ_SIM::test_connects_to_the_highest_priority_broker_and_failover_follows
mutate mqtt_tie_goes_to_the_higher_number $MQ_CORE "return pa > pb || (pa == pb && a < b);" "return pa > pb || (pa == pb && a > b);" unit:test_mqtt_core
mutate mqtt_failed_connect_not_cooled $MQ_CORE "p.cool_until = now + escalate(_t.fail_cooldown_ms, _t.fail_cooldown_max_ms, p.fails);" "p.cool_until = now;" unit:test_mqtt_core
mutate mqtt_cooldown_not_escalating $MQ_CORE "for (uint8_t k = 1; k < fails && ms < max; k++) ms *= 2;" "" unit:test_mqtt_core
mutate mqtt_auth_failure_not_cooled $MQ_CORE "p.cool_until = now + escalate(_t.auth_cooldown_ms, _t.auth_cooldown_max_ms, p.fails);" "p.cool_until = now;" unit:test_mqtt_core
mutate mqtt_dropped_session_not_failed_over $MQ_CTRL "if (!_psc.loop()) {" "if (false) {" $MQ_SIM::test_connects_to_the_highest_priority_broker_and_failover_follows
mutate mqtt_session_age_ignored $MQ_CORE "if (hold && (uint32_t)(now - _connected_at) < _t.hold_down_ms) return NONE;" "" unit:test_mqtt_core
mutate mqtt_session_age_ignored_sim $MQ_CORE "if (hold && (uint32_t)(now - _connected_at) < _t.hold_down_ms) return NONE;" "" $MQ_SIM::test_returns_to_the_better_broker_after_the_hold_down_without_flapping
mutate mqtt_responder_hold_down_ignored $MQ_CORE "if (hold && (uint32_t)(now - c.vis_since) < _t.hold_down_ms) continue;" "" unit:test_mqtt_core
mutate mqtt_responder_hold_down_ignored_sim $MQ_CORE "if (hold && (uint32_t)(now - c.vis_since) < _t.hold_down_ms) continue;" "" $MQ_SIM::test_a_responder_that_appears_later_is_taken_after_the_hold_down
mutate mqtt_browse_on_the_best_profile $MQ_CORE "if (better(i, ref)) return true;" "return true;" unit:test_mqtt_core
mutate mqtt_browse_on_the_best_profile_sim $MQ_CORE "if (better(i, ref)) return true;" "return true;" $MQ_SIM::test_discovery_browses_only_while_not_on_the_best_profile
mutate mqtt_plain_responder_accepted $MQ_CORE "    if (!secure) {" "    if (false) {" unit:test_mqtt_core
mutate mqtt_plain_responder_accepted_sim $MQ_CORE "    if (!secure) {" "    if (false) {" $MQ_SIM::test_a_discovered_broker_is_never_used_over_plain_mqtt
mutate mqtt_site_not_compared $MQ_CORE "if (strcmp(q.site, site) != 0) continue;" "" unit:test_mqtt_core
mutate mqtt_site_not_compared_sim $MQ_CORE "if (strcmp(q.site, site) != 0) continue;" "" $MQ_SIM::test_discovery_ignores_a_responder_with_another_site_id
mutate mqtt_rogue_cools_the_whole_profile $MQ_CORE "    // The certificate is not the anchor: that one broker is wrong, not the whole profile.
    if (cand) {" "    // The certificate is not the anchor: that one broker is wrong, not the whole profile.
    if (false) {" unit:test_mqtt_core
mutate mqtt_rogue_cools_the_whole_profile_sim $MQ_CORE "    // The certificate is not the anchor: that one broker is wrong, not the whole profile.
    if (cand) {" "    // The certificate is not the anchor: that one broker is wrong, not the whole profile.
    if (false) {" $MQ_SIM::test_a_rogue_does_not_shut_out_the_real_broker_that_carries_the_same_site_id
mutate mqtt_rogue_not_cooled $MQ_CORE "c.cool_until = now + escalate(_t.pin_cooldown_ms, _t.pin_cooldown_max_ms, c.fails);" "c.cool_until = now;" unit:test_mqtt_core
mutate mqtt_lost_responder_not_failed_over $MQ_CORE "      if (in_use) {" "      if (false) {" unit:test_mqtt_core
mutate mqtt_edit_keeps_the_cooldown $MQ_CORE "  p.cooling = false;
  p.fails = 0;
  p.skip_hold = true;" "  p.fails = 0;
  p.skip_hold = true;" unit:test_mqtt_core
mutate mqtt_edit_keeps_the_cooldown_sim $MQ_CORE "  p.cooling = false;
  p.fails = 0;
  p.skip_hold = true;" "  p.fails = 0;
  p.skip_hold = true;" $MQ_SIM::test_credentials_are_used_and_a_wrong_password_fails_over
mutate mqtt_edit_keeps_the_session $MQ_CORE "if ((_state == MqttState::Connected || _state == MqttState::Connecting) && _cur == index && credentials) {" "if (false) {" unit:test_mqtt_core
mutate mqtt_edit_waits_for_the_hold_down $MQ_CORE "  p.skip_hold = true;
  dropCands(index);" "  dropCands(index);" unit:test_mqtt_core
mutate mqtt_posture_change_keeps_plain_session $MQ_CORE "    if (!usable(_cur)) {" "    if (false) {" unit:test_mqtt_core
mutate mqtt_posture_change_keeps_plain_session_sim $MQ_CORE "    if (!usable(_cur)) {" "    if (false) {" $MQ_SIM::test_raising_the_posture_ends_a_plain_session
mutate mqtt_posture_not_passed_to_the_core $MQ_CTRL "  _core.setPosture(*_posture);
  _core.setLink(now, _net->linkUp());" "  _core.setLink(now, _net->linkUp());" $MQ_SIM::test_raising_the_posture_ends_a_plain_session
mutate mqtt_hardened_allows_plain $MQ_PROF_H "return q.kind != MqttProfile::Static || q.tls != 0 || !Posture::isHardened(posture);" "return true;" unit:test_mqtt_core
mutate mqtt_hardened_allows_plain_sim $MQ_PROF_H "return q.kind != MqttProfile::Static || q.tls != 0 || !Posture::isHardened(posture);" "return true;" $MQ_SIM::test_hardened_never_connects_a_stored_plain_profile
mutate mqtt_hardened_set_allows_plain $MQ_PROF_C "if (Posture::isHardened(posture))
          snprintf" "if (false)
          snprintf" unit:test_mqtt_prefs
mutate mqtt_hardened_set_allows_plain_sim $MQ_PROF_C "if (Posture::isHardened(posture))
          snprintf" "if (false)
          snprintf" $MQ_SIM::test_hardened_refuses_plain_mqtt_at_the_command
mutate mqtt_discovery_may_be_plain $MQ_PROF_H "bool useTls() const { return kind == Discovery || tls != 0; }" "bool useTls() const { return tls != 0; }" $MQ_SIM::test_a_discovery_profile_is_tls_even_if_the_slot_was_plain_before
mutate mqtt_discovery_may_be_plain_unit $MQ_PROF_H "bool useTls() const { return kind == Discovery || tls != 0; }" "bool useTls() const { return tls != 0; }" unit:test_mqtt_prefs
mutate mqtt_wifi_not_kept_for_mqtt_in_hardened examples/companion_radio/main.cpp "if (wifi_enabled && wifi_has_profile && !tcp_may_start && !mqtt_may_run) {" "if (wifi_enabled && wifi_has_profile && !tcp_may_start) {" $MQ_SIM::test_hardened_never_connects_a_stored_plain_profile
mutate mqtt_tcp_port_served_in_hardened examples/companion_radio/main.cpp "    if (tcp_may_start) {
      wifi_interface.begin(TCP_PORT);" "    if (true) {
      wifi_interface.begin(TCP_PORT);" $MQ_SIM::test_hardened_keeps_the_link_for_mqtt_but_never_serves_the_tcp_port
mutate mqtt_pin_not_checked $MQ_NET "if (strcasecmp(hex, anchor) != 0) {" "if (false) {" $MQ_SIM::test_discovery_rejects_the_right_site_id_with_the_wrong_certificate
mutate mqtt_anchor_not_passed_on $MQ_CTRL "j.has_anchor = j.tls && q.hasAnchor();" "j.has_anchor = false;" $MQ_SIM::test_discovery_rejects_the_right_site_id_with_the_wrong_certificate
mutate mqtt_tls_dropped $MQ_CTRL "j.tls = q.useTls();" "j.tls = false;" $MQ_SIM::test_discovery_accepts_a_matching_site_id_with_a_matching_certificate
mutate mqtt_unverified_reported_as_verified $MQ_CTRL "_core.onConnected(now, j.tls && j.has_anchor);" "_core.onConnected(now, j.tls);" $MQ_SIM::test_a_tls_profile_without_an_anchor_is_encrypted_but_reported_unverified_and_a_wrong_anchor_is_refused
mutate mqtt_name_resolved_once $MQ_CTRL "  if (j.resolve && !_net->resolve(j.host, j.addr)) { j.result = ConnectJob::Dns; return; }" "  static uint32_t cached[MqttProfiles::COUNT];
  if (j.resolve && cached[_job_profile]) j.addr = cached[_job_profile];
  else if (j.resolve && !_net->resolve(j.host, j.addr)) { j.result = ConnectJob::Dns; return; }
  else if (j.resolve) cached[_job_profile] = j.addr;" $MQ_SIM::test_a_host_name_is_resolved_again_on_every_attempt
mutate mqtt_password_in_listing $MQ_PROF_C 'const char* pwd = q.pwd[0] ? "set" : "unset";' 'const char* pwd = q.pwd[0] ? q.pwd : "unset";' unit:test_mqtt_prefs
mutate mqtt_password_in_listing_sim $MQ_PROF_C 'const char* pwd = q.pwd[0] ? "set" : "unset";' 'const char* pwd = q.pwd[0] ? q.pwd : "unset";' $MQ_SIM::test_credentials_are_nowhere_but_the_prefs_file
mutate mqtt_password_logged $MQ_CTRL "  _psc.setClient(_net->client());
  const char* user = j.user[0]" '  fprintf(stderr, "mqtt connect as %s\n", j.pwd);
  _psc.setClient(_net->client());
  const char* user = j.user[0]' $MQ_SIM::test_credentials_are_nowhere_but_the_prefs_file
mutate mqtt_credentials_not_sent $MQ_CTRL "const char* user = j.user[0] ? j.user : nullptr;" "const char* user = nullptr;" $MQ_SIM::test_credentials_are_used_and_a_wrong_password_fails_over
mutate mqtt_profiles_not_persisted examples/companion_radio/NodePrefs.h "        key[0] = 'h'; def(key, q.host, sizeof(q.host));
" "" unit:test_mqtt_prefs
mutate mqtt_profiles_not_persisted_sim examples/companion_radio/NodePrefs.h "        key[0] = 'h'; def(key, q.host, sizeof(q.host));
" "" $MQ_SIM::test_profiles_persist_across_a_reboot_and_can_be_disabled
mutate mqtt_status_not_published $MQ_CTRL "  _psc.publish(_topic, payload, true);" "  (void)payload;" $MQ_SIM::test_status_topic_is_published_and_retained
mutate mqtt_disable_ignored examples/companion_radio/MyMesh.cpp "    mqttController().setEnabled(_prefs.mqtt_enabled != 0);
" "" $MQ_SIM::test_profiles_persist_across_a_reboot_and_can_be_disabled
# --- the connect runs on a worker (docs/MQTT.md): undo each rule and its tests must go red
MQ_NB=sim/tests/test_mqtt_nonblocking.py
MQ_IL=sim/tests/test_mqtt_interleave.py   # the job is stopped at a named step (variants/sim/sim_mqtt_net.cpp), so these do not race
mutate mqtt_connect_runs_inline $MQ_CTRL "  if (!_net->asyncStart(&MqttController::connectEntry, this)) {" "  connectEntry(this);
  if (false) {" $MQ_NB::test_a_connect_attempt_does_not_stall_the_main_loop
mutate mqtt_connect_runs_inline_mesh $MQ_CTRL "  if (!_net->asyncStart(&MqttController::connectEntry, this)) {" "  connectEntry(this);
  if (false) {" $MQ_NB::test_the_mesh_keeps_delivering_during_a_connect_attempt
# the text is matched to the end of the posture block: the disabled branch above has the same line (a mutant on that one
# survived on 2026-10-17, because the first match was the wrong branch)
mutate mqtt_result_taken_before_the_job_ends $MQ_CTRL "    _core.onConnectCancelled();
  }
  if (_job_active && _net->asyncIdle()) takeResult();" "    _core.onConnectCancelled();
  }
  if (_job_active) takeResult();" $MQ_IL::test_the_result_is_not_taken_while_the_job_runs
mutate mqtt_result_taken_before_the_job_ends_while_disabled $MQ_CTRL "    if (_job_active && _net->asyncIdle()) takeResult();
    return;" "    if (_job_active) takeResult();
    return;" "$MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-disable]"
mutate mqtt_cancelled_result_kept $MQ_CTRL "const bool wanted = !_job_cancel && _core.state() == MqttState::Connecting;" "const bool wanted = true;" $MQ_NB::test_a_connect_that_completes_after_it_was_cancelled_is_closed_cleanly
mutate mqtt_cancel_does_not_wait_for_the_job $MQ_CTRL "  if (_job_active && _job_cancel) return;" "" $MQ_NB::test_a_profile_edit_during_a_pending_connect_is_applied_and_leaks_nothing
mutate mqtt_posture_cancel_not_reported_to_the_core $MQ_CTRL "    cancelAttempt();
    _core.onConnectCancelled();" "    cancelAttempt();" $MQ_NB::test_raising_the_posture_during_a_pending_connect_cancels_it
mutate mqtt_late_plain_session_announced_in_hardened $MQ_CTRL "    cancelAttempt();
    _core.onConnectCancelled();
  }
  if (_job_active && _net->asyncIdle()) takeResult();" "  }
  if (_job_active && _net->asyncIdle()) takeResult();" $MQ_NB::test_a_plain_connect_that_completes_after_the_posture_rose_says_nothing
mutate mqtt_disabled_controller_never_takes_the_result $MQ_CTRL "    if (_job_active && _net->asyncIdle()) takeResult();
    return;" "    return;" $MQ_NB::test_disabling_during_a_pending_connect_closes_the_late_session
mutate mqtt_close_ignores_a_running_job $MQ_CTRL "  if (_job_active) {   // the job owns the transport and PubSubClient: ask it to stop and let takeResult() tidy up
    _job_cancel = true;
    return;
  }" "" $MQ_IL::test_an_edit_while_the_job_owns_the_transport_does_not_touch_it
mutate mqtt_cancelled_attempt_penalises_the_profile $MQ_CORE "void MqttCore::onConnectCancelled() {
  if (_state != MqttState::Connecting || _cur == NONE) return;" "void MqttCore::onConnectCancelled() {
  if (_state != MqttState::Connecting || _cur == NONE) return;
  failCurrent(0, MqttFail::Timeout, false);" unit:test_mqtt_core
# --- CA-certificate anchors and SNI (docs/MQTT.md): undo each rule and its tests must go red
MQ_CA_OTHER=$MQ_SIM::test_a_ca_anchor_rejects_a_certificate_from_another_ca
mutate mqtt_ca_chain_not_verified $MQ_NET "      SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER, nullptr);" "      SSL_CTX_set_verify(_ctx, SSL_VERIFY_NONE, nullptr);" $MQ_CA_OTHER
mutate mqtt_ca_host_name_not_checked $MQ_NET "      if (a.ca_b64) SSL_set1_host(_ssl, a.name);" "" $MQ_SIM::test_a_ca_anchor_rejects_a_wrong_host_name
mutate mqtt_sni_not_sent $MQ_NET "      SSL_set_tlsext_host_name(_ssl, a.name);   // SNI, with every TLS profile that has a host name" "" $MQ_SIM::test_the_server_name_is_sent_with_every_tls_profile_that_has_a_host_name
mutate mqtt_ca_refusal_not_reported_as_pin $MQ_NET "return refused ? fail(\"pin\", MqttOpen::Pin) : fail(\"tls\", MqttOpen::Tls);" "return fail(\"tls\", MqttOpen::Tls);" $MQ_CA_OTHER
mutate mqtt_ca_not_passed_on $MQ_CTRL "  oa.ca_b64 = j.ca;" "  oa.ca_b64 = nullptr;" $MQ_CA_OTHER
mutate mqtt_name_not_passed_on $MQ_CTRL "  oa.name = j.name[0] ? j.name : nullptr;" "  oa.name = nullptr;" $MQ_SIM::test_a_ca_anchor_rejects_a_wrong_host_name
mutate mqtt_name_sent_for_an_address_literal $MQ_CTRL "    if (j.resolve) { strlcpy_(j.host, q.host, sizeof(j.host)); strlcpy_(j.name, q.host, sizeof(j.name)); }" "    strlcpy_(j.host, q.host, sizeof(j.host)); strlcpy_(j.name, q.host, sizeof(j.name)); " $MQ_SIM::test_a_ca_anchor_on_an_address_literal_checks_the_chain_only
mutate mqtt_discovery_ca_not_a_usable_anchor $MQ_PROF_H "(validAnchor(q.anchor) || q.hasCa())" "validAnchor(q.anchor)" unit:test_mqtt_prefs
mutate mqtt_discovery_ca_not_a_usable_anchor_sim $MQ_PROF_H "(validAnchor(q.anchor) || q.hasCa())" "validAnchor(q.anchor)" $MQ_SIM::test_a_discovery_ca_anchor_checks_the_chain_and_a_rogue_ca_is_refused
mutate mqtt_ca_does_not_replace_the_fingerprint $MQ_PROF_C "  p[i].anchor[0] = 0;    // a profile has one anchor: the CA replaces a leaf fingerprint" "" unit:test_mqtt_prefs
mutate mqtt_fingerprint_does_not_replace_the_ca $MQ_PROF_C "      else { pr.clearCa(idx); done(true, \"anchor updated\"); }" "      else { done(true, \"anchor updated\"); }" unit:test_mqtt_prefs
mutate mqtt_anchor_clear_keeps_the_ca $MQ_PROF_C "{ q.anchor[0] = 0; pr.clearCa(idx); done(true, \"anchor cleared\"); }" "{ q.anchor[0] = 0; done(true, \"anchor cleared\"); }" unit:test_mqtt_prefs
mutate mqtt_truncated_ca_accepted $MQ_PROF_C "  if (hdr + len != n) return -1;" "" unit:test_mqtt_prefs
mutate mqtt_truncated_ca_accepted_sim $MQ_PROF_C "  if (hdr + len != n) return -1;" "" $MQ_SIM::test_a_damaged_ca_is_refused_at_ca_end_and_the_profile_keeps_its_anchor
mutate mqtt_ca_add_saves_the_prefs $MQ_PROF_C "        pr.stage_len_ = (uint16_t)(pr.stage_len_ + n);
" "        pr.stage_len_ = (uint16_t)(pr.stage_len_ + n);
        change.changed = true;
" unit:test_mqtt_prefs
mutate mqtt_ca_one_character_too_many_accepted $MQ_PROF_C "if (pr.stage_len_ + n > MqttProfile::CA_B64_MAX) {" "if (pr.stage_len_ + n >= MqttProfile::CA_B64_MAX) {" unit:test_mqtt_prefs
mutate mqtt_ca_not_persisted examples/companion_radio/NodePrefs.h "            if (!m.caChunk(i, n, piece)) break;" "            break;" unit:test_mqtt_prefs
mutate mqtt_ca_not_persisted_sim examples/companion_radio/NodePrefs.h "            if (!m.caChunk(i, n, piece)) break;" "            break;" $MQ_SIM::test_a_ca_anchor_survives_the_broker_certificate_being_renewed
mutate mqtt_ca_only_first_chunk_persisted examples/companion_radio/NodePrefs.h "        for (uint8_t n = 0; n < MqttProfile::CA_CHUNKS; n++) {" "        for (uint8_t n = 0; n < 1; n++) {" $MQ_SIM::test_the_largest_practical_ca_fits_and_is_accepted
mutate mqtt_ca_listed_as_fingerprint $MQ_PROF_C 'const char* anchor = q.hasCa() ? "ca" : (q.anchor[0] ? "fingerprint" : "unset");' 'const char* anchor = (q.hasCa() || q.anchor[0]) ? "fingerprint" : "unset";' unit:test_mqtt_prefs
mutate mqtt_ca_copy_not_made $MQ_CTRL "    strcpy(j.ca, q.ca);" "    j.ca[0] = 0;" $MQ_SIM::test_a_ca_anchor_survives_the_broker_certificate_being_renewed
mutate mqtt_damaged_anchor_connects_anyway src/helpers/mqtt/MqttController.cpp "  if (q.useTls() && !MqttProfiles::anchorUsable(q)) {" "  if (false) {" "$MQ_SIM::test_a_damaged_stored_ca_fails_closed_and_never_connects_unverified"
mutate mqtt_damaged_anchor_unit src/helpers/mqtt/MqttProfiles.cpp "  return q.anchor[0] == 0 || validAnchor(q.anchor);" "  return true;" unit:test_mqtt_prefs
mutate mqtt_damaged_ca_dropped_at_load src/helpers/mqtt/MqttProfiles.cpp "    poisonCa(p[i]);    // a gap" "    clearCa(i);    // a gap" unit:test_mqtt_prefs
mutate mqtt_damaged_ca_dropped_at_load_sim src/helpers/mqtt/MqttProfiles.cpp "    poisonCa(p[i]);    // a gap" "    clearCa(i);    // a gap" "$MQ_SIM::test_a_damaged_stored_ca_fails_closed_and_never_connects_unverified"
mutate mqtt_damaged_anchor_not_audited src/helpers/mqtt/MqttController.cpp "    Posture::audit().addOnce(_audit_now" "    (void)(_audit_now" "$MQ_SIM::test_a_damaged_stored_ca_fails_closed_and_never_connects_unverified"
mutate mqtt_no_cancel_check_after_the_name_is_resolved $MQ_CTRL "  if (j.resolve && !_net->resolve(j.host, j.addr)) { j.result = ConnectJob::Dns; return; }
  if (_job_cancel) { j.result = ConnectJob::Cancelled; return; }" "  if (j.resolve && !_net->resolve(j.host, j.addr)) { j.result = ConnectJob::Dns; return; }" "$MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[dns-edit] $MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[dns-disable]"
mutate mqtt_no_cancel_check_after_the_transport_opens $MQ_CTRL "  if (_job_cancel) { _net->close(); j.result = ConnectJob::Cancelled; return; }" "" "$MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-edit] $MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tls-edit]"
mutate mqtt_cancelled_transport_not_closed_by_the_job $MQ_CTRL "  if (_job_cancel) { _net->close(); j.result = ConnectJob::Cancelled; return; }" "  if (_job_cancel) { j.result = ConnectJob::Cancelled; return; }" "$MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-disable] $MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tls-disable]"
mutate mqtt_close_does_not_raise_the_cancel_flag $MQ_CTRL "    _job_cancel = true;
    return;
  }
  if (_open && _psc.connected())" "    return;
  }
  if (_open && _psc.connected())" "$MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-edit] $MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-disable]"
mutate mqtt_posture_does_not_raise_the_cancel_flag $MQ_CTRL "    cancelAttempt();
    _core.onConnectCancelled();" "    _core.onConnectCancelled();" $MQ_IL::test_a_cancel_between_the_steps_stops_the_job_at_the_next_check[tcp-posture]
# --- connectivity mode and its screen (docs/CONNECTIVITY_MODE.md)
CM_H=src/helpers/connmode/ConnMode.h
CS_H=src/helpers/connmode/ConnStatus.h
CM_SIM=sim/tests/test_connectivity_mode.py
UI=examples/companion_radio/ui-new/UITask.cpp
mutate connmode_damaged_byte_starts_wifi $CM_H "inline uint8_t sanitize(uint8_t m) { return m == WIFI ? WIFI : BLE; }" "inline uint8_t sanitize(uint8_t m) { return m != BLE ? WIFI : BLE; }" unit:test_connmode
mutate connmode_ble_may_always_start $CM_H "inline bool bleMayStart(uint8_t mode) { return sanitize(mode) == BLE; }" "inline bool bleMayStart(uint8_t mode) { return true; }" unit:test_connmode
mutate connmode_ble_started_in_wifi_mode examples/companion_radio/main.cpp "if (!ConnMode::bleMayStart(conn_mode)) {" "if (false) {" $CM_SIM::test_wifi_mode_starts_only_the_wifi_stack_and_persists
mutate connmode_wifi_started_in_ble_mode examples/companion_radio/main.cpp "if (!ConnMode::wifiMayStart(conn_mode)) wifi_enabled = false;" "if (false) wifi_enabled = false;" $CM_SIM::test_the_default_mode_is_ble_and_only_the_ble_stack_starts
mutate connmode_remote_refusal_not_audited $CM_H "    Posture::recordRefusal(now, caller, Posture::W_CONNMODE);
    snprintf(reply, max, \"ERR: not permitted\");" "    snprintf(reply, max, \"ERR: not permitted\");" unit:test_connmode
mutate connmode_change_not_audited $CM_H "  Posture::audit().add(now, Posture::EV_MODE_SET, caller, want);" "  (void)now;" unit:test_connmode
mutate connmode_change_not_audited_sim $CM_H "  Posture::audit().add(now, Posture::EV_MODE_SET, caller, want);" "  (void)now;" $CM_SIM::test_the_cable_switch_is_audited
mutate connmode_not_persisted examples/companion_radio/NodePrefs.h "      def(\"conn_mode\", _parent->connectivity_mode);" "" $CM_SIM::test_wifi_mode_starts_only_the_wifi_stack_and_persists
mutate connmode_prefs_written_in_place examples/companion_radio/DataStore.cpp "return AtomicFile::save(_fs, \"/prefs.json\", [&_prefs](Stream& s) { return _prefs.saveSerial(s); });" "File f = _fs->open(\"/prefs.json\", \"w\", true); bool ok = f && _prefs.saveSerial(f); if (f) f.close(); return ok;" $CM_SIM::test_a_power_cut_while_switching_keeps_the_previous_complete_prefs
mutate connmode_switch_without_restart examples/companion_radio/MyMesh.cpp "    board.reboot();   // starts the stack of the new mode; the mode is already saved" "    // no restart" $CM_SIM::test_wifi_mode_starts_only_the_wifi_stack_and_persists
mutate connmode_button_switches_without_confirmation $UI "      _sw_confirm = true;
      _sw_until = millis() + CONNMODE_CONFIRM_MILLIS;" "      the_mesh.requestConnectivityMode(ConnMode::other(the_mesh.getConnectivityMode()), Posture::CALLER_BUTTON);" $CM_SIM::test_the_button_switches_only_after_a_confirming_press
mutate connmode_any_key_confirms $UI "      if (c == KEY_ENTER) {
        const uint8_t want" "      if (true) {
        const uint8_t want" $CM_SIM::test_a_cancelled_confirmation_does_nothing
mutate connmode_confirmation_never_times_out $UI "(millis() > _sw_until || _page != HomePage::CONNECTIVITY)" "(false || _page != HomePage::CONNECTIVITY)" $CM_SIM::test_an_unconfirmed_switch_times_out_and_changes_nothing
mutate connmode_bluetooth_page_not_skipped $UI "if (_page == HomePage::BLUETOOTH && the_mesh.getConnectivityMode() != ConnMode::BLE) _page = (_page + 1) % HomePage::Count;" "if (false) _page = (_page + 1) % HomePage::Count;" $CM_SIM::test_the_button_switches_back_and_works_in_hardened
mutate connmode_default_pin_not_flagged $CM_H "inline bool pinIsDefault(uint32_t pin) { return pin == 0 || pin == 123456; }" "inline bool pinIsDefault(uint32_t pin) { return pin == 0; }" unit:test_connmode
mutate connmode_default_pin_not_flagged_sim $CM_H "inline bool pinIsDefault(uint32_t pin) { return pin == 0 || pin == 123456; }" "inline bool pinIsDefault(uint32_t pin) { return pin == 0; }" $CM_SIM::test_a_default_pin_is_a_warning_not_a_credential
mutate connmode_posture_not_shown $CS_H "Posture::isHardened(s.posture) ? \"Hardened\" : \"Normal\"" "\"Normal\"" unit:test_connmode
mutate connmode_posture_not_shown_sim $CS_H "Posture::isHardened(s.posture) ? \"Hardened\" : \"Normal\"" "\"Normal\"" $CM_SIM::test_the_screen_follows_the_posture
mutate connmode_home_pin_hidden_by_usb $CS_H "if (pin != 0 && ble_mode && !ble_connected) return LINK_PIN;" "if (pin != 0 && ble_mode && !any_connected) return LINK_PIN;" unit:test_connmode
mutate connmode_ble_connected_counts_usb src/helpers/MultiSerialInterface.h "if(iface.instance && iface.type == type && iface.instance->isConnected()) {" "if(iface.instance && iface.instance->isConnected()) {" $CM_SIM::test_ble_mode_screen_and_home_line
mutate connmode_tls_unverified_shown_verified examples/companion_radio/ui-new/ConnStatusScreen.h "(m.core()->verified() ? ConnMode::TLS_VERIFIED : ConnMode::TLS_UNVERIFIED)" "ConnMode::TLS_VERIFIED" $CM_SIM::test_mqtt_line_says_whether_tls_is_verified
mutate connmode_wifi_password_drawn_as_ssid examples/companion_radio/ui-new/ConnStatusScreen.h "snprintf(s.ssid, sizeof(s.ssid), \"%s\", prefs->wifi_profiles.ssid(cur));" "snprintf(s.ssid, sizeof(s.ssid), \"%s\", prefs->wifi_profiles.pwd_[cur][0] ? prefs->wifi_profiles.pwd_[cur] : prefs->wifi_profiles.ssid(cur));" $CM_SIM::test_no_secret_is_ever_drawn
mutate connmode_mqtt_password_drawn $UI "      int ly = 19;" "      int ly = 19; display.drawTextCentered(display.width() / 2, 8, _node_prefs->mqtt_profiles.p[0].pwd);" $CM_SIM::test_no_secret_is_ever_drawn

# --- adaptive contact memory (docs/CONTACTS.md): undo each rule and its tests must go red
CS=src/helpers/ContactStore.cpp
CT_SIM=sim/tests/test_contacts.py
mutate contacts_bucket_written_in_place examples/companion_radio/DataStore.cpp '  bucketPath(path, sizeof(path), b);
  return AtomicFile::save(_getContactsChannelsFS(), path, [buf, len](Stream& s) { return s.write(buf, len) == len; }, is_new);
}

bool DataStore::removeBucket' '  bucketPath(path, sizeof(path), b);
  File f = openWrite(_getContactsChannelsFS(), path); if (!f) return false; bool ok = f.write(buf, len) == len; f.close(); return ok;
}

bool DataStore::removeBucket' $CT_SIM::test_power_cut_in_the_temp_file_keeps_the_previous_record $CT_SIM::test_contacts_are_never_written_in_place
mutate contacts_stock_file_written_in_place examples/companion_radio/DataStore.cpp 'AtomicFile::save(_getContactsChannelsFS(), "/contacts3", [host, filter](Stream& s) {' 'auto inPlaceSave = [](FILESYSTEM* fs, const char* path, auto writer) { File f = openWrite(fs, path); if (f) { writer(f); f.close(); } }; inPlaceSave(_getContactsChannelsFS(), "/contacts3", [host, filter](Stream& s) {' $CT_SIM::test_the_stock_array_build_is_ram_only_and_atomic_and_evicts_the_oldest
mutate contacts_flush_skipped examples/companion_radio/MyMesh.cpp '  flushContacts();   // rewrites the buckets that hold a changed record, each one atomically' '' $CT_SIM::test_power_cut_between_write_and_rename
mutate contacts_old_file_kept_after_migration examples/companion_radio/MyMesh.cpp '  if (ok) _store->removeLegacyContacts();' '' $CT_SIM::test_stock_contacts_file_migrates_unchanged
mutate contacts_merge_overwrites_store examples/companion_radio/MyMesh.cpp '    if (contactStore().find(contact.id.pub_key, PUB_KEY_SIZE)) return true;   // the store'"'"'s version is newer' '' $CT_SIM::test_a_stock_file_beside_a_filled_store_is_merged_not_duplicated
mutate contacts_bulk_import_into_filled_store examples/companion_radio/MyMesh.cpp '  bool bulk = cs.count() == 0;' '  bool bulk = true;' $CT_SIM::test_a_stock_file_beside_a_filled_store_is_merged_not_duplicated
mutate contacts_record_crc_unchecked $CS '  if (crc32(in, 156) != want) return false;' '' unit:test_contact_store
mutate contacts_record_crc_unchecked_sim $CS '  if (crc32(in, 156) != want) return false;' '' $CT_SIM::test_a_damaged_bucket_loses_only_what_is_in_it
mutate contacts_record_layout_changed $CS '  out[64] = c.type;
  out[65] = c.flags;' '  out[64] = c.flags;
  out[65] = c.type;' unit:test_contact_store
mutate contacts_record_layout_changed_sim $CS '  out[64] = c.type;
  out[65] = c.flags;' '  out[64] = c.flags;
  out[65] = c.type;' $CT_SIM::test_stock_contacts_file_migrates_unchanged
mutate contacts_key_prefilter_fixed_width $CS '  int n = prefix_len < 4 ? prefix_len : 4;' '  int n = 4;' unit:test_contact_store
mutate contacts_hash_lookup_inverted $CS '    if (memcmp(e.key, hash, PATH_HASH_SIZE) == 0) out[n++] = r;' '    if (memcmp(e.key, hash, PATH_HASH_SIZE) != 0) out[n++] = r;' unit:test_contact_store
mutate contacts_flush_writes_every_bucket $CS '    if (_bdirty[b >> 3] & (1u << (b & 7))) {' '    if (true) {' unit:test_contact_store
mutate contacts_cache_ignores_heap $CS '    uint32_t slots = cache_budget / sizeof(Slot);' '    uint32_t slots = cfg.cache_ceiling;' unit:test_contact_store
mutate contacts_cache_ignores_heap_sim $CS '    uint32_t slots = cache_budget / sizeof(Slot);' '    uint32_t slots = cfg.cache_ceiling;' $CT_SIM::test_capacity_and_cache_follow_the_heap_the_board_reports
mutate contacts_boot_probes_every_bucket $CS '    if (_bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' '    if (false && _bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' $CT_SIM::test_boot_file_system_calls_do_not_grow_with_the_capacity_empty_store $CT_SIM::test_boot_file_system_calls_follow_the_files_present_with_350_contacts
mutate contacts_boot_probes_every_bucket_unit $CS '    if (_bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' '    if (false && _bpresent && _be->listBuckets((uint16_t)limit, _bpresent)) {' unit:test_contact_store
mutate contacts_boot_recovers_every_bucket examples/companion_radio/DataStore.cpp '      if (!(swap[b >> 3] & (1u << (b & 7)))) continue;
' '' $CT_SIM::test_boot_file_system_calls_do_not_grow_with_the_capacity_empty_store
mutate contacts_boot_swap_not_recovered examples/companion_radio/DataStore.cpp '      AtomicFile::recover(fs, path);
      if (fs->exists(path)) present' '      if (fs->exists(path)) present' $CT_SIM::test_a_swap_left_by_a_power_cut_in_a_high_bucket_is_recovered_at_boot
mutate contacts_capacity_ignores_filesystem $CS '    if (cap > cap_fs) cap = (uint32_t)cap_fs;' '' unit:test_contact_store
mutate contacts_capacity_ignores_filesystem_sim $CS '    if (cap > cap_fs) cap = (uint32_t)cap_fs;' '' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_capacity_below_what_is_stored $CS '    if (cap < (uint32_t)(max_bucket + 1) * K) cap = (uint32_t)(max_bucket + 1) * K;   // never smaller than what is stored' '' unit:test_contact_store
mutate contacts_favourites_not_pinned_on_use $CS '  if (fav && !s.pinned && _pinned < _pin_limit) { s.pinned = 1; _pinned++; }' '  if (false) { s.pinned = 1; _pinned++; }' unit:test_contact_store
mutate contacts_favourites_not_pinned_on_replace $CS '    if (s.rec >= 0 && !s.pinned && (s.ci.flags & 0x01)) { s.pinned = 1; _pinned++; }' '    if (false) { s.pinned = 1; _pinned++; }' unit:test_contact_store
mutate contacts_pinned_slot_replaced_sim $CS '    if (_slots[i].pinned) continue;' '' $CT_SIM::test_favourites_stay_resident_and_reachable_under_a_small_heap
mutate contacts_pinned_slot_replaced $CS '    if (_slots[i].pinned) continue;' '' unit:test_contact_store
mutate contacts_pinned_favourites_fill_the_cache $CS '  _pin_limit = _nslots > CONTACT_WORKING_SET ? _nslots - CONTACT_WORKING_SET : 0;' '  _pin_limit = _nslots;' unit:test_contact_store
mutate contacts_dirty_slot_dropped $CS '  if (slotDirty(_slots[victim])) {' '  if (false) {' unit:test_contact_store
mutate contacts_failed_write_still_evicts $CS '    if (!flushBucket(bucketOf(_slots[victim].rec))) return -1;   // cannot let a changed record go' '    flushBucket(bucketOf(_slots[victim].rec));' unit:test_contact_store
mutate contacts_sync_only_cached $CS '    if (readRecord(r, out, lm)) return true;' '    continue;' $CT_SIM::test_app_sync_returns_every_stored_contact_beyond_the_cache
mutate contacts_sync_only_cached_unit $CS '    if (readRecord(r, out, lm)) return true;' '    continue;' unit:test_contact_store
mutate contacts_sync_count_is_the_cache src/helpers/BaseChatMesh.h '  int getNumContacts() const { return cstore.count(); }' '  int getNumContacts() const { return cstore.cacheSlots(); }' $CT_SIM::test_app_sync_returns_every_stored_contact_beyond_the_cache
mutate contacts_evict_without_permission $CS '    if (!allow_evict) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' '    if (false) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' $CT_SIM::test_without_the_overwrite_setting_a_full_store_refuses_and_keeps_everything
mutate contacts_evict_without_permission_unit $CS '    if (!allow_evict) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' '    if (false) { _refused_full++; _refusal = REFUSED_FULL; return NULL; }' unit:test_contact_store
mutate contacts_favourite_evictable $CS '    if (!(e.flags & F_USED) || (e.flags & F_FAV)) continue;' '    if (!(e.flags & F_USED)) continue;' unit:test_contact_store
mutate contacts_favourite_evictable_sim $CS '    if (!(e.flags & F_USED) || (e.flags & F_FAV)) continue;' '    if (!(e.flags & F_USED)) continue;' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_all_favourites_refusal_not_counted $CS '    if (rec < 0) { _refused_fav++; _refusal = REFUSED_ALL_FAVOURITES; return NULL; }' '    if (rec < 0) { _refusal = REFUSED_ALL_FAVOURITES; return NULL; }' $CT_SIM::test_a_store_of_only_favourites_refuses_a_new_favourite_and_says_so
mutate contacts_overwrite_event_missing $CS '      _listener->onContactEvicted(key);                    // the stock overwrite event, before the record goes' '' unit:test_contact_store
mutate contacts_overwrite_event_missing_sim $CS '      _listener->onContactEvicted(key);                    // the stock overwrite event, before the record goes' '' $CT_SIM::test_a_full_store_evicts_the_least_meaningful_and_never_a_favourite
mutate contacts_message_history_ignored $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;
  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' unit:test_contact_store
mutate contacts_message_history_ignored_sim $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;
  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_old_message_counts_for_nothing $CS '  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.flags & F_PATH) return 1;' unit:test_contact_store
mutate contacts_recent_message_window_ignored $CS '  if (e.last_msg && (now < e.last_msg || now - e.last_msg <= CONTACT_RECENT_SECS)) return 2;' '' unit:test_contact_store
mutate contacts_known_path_ignored $CS '  if (e.last_msg || (e.flags & F_PATH)) return 1;' '  if (e.last_msg) return 1;' unit:test_contact_store
mutate contacts_stock_policy_always $CS '    if (_cfg.stock_policy) { cls = 0; t = e.lastmod; }' '    if (true) { cls = 0; t = e.lastmod; }' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_stock_policy_always_unit $CS '    if (_cfg.stock_policy) { cls = 0; t = e.lastmod; }' '    if (true) { cls = 0; t = e.lastmod; }' unit:test_contact_store
mutate contacts_stock_array_build_evicts_by_score src/helpers/BaseChatMesh.cpp '  cfg.stock_policy = true;' '  cfg.stock_policy = false;' $CT_SIM::test_the_stock_array_build_is_ram_only_and_atomic_and_evicts_the_oldest
mutate contacts_message_sent_not_noted src/helpers/BaseChatMesh.cpp '  noteDirectMessage(recipient);   // we wrote to them: they are worth keeping' '' $CT_SIM::test_a_contact_with_message_history_outlives_an_advert_only_one
mutate contacts_message_received_not_noted src/helpers/BaseChatMesh.cpp '      noteDirectMessage(from);' '' $CT_SIM::test_a_contact_that_wrote_to_us_outlives_advert_only_ones
mutate contacts_message_time_not_kept $CS '  _idx[rec].last_msg = now;' '' unit:test_contact_store
mutate contacts_secrets_not_invalidated $CS '  for (int i = 0; i < _nslots; i++) if (_slots && _slots[i].rec >= 0) _slots[i].ci.shared_secret_valid = false;' '' unit:test_contact_store
mutate contacts_bulk_import_writes_each_record src/helpers/ContactStore.cpp '  if (b != _import_bucket) {' '  if (true) {' unit:test_contact_store
mutate contacts_rebuild_probes_each_bucket src/helpers/ContactStore.cpp '  const bool have_list = listed && _be->listBuckets((uint16_t)_nbuckets, listed);' '  const bool have_list = false;' unit:test_contact_store
mutate contacts_rebuild_probes_each_bucket_sim src/helpers/ContactStore.cpp '  const bool have_list = listed && _be->listBuckets((uint16_t)_nbuckets, listed);' '  const bool have_list = false;' sim/tests/test_contacts.py::test_rebuild_looks_up_no_bucket_that_is_not_there
# rescue "rebuild" works from the store (docs/CONTACTS.md)
CT_KEEP=$CT_SIM::test_rebuild_keeps_every_stored_contact_beyond_the_cache
CT_ISO=$CT_SIM::test_rebuild_isolates_a_damaged_record_and_keeps_all_the_others
CT_CUT=$CT_SIM::test_a_power_cut_during_rebuild_keeps_the_previous_buckets
mutate contacts_rebuild_cache_only $CS '        if (!zero && decode(r, c, lm)) {' '        if (false) {' "$CT_KEEP $CT_ISO"
mutate contacts_rebuild_cache_only_unit $CS '        if (!zero && decode(r, c, lm)) {' '        if (false) {' unit:test_contact_store
mutate contacts_rebuild_damaged_bucket_not_kept_aside $CS '      if (!zero && !decode(r, c, lm)) any_bad = true;' '' "$CT_ISO"
mutate contacts_rebuild_damaged_bucket_not_kept_aside_unit $CS '      if (!zero && !decode(r, c, lm)) any_bad = true;' '' unit:test_contact_store
mutate contacts_rebuild_damaged_record_kept $CS '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }
          memset(r, 0, REC);' '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }' "$CT_ISO"
mutate contacts_rebuild_damaged_record_kept_unit $CS '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }
          memset(r, 0, REC);' '          if (e.flags & F_USED) { memset(&e, 0, sizeof(Idx)); e.slot = NOSLOT; _count--; }' unit:test_contact_store
mutate contacts_rebuild_rewritten_after_remove $CS '    if (any) ok = writeB(b);
    else ok = have ? removeB(b) : true;
    if (!ok) { _write_errors++; continue; }' '    removeB(b);
    if (any) ok = writeB(b);
    else ok = have ? removeB(b) : true;
    if (!ok) { _write_errors++; continue; }' "$CT_CUT"
mutate contacts_rebuild_formats_filesystem examples/companion_radio/MyMesh.cpp '      int damaged = 0, isolated = 0;' '      _store->formatFileSystem();
      int damaged = 0, isolated = 0;' "$CT_KEEP $CT_ISO"
mutate contacts_rebuild_damaged_not_reported $CS '  if (damaged_out) *damaged_out = damaged;
  if (isolated_out)' '  if (isolated_out)' "$CT_ISO"
mutate contacts_rebuild_damaged_not_reported_unit $CS '  if (damaged_out) *damaged_out = damaged;
  if (isolated_out)' '  if (isolated_out)' unit:test_contact_store
mutate contacts_device_query_wraps examples/companion_radio/MyMesh.cpp '      out_frame[i++] = half > 255 ? 255 : half;   // v3+' '      out_frame[i++] = half;   // v3+' $CT_SIM::test_device_query_reports_half_the_capacity_capped_at_one_byte

# --- the unified command-line engine (docs/CLI.md): one mutant per rule of the privilege table, one per rule of the
# --- registry and the adapters. The rules that used to sit in the paths below (transport trust, the remote CLI limits, the
# --- postures, the default-password rule, set prv.key, the write-only secrets) now live in the table; each mutant breaks
# --- the moved rule at its new place and points at the tests that can see it.
CLI_P=src/helpers/cli/CliPolicy.h
CLI_R=src/helpers/cli/CliRegistry.h
CLI_E=src/helpers/cli/CliEngine.h
CLI_M=src/helpers/cli/CliMqtt.h
CLI_UNIT=unit:test_cli_engine
# table rows: a cell or a whole row loosened
mutate table_untrusted_row_open src/helpers/cli/CliPolicy.h "/* untrusted      N */ { CLI_ROW(UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED)," "/* untrusted      N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW)," sim/tests/test_companion_transport.py sim/tests/test_postures_companion.py::test_untrusted_transport_refusals_are_audited
# the native management frames on the TCP port and default-PIN BLE (docs/CLI.md), and the secrets their reads withhold
FG=sim/tests/test_companion_frame_gate.py
mutate frame_gate_radio_params_ungated examples/companion_radio/MyMesh.cpp 'case CMD_SET_ADVERT_LATLON: case CMD_SET_RADIO_PARAMS:' 'case CMD_SET_ADVERT_LATLON:' "$FG::test_management_frame_refused_over_tcp[set-radio-params]"
mutate frame_gate_reboot_ungated examples/companion_radio/MyMesh.cpp '    case CMD_REBOOT: return Cli::FR_APP_REBOOT;' '' "$FG::test_management_frame_refused_over_tcp[reboot]"
mutate frame_gate_cli_text_ungated examples/companion_radio/MyMesh.cpp 'if (txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND) return Cli::FR_APP_WRITE;' '(void)txt_type;' "$FG::test_management_frame_refused_over_tcp[send-cli-text]"
mutate frame_gate_repeater_text_ungated examples/companion_radio/MyMesh.cpp 'if (to && to->type == ADV_TYPE_REPEATER) return Cli::FR_APP_WRITE;' '(void)to;' $FG::test_text_to_a_repeater_is_remote_administration
mutate frame_gate_pin_to_untrusted examples/companion_radio/MyMesh.cpp 'const uint32_t pin = serialTrusted() ? _prefs.ble_pin : 0;' 'const uint32_t pin = _prefs.ble_pin;' $FG::test_untrusted_reads_carry_no_secrets
mutate frame_gate_channel_secret_to_untrusted examples/companion_radio/MyMesh.cpp '      if (serialTrusted()) memcpy(&out_frame[i], channel.channel.secret, 16);
      else memset(&out_frame[i], 0, 16);' '      memcpy(&out_frame[i], channel.channel.secret, 16);' $FG::test_untrusted_reads_carry_no_secrets
mutate frame_gate_cable_untrusted examples/companion_radio/MyMesh.cpp '  return Cli::decide(serialContext(), Cli::C_READ).allow;' '  return false;' $FG::test_untrusted_reads_carry_no_secrets
mutate table_untrusted_row_open_unit src/helpers/cli/CliPolicy.h "/* untrusted      N */ { CLI_ROW(UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED,       UNTRUSTED)," "/* untrusted      N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW)," unit:test_cli_engine
mutate table_cable_key_in_hardened src/helpers/cli/CliPolicy.h "/* cable          H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           ALLOW,           ALLOW) }," "/* cable          H */   CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW) }," sim/tests/test_postures_companion.py::test_hardened_refuses_key_export_and_import_on_usb sim/tests/test_postures_admin.py::test_key_export_and_import_are_refused_in_hardened_even_on_the_console
mutate table_cable_key_in_hardened_unit src/helpers/cli/CliPolicy.h "/* cable          H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           ALLOW,           ALLOW) }," "/* cable          H */   CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW) }," unit:test_cli_engine
mutate table_paired_factory_reset_in_hardened src/helpers/cli/CliPolicy.h "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           NOT_PERMITTED,   ALLOW) }," "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           ALLOW,           ALLOW) }," unit:test_cli_engine
mutate table_paired_key_in_hardened src/helpers/cli/CliPolicy.h "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           NOT_PERMITTED,   ALLOW) }," "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW) }," unit:test_cli_engine
mutate table_remote_admin_prvkey_allowed src/helpers/cli/CliPolicy.h "/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW)," "/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           NOT_PERMITTED,   LOCAL_ONLY,      ALLOW)," sim/tests/test_repeater_prvkey.py::test_remote_admin_cannot_replace_identity
mutate table_remote_admin_posture_allowed src/helpers/cli/CliPolicy.h "/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW)," "/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      ALLOW,           LOCAL_ONLY,      ALLOW)," sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate table_remote_admin_writes_in_hardened src/helpers/cli/CliPolicy.h "/* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   HARDENED,        ALLOW) }," "/* remote admin   H */   CLI_ROW(ALLOW,           ALLOW,           HARDENED,        NOT_PERMITTED,   HARDENED,        ALLOW) }," sim/tests/test_postures_admin.py::test_hardened_remote_admin_is_read_only_plus_reboot
mutate table_remote_admin_destructive_in_hardened src/helpers/cli/CliPolicy.h "/* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   HARDENED,        ALLOW) }," "/* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   ALLOW,           ALLOW) }," unit:test_posture
mutate table_remote_admin_reboot_refused src/helpers/cli/CliPolicy.h "/* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   HARDENED,        ALLOW) }," "/* remote admin   H */   CLI_ROW(ALLOW,           HARDENED,        HARDENED,        NOT_PERMITTED,   HARDENED,        HARDENED) }," sim/tests/test_postures_admin.py::test_hardened_remote_admin_is_read_only_plus_reboot
mutate table_default_password_row_open src/helpers/cli/CliPolicy.h "/* admin, default N */ { CLI_ROW(DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW,      DEFAULT_PW)," "/* admin, default N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW)," sim/tests/test_postures_admin.py::test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session
mutate table_guest_row_open src/helpers/cli/CliPolicy.h "/* guest          N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED)," "/* guest          N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW)," unit:test_cli_engine
mutate table_contact_writes_allowed src/helpers/cli/CliPolicy.h "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," "/* flagged contact N */ { CLI_ROW(ALLOW,           ALLOW,           REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," sim/tests/test_companion_remote_cli.py
mutate table_contact_secret_allowed src/helpers/cli/CliPolicy.h "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  ALLOW,           NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," unit:test_cli_engine
mutate table_contact_posture_allowed src/helpers/cli/CliPolicy.h "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  ALLOW,           REMOTE_LIMITED,  ALLOW)," sim/tests/test_postures_companion.py::test_remote_cli_cannot_change_the_posture_and_it_is_audited
mutate table_contact_switches_the_mode src/helpers/cli/CliPolicy.h "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  NOT_PERMITTED,   REMOTE_LIMITED,  ALLOW)," "/* flagged contact N */ { CLI_ROW(ALLOW,           REMOTE_LIMITED,  REMOTE_LIMITED,  ALLOW,           REMOTE_LIMITED,  ALLOW)," sim/tests/test_connectivity_mode.py::test_a_remote_switch_is_refused_and_audited
mutate table_contact_cli_stays_on_in_hardened src/helpers/cli/CliPolicy.h "/* flagged contact H */   CLI_ROW(CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF) }," "/* flagged contact H */   CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW) }," sim/tests/test_postures_companion.py::test_hardened_turns_the_remote_cli_off
mutate table_contact_reads_in_hardened src/helpers/cli/CliPolicy.h "/* flagged contact H */   CLI_ROW(CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF) }," "/* flagged contact H */   CLI_ROW(ALLOW,           CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF,         CLI_OFF) }," sim/tests/test_postures_companion.py::test_hardened_turns_the_remote_cli_off
mutate table_mqtt_posture_allowed src/helpers/cli/CliPolicy.h "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   ALLOW)," sim/tests/test_cli_unified.py::test_mqtt_cannot_do_what_only_a_local_caller_may
mutate table_mqtt_secret_allowed src/helpers/cli/CliPolicy.h "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," unit:test_cli_engine
mutate table_mqtt_destructive_allowed src/helpers/cli/CliPolicy.h "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   ALLOW,           ALLOW)," unit:test_cli_engine
mutate table_mqtt_hardened_writes_always src/helpers/cli/CliPolicy.h "/* MQTT verified  H */   CLI_ROW(ALLOW,           NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) }," "/* MQTT verified  H */   CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) }," sim/tests/test_cli_unified.py::test_hardened_mqtt_is_read_only_unless_the_profile_is_flagged_for_management
mutate table_mqtt_hardened_reboot_always src/helpers/cli/CliPolicy.h "/* MQTT verified  H */   CLI_ROW(ALLOW,           NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) }," "/* MQTT verified  H */   CLI_ROW(ALLOW,           NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW) }," unit:test_cli_engine
mutate table_mqtt_hardened_reads_refused src/helpers/cli/CliPolicy.h "/* MQTT verified  H */   CLI_ROW(ALLOW,           NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) }," "/* MQTT verified  H */   CLI_ROW(NOT_PERMITTED,   NEEDS_FLAG,      NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NEEDS_FLAG) }," sim/tests/test_cli_unified.py::test_hardened_mqtt_is_read_only_unless_the_profile_is_flagged_for_management
mutate table_mqtt_normal_writes_refused src/helpers/cli/CliPolicy.h "/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," "/* MQTT verified  N */ { CLI_ROW(ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW)," sim/tests/test_cli_unified.py::test_the_same_command_over_usb_lora_and_mqtt
mutate table_mqtt_unverified_row_open src/helpers/cli/CliPolicy.h "/* MQTT unverified N */ { CLI_ROW(MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED, MQTT_UNVERIFIED)," "/* MQTT unverified N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW)," sim/tests/test_cli_unified.py::test_mqtt_is_refused_on_plain_and_on_unverified_tls_and_audited
mutate table_button_cannot_switch src/helpers/cli/CliPolicy.h "/* button         N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   NOT_PERMITTED)," "/* button         N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED)," unit:test_connmode
mutate table_button_runs_commands src/helpers/cli/CliPolicy.h "/* button         N */ { CLI_ROW(NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   NOT_PERMITTED)," "/* button         N */ { CLI_ROW(NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   NOT_PERMITTED)," unit:test_cli_engine
# the rules around the table
mutate policy_flag_ignored_for_the_lowering_rule src/helpers/cli/CliPolicy.h "if (r == R_ALLOW && (flags & F_CABLE_ONLY) && p != P_CABLE && p != P_SIGNED) r = R_NOT_PERMITTED;" "if (false) r = R_NOT_PERMITTED;" unit:test_posture
mutate policy_flag_ignored_for_the_lowering_rule_unit src/helpers/cli/CliPolicy.h "if (r == R_ALLOW && (flags & F_CABLE_ONLY) && p != P_CABLE && p != P_SIGNED) r = R_NOT_PERMITTED;" "if (false) r = R_NOT_PERMITTED;" unit:test_cli_engine
mutate policy_management_flag_ignored src/helpers/cli/CliPolicy.h "if (r == R_NEEDS_FLAG) r = c.mgmt ? R_ALLOW : R_MQTT_READONLY;" "if (r == R_NEEDS_FLAG) r = R_ALLOW;" unit:test_cli_engine
mutate policy_hardened_byte_fails_open src/helpers/cli/CliPolicy.h "tableCell(p, c.posture != 0, cls)" "tableCell(p, c.posture == 1, cls)" unit:test_cli_engine
mutate policy_inconsistent_context_trusted src/helpers/cli/CliPolicy.h "if (c.remote != wire_remote) return P_UNTRUSTED;" "" unit:test_cli_engine
mutate policy_unset_context_trusted src/helpers/cli/CliPolicy.h "    default: return P_UNTRUSTED;   // T_TCP, T_NONE, anything else" "    default: return P_CABLE;" unit:test_cli_engine
mutate policy_default_pin_ble_is_paired src/helpers/cli/CliPolicy.h "case T_BLE: return c.auth == A_PAIRED_PIN ? P_PAIRED : P_UNTRUSTED;" "case T_BLE: return (c.auth == A_PAIRED_PIN || c.auth == A_PAIRED_DEFAULT_PIN) ? P_PAIRED : P_UNTRUSTED;" unit:test_cli_engine
mutate policy_unverified_session_is_verified src/helpers/cli/CliMqtt.h "return Context(T_MQTT, tls_verified ? A_TLS_VERIFIED : A_TLS_UNVERIFIED, posture, profile_manages);" "return Context(T_MQTT, A_TLS_VERIFIED, posture, profile_manages);" unit:test_cli_mqtt
mutate policy_unknown_class_allowed src/helpers/cli/CliPolicy.h "if (p >= P_COUNT || cls >= C_COUNT) return R_NOT_PERMITTED;" "if (p >= P_COUNT || cls >= C_COUNT) return R_ALLOW;" unit:test_cli_engine
# the registry
mutate registry_unregistered_command_is_a_read src/helpers/cli/CliRegistry.h "  uint8_t cls = C_WRITE;" "  uint8_t cls = C_READ;"  sim/tests/test_companion_remote_cli.py
mutate registry_unregistered_command_is_a_read_unit src/helpers/cli/CliRegistry.h "  uint8_t cls = C_WRITE;" "  uint8_t cls = C_READ;" unit:test_cli_engine
mutate registry_get_prvkey_is_a_read src/helpers/cli/CliRegistry.h "{\"get prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET," "{\"get prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_READ,  " sim/tests/test_postures_admin.py::test_key_export_and_import_are_refused_in_hardened_even_on_the_console
mutate registry_set_prvkey_is_a_write src/helpers/cli/CliRegistry.h "{\"set prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET," "{\"set prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_WRITE, " sim/tests/test_repeater_prvkey.py::test_remote_admin_cannot_replace_identity
mutate registry_prvkey_import_refusal_is_audited src/helpers/cli/CliRegistry.h "{\"set prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET,      F_QUIET," "{\"set prv.key\",            M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_SECRET,      0,       " unit:test_cli_engine
mutate registry_wifi_password_readable src/helpers/cli/CliRegistry.h "{\"get wifi.pwd\",           M_EXACT," "{\"get wifi.pwdX\",          M_EXACT," sim/tests/test_cli_unified.py::test_get_wifi_pwd_is_answered_write_only_by_the_engine
mutate registry_wifi_password_readable_unit src/helpers/cli/CliRegistry.h "{\"get wifi.pwd\",           M_EXACT," "{\"get wifi.pwdX\",          M_EXACT," unit:test_cli_engine
mutate registry_audit_is_a_read src/helpers/cli/CliRegistry.h "{\"get audit\",              M_WORD,    ROLE_COMPANION | ROLE_REPEATER, C_POSTURE," "{\"get audit\",              M_WORD,    ROLE_COMPANION | ROLE_REPEATER, C_READ,   " unit:test_cli_engine
mutate registry_poweroff_is_a_write src/helpers/cli/CliRegistry.h '{"poweroff",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_DESTRUCTIVE,' '{"poweroff",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_WRITE,      ' sim/tests/test_cli_unified.py::test_mqtt_cannot_power_the_device_off_but_the_console_can
mutate registry_poweroff_is_a_write_unit src/helpers/cli/CliRegistry.h '{"poweroff",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_DESTRUCTIVE,' '{"poweroff",               M_PREFIX,  ROLE_COMPANION | ROLE_REPEATER, C_WRITE,      ' unit:test_cli_engine
mutate registry_reboot_is_a_prefix src/helpers/cli/CliRegistry.h "{\"reboot\",                 M_EXACT," "{\"reboot\",                 M_PREFIX," unit:test_cli_engine
mutate registry_erase_is_a_write src/helpers/cli/CliRegistry.h "{\"erase\",                  M_EXACT,   ROLE_REPEATER,                  C_DESTRUCTIVE," "{\"erase\",                  M_EXACT,   ROLE_REPEATER,                  C_WRITE,      " unit:test_cli_engine
mutate registry_set_posture_lowering_not_cable_only src/helpers/cli/CliRegistry.h "r.flags = strcmp(arg, \"hardened\") != 0 ? F_CABLE_ONLY : F_RAISES;" "r.flags = 0;" unit:test_cli_engine
mutate registry_mgmt_flag_is_a_write src/helpers/cli/CliRegistry.h "if (role == ROLE_COMPANION && isMgmtFlag(cmd)) {" "if (false) {" sim/tests/test_cli_unified.py::test_mqtt_cannot_do_what_only_a_local_caller_may sim/tests/test_cli_unified.py::test_mqtt_cannot_do_what_only_a_local_caller_may
mutate registry_remote_read_list_has_clock_for_companions src/helpers/cli/CliRegistry.h "{\"clock\",                  M_EXACT,   ROLE_REPEATER," "{\"clock\",                  M_EXACT,   ROLE_COMPANION | ROLE_REPEATER," unit:test_cli_engine
mutate registry_frame_key_export_ungated src/helpers/cli/CliRegistry.h "case FR_KEY_EXPORT: r.cls = C_SECRET;" "case FR_KEY_EXPORT: r.cls = C_READ;" sim/tests/test_postures_companion.py::test_hardened_refuses_key_export_and_import_on_usb
mutate registry_frame_key_import_ungated src/helpers/cli/CliRegistry.h "case FR_KEY_IMPORT: r.cls = C_SECRET;" "case FR_KEY_IMPORT: r.cls = C_READ;" sim/tests/test_postures_companion.py::test_hardened_refuses_key_export_and_import_on_usb
mutate registry_frame_cli_ungated src/helpers/cli/CliRegistry.h "case FR_CLI: r.cls = C_READ;" "case FR_CLI: r.cls = C_READ; r.cls = C_REBOOT;" unit:test_cli_engine
mutate registry_frame_factory_reset_is_a_write src/helpers/cli/CliRegistry.h "case FR_FACTORY_RESET: r.cls = C_DESTRUCTIVE;" "case FR_FACTORY_RESET: r.cls = C_WRITE;" unit:test_cli_engine
# the gate
mutate gate_refusal_not_audited src/helpers/cli/CliEngine.h "if (refusalAudited(v, r)) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));" "(void)now;" sim/tests/test_postures_companion.py::test_remote_cli_cannot_change_the_posture_and_it_is_audited sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited
mutate gate_refusal_not_audited_unit src/helpers/cli/CliEngine.h "if (refusalAudited(v, r)) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));" "(void)now;" unit:test_cli_engine
mutate gate_connmode_refusal_not_audited_sim src/helpers/cli/CliEngine.h "if (refusalAudited(v, r)) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));" "(void)now;" sim/tests/test_connectivity_mode.py::test_a_remote_switch_is_refused_and_audited
mutate gate_frame_refusal_not_audited src/helpers/cli/CliEngine.h "if (!v.allow) Posture::recordRefusal(now, v.caller, auditWhat(v.reason, r));" "(void)now;" sim/tests/test_postures_companion.py::test_untrusted_transport_refusals_are_audited
mutate gate_canned_reply_dropped src/helpers/cli/CliEngine.h "  if (r.canned) {" "  if (false) {" unit:test_cli_engine
mutate gate_refusal_what_is_wrong src/helpers/cli/CliEngine.h "    case R_CLI_OFF: return Posture::W_REMOTE_CLI;" "    case R_CLI_OFF: return Posture::W_REMOTE_WRITE;" sim/tests/test_postures_companion.py::test_hardened_turns_the_remote_cli_off
mutate gate_not_run_by_the_companion examples/companion_radio/MyMesh.cpp "if (Cli::gate(command, ctx, Cli::ROLE_COMPANION, getRTCClock()->getCurrentTime(), reply, 150, &outcome)) {" "if (false && Cli::gate(command, ctx, Cli::ROLE_COMPANION, getRTCClock()->getCurrentTime(), reply, 150, &outcome)) {" sim/tests/test_companion_remote_cli.py
mutate gate_not_run_by_the_front_door_unit src/helpers/Posture.h "if (Cli::gate(command, ctx, Cli::ROLE_REPEATER, now, reply, max)) return true;" "" unit:test_posture
mutate gate_default_password_session_not_recognised src/helpers/Posture.h "isDefaultAdminPassword(admin_pw, build_default) ? Cli::A_ADMIN_DEFAULT_PW : Cli::A_ADMIN" "Cli::A_ADMIN" sim/tests/test_postures_admin.py::test_remote_commands_are_refused_while_the_password_is_default_even_with_a_session
mutate frame_gate_skipped examples/companion_radio/MyMesh.cpp "if (frame != Cli::FR_NONE && !Cli::allowFrame(fctx, frame, now)) {" "if (false) {" sim/tests/test_companion_transport.py
mutate frame_context_trusts_every_interface examples/companion_radio/MyMesh.cpp "if (_serial && _serial->isTrustedTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);" "if (_serial) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);" sim/tests/test_companion_transport.py
mutate frame_context_cable_is_not_a_cable examples/companion_radio/MyMesh.cpp "if (_serial && _serial->isCableTransport()) return Cli::Context(Cli::T_USB, Cli::A_CABLE, _prefs.posture);" "if (_serial && _serial->isCableTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);" sim/tests/test_postures_companion.py::test_usb_factory_reset_recovers_a_hardened_companion
mutate contact_context_is_a_cable examples/companion_radio/MyMesh.cpp "setCliCaller(Cli::Context(Cli::T_LORA, Cli::A_CONTACT_FLAG, _prefs.posture), Posture::CALLER_REMOTE_CLI);" "setCliCaller(Cli::Context(Cli::T_USB, Cli::A_CABLE, _prefs.posture), Posture::CALLER_CABLE);" sim/tests/test_companion_remote_cli.py
# facades: the older entry points ask the table
mutate facade_key_transfer_ignores_the_table src/helpers/Posture.h "return Cli::decide(Cli::contextForCaller(CALLER_CABLE, posture), Cli::C_SECRET).allow;" "return true;" unit:test_posture
mutate facade_factory_reset_ignores_the_table src/helpers/Posture.h "return Cli::decide(Cli::contextForCaller(caller, posture), Cli::C_DESTRUCTIVE).allow;" "return true;" unit:test_posture
mutate facade_remote_read_only_list_has_writes src/helpers/Posture.h "return cls == Cli::C_READ || cls == Cli::C_REBOOT;" "return cls == Cli::C_READ || cls == Cli::C_REBOOT || cls == Cli::C_WRITE;" unit:test_posture
mutate facade_ble_can_lower src/helpers/Posture.h "bool allowed = Cli::decide(ctx, Cli::C_POSTURE, to_hardened ? 0 : Cli::F_CABLE_ONLY).allow;" "bool allowed = Cli::decide(ctx, Cli::C_POSTURE, 0).allow;" unit:test_posture
mutate facade_remote_can_change_the_posture src/helpers/Posture.h "bool allowed = Cli::decide(ctx, Cli::C_POSTURE, to_hardened ? 0 : Cli::F_CABLE_ONLY).allow;" "bool allowed = true;" unit:test_posture
mutate facade_audit_readable_remotely src/helpers/Posture.h "if (!Cli::decide(ctx, Cli::C_POSTURE).allow) {
      recordRefusal(now, caller, W_AUDIT);" "if (false) {
      recordRefusal(now, caller, W_AUDIT);" unit:test_posture
mutate facade_posture_refusal_not_audited src/helpers/AuditLog.h "inline void recordRefusal(uint32_t ts, uint8_t caller, uint8_t what) { audit().add(ts, EV_REFUSED, caller, what); }" "inline void recordRefusal(uint32_t ts, uint8_t caller, uint8_t what) { }" sim/tests/test_postures_admin.py::test_remote_admin_cannot_change_the_posture_and_it_is_audited sim/tests/test_postures_companion.py::test_untrusted_transport_refusals_are_audited
mutate facade_connmode_remote_may_switch src/helpers/connmode/ConnMode.h "return Cli::decide(Cli::contextForCaller(caller, Posture::NORMAL), Cli::C_POSTURE).allow;   // cli/CliPolicy.h" "return Cli::decide(Cli::contextForCaller(caller, Posture::NORMAL), Cli::C_POSTURE).allow || caller == Posture::CALLER_REMOTE_CLI;" unit:test_connmode
# MQTT adapter
mutate mqtt_cli_session_always_verified src/helpers/mqtt/MqttController.cpp "Cli::Mqtt::sessionContext(_core.verified(), *_posture, manages)" "Cli::Mqtt::sessionContext(true, *_posture, manages)" sim/tests/test_cli_unified.py::test_mqtt_is_refused_on_plain_and_on_unverified_tls_and_audited
mutate mqtt_cli_profile_always_manages src/helpers/mqtt/MqttController.cpp "const bool manages = cur != MqttCore::NONE && _profiles->p[cur].manage != 0;" "const bool manages = true;" sim/tests/test_cli_unified.py::test_hardened_mqtt_is_read_only_unless_the_profile_is_flagged_for_management
mutate mqtt_cli_retained_request_replayed src/helpers/mqtt/MqttController.cpp " || (int32_t)(millis() - _cli_open_at) < 0) return;" ") return;" sim/tests/test_cli_unified.py::test_a_retained_request_is_not_replayed_to_a_reconnecting_node
mutate mqtt_cli_correlation_id_not_echoed src/helpers/mqtt/MqttController.cpp "n = Cli::Mqtt::formatResponse(out, sizeof(out), req.id, status, reply);" "n = Cli::Mqtt::formatResponse(out, sizeof(out), \"\", status, reply);" sim/tests/test_cli_unified.py::test_malformed_requests_get_an_answer_and_run_nothing
mutate mqtt_cli_bad_request_runs_something src/helpers/mqtt/MqttController.cpp "if (!Cli::Mqtt::parseRequest(payload, len, req)) {" "if (false) {" sim/tests/test_cli_unified.py::test_malformed_requests_get_an_answer_and_run_nothing
mutate mqtt_cli_no_subscription src/helpers/mqtt/MqttController.cpp "  if (_cli_fn) _psc.subscribe(_req_topic);   // before the status" "  // no subscription: before the status" sim/tests/test_cli_unified.py::test_reboot_over_mqtt_runs_in_normal_and_the_node_comes_back
mutate mqtt_cli_refusal_reported_as_ok examples/companion_radio/MyMesh.cpp "if (_cli_refused) return Cli::Mqtt::S_REFUSED;" "" sim/tests/test_cli_unified.py::test_mqtt_cannot_do_what_only_a_local_caller_may
mutate mqtt_cli_flag_not_persisted examples/companion_radio/NodePrefs.h "        key[0] = 'g'; def(key, q.manage);
" "" sim/tests/test_cli_unified.py::test_the_management_flag_survives_a_reboot_and_is_off_by_default
mutate mqtt_cli_flag_settable_by_a_remote_caller_unit src/helpers/cli/CliRegistry.h "  return memcmp(q, \"manage\", 6) == 0 && (q[6] == 0 || q[6] == ' ');" "  return false;" unit:test_cli_engine
mutate mqtt_profile_manage_listing_always src/helpers/mqtt/MqttProfiles.cpp "const char* manage = q.manage ? \" manage=on\" : \"\";" "const char* manage = \" manage=off\";" sim/tests/test_cli_unified.py::test_the_management_flag_survives_a_reboot_and_is_off_by_default sim/tests/test_companion_mqtt.py::test_credentials_are_nowhere_but_the_prefs_file
mutate mqtt_profile_manage_accepts_anything src/helpers/mqtt/MqttProfiles.cpp "else snprintf(reply, cap, \"ERR: manage is on or off\");" "else { q.manage = 1; done(false, \"?\"); }" sim/tests/test_cli_unified.py::test_the_management_flag_survives_a_reboot_and_is_off_by_default
mutate mqtt_parse_accepts_duplicate_keys src/helpers/cli/CliMqtt.h "      if (have_id) return false;
" "" unit:test_cli_mqtt
mutate mqtt_parse_accepts_control_characters src/helpers/cli/CliMqtt.h "    if (ch < 0x20 || ch == 0x7f) return false;   // a raw control character" "" unit:test_cli_mqtt
mutate mqtt_parse_accepts_decoded_control_characters src/helpers/cli/CliMqtt.h "    if (ch < 0x20 || ch == 0x7f) return false;
    if (ch < 0x80) {" "    if (ch < 0x80) {" unit:test_cli_mqtt
mutate mqtt_parse_accepts_trailing_bytes src/helpers/cli/CliMqtt.h "  if (c.more()) return false;   // trailing bytes" "" unit:test_cli_mqtt
mutate mqtt_parse_id_length_unbounded src/helpers/cli/CliMqtt.h "if (n == 0 || n > (size_t)ID_MAX) return false;" "if (n == 0) return false;" unit:test_cli_mqtt
mutate mqtt_parse_id_characters_unchecked src/helpers/cli/CliMqtt.h "for (size_t i = 0; i < n; i++) if (!idChar(id[i])) return false;" "" unit:test_cli_mqtt
mutate mqtt_format_reply_not_escaped src/helpers/cli/CliMqtt.h "if (ch == '\"' || ch == '\\\\') { esc[0] = '\\\\'; esc[1] = (char)ch; el = 2; }" "if (false) { }" unit:test_cli_mqtt
mutate mqtt_format_truncation_not_flagged src/helpers/cli/CliMqtt.h "const char* tail = cut ? TAIL_CUT : TAIL_OK;" "const char* tail = TAIL_OK;" unit:test_cli_mqtt
mutate mqtt_format_cuts_inside_a_character src/helpers/cli/CliMqtt.h "if (n + k + reserve >= cap) { cut = true; break; }   // keeps room for the tail and the NUL" "if (n + 1 + reserve >= cap) { cut = true; break; } if (n + k + reserve >= cap) { k = 1; used = 1; }" unit:test_cli_mqtt
mutate mqtt_topic_is_not_per_device src/helpers/cli/CliMqtt.h "snprintf(out, cap, \"mecon/%s/cli/req\", node_id);" "snprintf(out, cap, \"mecon/cli/req\");" unit:test_cli_mqtt
# MECON commands
mutate mecon_check_always_allows src/helpers/cli/MeconCli.h "v.allow ? \"allowed\" : reasonName(v.reason)" "\"allowed\"" unit:test_cli_engine
mutate mecon_who_hides_the_principal src/helpers/cli/MeconCli.h "principalName(principalOf(ctx)), ctx.mgmt ? \" mgmt=on\" : \"\");" "\"cable\", ctx.mgmt ? \" mgmt=on\" : \"\");" unit:test_cli_engine

# atomic blobs and client ACL
mutate acl_swap_not_recovered src/helpers/ClientACL.cpp '  AtomicFile::recover(_fs, "/s_contacts");   // finish or drop a save that was cut by a power loss
' "" sim/tests/test_atomic_blobs_acl.py::test_acl_cut_between_write_and_rename
mutate acl_written_in_place src/helpers/ClientACL.cpp 'void ClientACL::save(FILESYSTEM* fs, bool (*filter)(ClientInfo*)) {
  _fs = fs;
' '#define AtomicFile ClientInPlace
namespace ClientInPlace {
  template <typename F, typename W> inline bool save(F* fs, const char* p, W w) { File f = fs->open(p, "w", true); if (!f) return false; bool ok = w(f); f.close(); return ok; }
  template <typename F> inline void discard(F* fs, const char* p) { fs->remove(p); }
}
void ClientACL::save(FILESYSTEM* fs, bool (*filter)(ClientInfo*)) {
  _fs = fs;
' sim/tests/test_atomic_blobs_acl.py::test_acl_cut_in_temp_file_keeps_logged_in_client sim/tests/test_atomic_blobs_acl.py::test_acl_is_never_written_in_place
mutate blob_swap_not_recovered examples/companion_radio/DataStore.cpp '  AtomicFile::recover(_fs, path);   // finish or drop a save that was cut by a power loss
  if (_fs->exists(path)) {' '  if (_fs->exists(path)) {' sim/tests/test_atomic_blobs_acl.py::test_blob_cut_between_write_and_rename
mutate blob_written_in_place examples/companion_radio/DataStore.cpp '  return AtomicFile::save(_fs, path, [src_buf, len](Stream& s) { return s.write(src_buf, len) == len; });' '  File f = openWrite(_fs, path); if (!f) return false; int n = f.write(src_buf, len); f.close(); return n == len;' sim/tests/test_atomic_blobs_acl.py::test_blob_cut_in_temp_file_keeps_previous_blob sim/tests/test_atomic_blobs_acl.py::test_blob_is_never_written_in_place

mutate mem_unknown_printed_as_measured src/helpers/cli/MeconCli.h "if (!m.known) snprintf" "if (false) snprintf" unit:test_cli_engine
mutate mem_loop_stack_not_reported src/helpers/cli/MeconCli.h "    if (m.loop_stack_free && n > 0 && (size_t)n < max)" "    if (false)" unit:test_heap_floor

# firmware identity (version string)
mutate ver_dev_suffix_dropped src/helpers/MeconVersion.h '"+%.*s.%.*s"' '"%.0s%.0s"' unit:test_mecon_version
mutate ver_dirty_unmarked src/helpers/MeconVersion.h 'if (dirty) strncat' 'if (false) strncat' unit:test_mecon_version
mutate ver_unknown_not_shortened src/helpers/MeconVersion.h 'strcmp(tag, "unknown") == 0 ? "unk" : tag' 'tag' unit:test_mecon_version
mutate ver_frame_back_to_stock examples/companion_radio/MyMesh.h '#define FIRMWARE_VERSION mecon::versionShort()' '#define FIRMWARE_VERSION "v1.18.0"' sim/tests/test_mecon_version.py::test_companion_device_info_frame_carries_the_mecon_version_and_the_unchanged_protocol_level
mutate ver_protocol_level_changed examples/companion_radio/MyMesh.h '#define FIRMWARE_VER_CODE 14' '#define FIRMWARE_VER_CODE 15' sim/tests/test_mecon_version.py::test_companion_device_info_frame_carries_the_mecon_version_and_the_unchanged_protocol_level
mutate ver_companion_cli_stock examples/companion_radio/MyMesh.cpp 'mecon::versionFull(), FIRMWARE_BUILD_DATE' '"v1.18.0", FIRMWARE_BUILD_DATE' sim/tests/test_mecon_version.py::test_companion_ver_and_mecon_version_over_the_cli_frame
mutate ver_repeater_cli_stock examples/simple_repeater/MyMesh.h 'getFirmwareVer() override { return mecon::versionFull(); }' 'getFirmwareVer() override { return "v1.18.0"; }' sim/tests/test_mecon_version.py::test_repeater_and_room_server_ver_and_mecon_version
mutate ver_room_cli_stock examples/simple_room_server/MyMesh.h 'getFirmwareVer() override { return mecon::versionFull(); }' 'getFirmwareVer() override { return "v1.18.0"; }' sim/tests/test_mecon_version.py::test_repeater_and_room_server_ver_and_mecon_version
mutate ver_get_command_missing src/helpers/cli/MeconCli.h 'strcmp(cmd, "get mecon.version") == 0' 'strcmp(cmd, "get mecon.versionx") == 0' sim/tests/test_mecon_version.py::test_companion_ver_and_mecon_version_over_the_cli_frame
mutate ver_script_ignores_dirty bin/mecon_version.py '"--match", "v[0-9]*", "--dirty"]' '"--match", "v[0-9]*"]' sim/tests/test_mecon_version.py::test_build_script_defines_the_describe_string_for_tag_dev_dirty_and_no_git
mutate ver_script_any_tag bin/mecon_version.py '"--match", "v[0-9]*"' '"--match", "*"' sim/tests/test_mecon_version.py::test_build_script_defines_the_describe_string_for_tag_dev_dirty_and_no_git

mutate mem_psram_not_reported src/helpers/cli/MeconCli.h 'if (m.psram && n > 0' 'if (false && n > 0' unit:test_heap_floor
mutate boot_get_command_missing src/helpers/cli/MeconCli.h 'strcmp(cmd, "get mecon.boot") == 0' 'strcmp(cmd, "get mecon.bootx") == 0' sim/tests/test_boot_times.py
mutate boot_companion_setup_unmarked examples/companion_radio/main.cpp 'mecon::boot::mark(mecon::boot::SETUP, millis());' '/* unmarked */' sim/tests/test_boot_times.py::test_companion_reports_store_mesh_and_setup_in_order
mutate boot_repeater_mesh_unmarked examples/simple_repeater/main.cpp 'mecon::boot::mark(mecon::boot::MESH, millis());' '/* unmarked */' sim/tests/test_boot_times.py::test_repeater_and_room_server_report_mesh_and_setup

# boot looks up no file that is not there (#43): one listing at boot answers the lookups
mutate boot_presence_never_listed examples/companion_radio/main.cpp '  BootPresence::begin(SPIFFS);   // one listing' '  // BootPresence::begin(SPIFFS);   // one listing' $CT_SIM::test_boot_looks_up_no_file_that_is_not_there
mutate boot_presence_recover_ignores_listing src/helpers/AtomicFile.h '  if (BootPresence::mayExist(tmp) && fs->exists(tmp)) fs->remove(tmp);   // never verified' '  if (fs->exists(tmp)) fs->remove(tmp);   // never verified' $CT_SIM::test_boot_looks_up_no_file_that_is_not_there
mutate boot_presence_says_absent_for_everything src/helpers/BootPresence.h '  if (!s.active) return true;
  const char* b = baseName(path);' '  if (!s.active) return true;
  return false;
  const char* b = baseName(path);' $CT_SIM::test_a_swap_left_by_a_power_cut_in_a_high_bucket_is_recovered_at_boot

# the CLI reference page is checked against the registry and the MECON handlers
mutate cli_reference_registry_row_misspelt docs/CLI_REFERENCE.md '| `neighbors` | List nearby repeaters heard.' '| `neighbours` | List nearby repeaters heard.' sim/tests/test_cli_reference_sync.py
mutate cli_reference_mecon_command_renamed src/helpers/cli/MeconCli.h 'strcmp(cmd, "get mecon.mem") == 0' 'strcmp(cmd, "get mecon.memx") == 0' sim/tests/test_cli_reference_sync.py

# the Wi-Fi controller's own scan: bounded, stopped in the driver, and a failed scan still joins directly
WIFI_CTL=src/helpers/wifi/WifiController.cpp
mutate wifi_failed_scan_not_joined_directly $WIFI_CORE "  if (pick == NONE) { enterBackoff(now); return; }
  connectTo(now, pick);
  _blind = true;" "  enterBackoff(now);
  return;" unit:test_wifi_core
mutate wifi_failed_scan_not_joined_directly_sim $WIFI_CORE "  if (pick == NONE) { enterBackoff(now); return; }
  connectTo(now, pick);
  _blind = true;" "  enterBackoff(now);
  return;" $WIFI_SIM::test_a_scan_that_never_ends_is_stopped_and_the_best_profile_is_joined_directly
mutate wifi_failed_scan_ignores_priority $WIFI_CORE "    bool higher = pick == NONE || _profiles->priority(i) > _profiles->priority(pick);   // ties keep the lower number" "    bool higher = pick == NONE;" unit:test_wifi_core
mutate wifi_blind_miss_not_cooled $WIFI_CORE "      if (_blind) startCooldown(i, now, _t.fail_cooldown_ms);   // a guess that missed: let the next attempt guess another" "" unit:test_wifi_core
mutate wifi_failed_upgrade_scan_guesses $WIFI_CORE "  if (_scan_upgrade) {
    _scan_upgrade = false;
    return;
  }
  if (_state != WifiState::Scanning) return;" "  _scan_upgrade = false;" unit:test_wifi_core
mutate wifi_silent_scan_taken_for_empty $WIFI_CORE "        scanFailed(now);   // a scan that never answers: nothing is known, so try the best profile directly" "        scanDone(now);" unit:test_wifi_core
mutate wifi_join_drop_counted_at_once $WIFI_CORE '    if (!_drop_pending) _drop_at = now;' '    if (!_drop_pending) _drop_at = now - _t.join_drop_settle_ms;' unit:test_wifi_core
mutate wifi_scan_not_stopped_in_the_driver $WIFI_CTL "  _scanning = false;
  esp_wifi_scan_stop();
}" "  _scanning = false;
}" $WIFI_SIM::test_the_next_scan_works_after_a_stopped_one
mutate wifi_scan_not_stopped_in_the_driver_first $WIFI_CTL "  _scanning = false;
  esp_wifi_scan_stop();
}" "  _scanning = false;
}" $WIFI_SIM::test_a_scan_that_never_ends_is_stopped_and_the_best_profile_is_joined_directly
mutate wifi_scan_not_bounded $WIFI_CTL "const uint32_t SCAN_BOUND_MS = 5000;" "const uint32_t SCAN_BOUND_MS = 5000000;" $WIFI_SIM::test_a_wedged_scan_does_not_stall_the_main_loop
mutate wifi_scan_timeout_taken_for_empty $WIFI_CTL '    WIFICTL_LOG("scan did not report");
    scanAbort();
    _core.scanFailed(now);' '    WIFICTL_LOG("scan did not report");
    scanAbort();
    _core.scanDone(now);' $WIFI_SIM::test_a_scan_that_never_ends_is_stopped_and_the_best_profile_is_joined_directly
mutate wifi_arduino_table_not_read $WIFI_CTL "int n = WiFi.scanComplete();   // Arduino's handler, when it ran, read the driver's list into its own table" "int n = -1;" $WIFI_SIM::test_joins_the_highest_priority_visible_profile
mutate wifi_driver_list_not_read $WIFI_CTL "if (cnt && esp_wifi_scan_get_ap_records(&cnt, rec) != ESP_OK) cnt = 0;" "cnt = 0;" $WIFI_SIM::test_results_are_read_from_the_driver_when_arduino_never_takes_the_event
mutate wifi_scan_holds_the_main_loop $WIFI_CTL "  _scanning = true;
}

void WifiController::scanAbort() {
  _scan_armed = false;" "  _scanning = true;
  while (!_ev_scan_done && (uint32_t)(millis() - now) < SCAN_BOUND_MS) delay(10);
}

void WifiController::scanAbort() {
  _scan_armed = false;" $WIFI_SIM::test_a_wedged_scan_does_not_stall_the_main_loop
# The Ethernet console is the untrusted/remote-admin caller, never timestamp 0 (the cable)
mutate eth_console_cable_repeater examples/simple_repeater/main.cpp "Cli::netConsoleRun(ethernet_console, the_mesh, ADMIN_PASSWORD, rtc_clock.getCurrentTime(), ethernet_command, reply," "the_mesh.handleCommand(0, ethernet_command, reply); if (0) Cli::netConsoleRun(ethernet_console, the_mesh, ADMIN_PASSWORD, rtc_clock.getCurrentTime(), ethernet_command, reply," sim/tests/test_ethernet_console.py
mutate eth_console_cable_room examples/simple_room_server/main.cpp "Cli::netConsoleRun(ethernet_console, the_mesh, ADMIN_PASSWORD, rtc_clock.getCurrentTime(), ethernet_command, reply," "the_mesh.handleCommand(0, ethernet_command, reply); if (0) Cli::netConsoleRun(ethernet_console, the_mesh, ADMIN_PASSWORD, rtc_clock.getCurrentTime(), ethernet_command, reply," sim/tests/test_ethernet_console.py
mutate eth_console_runs_as_timestamp_zero src/helpers/cli/NetConsole.h "mesh.handleCommand(now != 0 ? now : 1, line, reply);" "mesh.handleCommand(0, line, reply);" sim/tests/test_ethernet_console.py
mutate eth_console_unauthenticated_is_cable src/helpers/cli/CliPolicy.h "case T_NETCON: return c.auth == A_NETCON_ADMIN ? P_NETCON_ADMIN : P_UNTRUSTED;" "case T_NETCON: return c.auth == A_NETCON_ADMIN ? P_NETCON_ADMIN : P_CABLE;" sim/tests/test_ethernet_console.py
mutate eth_console_login_in_hardened src/helpers/cli/NetConsole.h "    if (posture != 0) {
      Posture::recordRefusal" "    if (false) {
      Posture::recordRefusal" sim/tests/test_ethernet_console.py
mutate eth_console_login_with_default_password src/helpers/cli/NetConsole.h "if (is_default || !match) {" "if (!match) {" sim/tests/test_ethernet_console.py
mutate eth_console_session_survives_reconnect variants/sim/SimEthernetCLI.h "    ethernet_console.endSession();
    sim_eth_send(ETHERNET_CLI_BANNER" "    sim_eth_send(ETHERNET_CLI_BANNER" sim/tests/test_ethernet_console.py

# --- the enforced heap floor (docs/CONNECTIVITY_MODE.md): undo each rule and its tests must go red
mutate heap_floor_total_free_not_largest src/helpers/HeapFloor.h "if (m.largest < _floor) {" "if (m.free < _floor) {" unit:test_heap_floor
mutate heap_floor_total_free_not_largest_sim src/helpers/HeapFloor.h "if (m.largest < _floor) {" "if (m.free < _floor) {" sim/tests/test_heap_floor.py::test_the_figure_is_the_largest_block_not_the_total_free_heap
mutate heap_floor_no_hysteresis src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (m.largest >= _floor) {" unit:test_heap_floor
mutate heap_floor_no_hysteresis_sim src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (m.largest >= _floor) {" sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_no_audit_entry src/helpers/HeapFloor.h "audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR);" "(void)ts;" unit:test_heap_floor
mutate heap_floor_no_audit_entry_sim src/helpers/HeapFloor.h "audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR);" "(void)ts;" sim/tests/test_heap_floor.py::test_one_audit_entry_per_episode_however_long_it_lasts
mutate heap_floor_audit_on_every_look src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR), m.largest >= _floor + _hyst) {" unit:test_heap_floor
mutate heap_floor_audit_on_every_look_sim src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR), m.largest >= _floor + _hyst) {" sim/tests/test_heap_floor.py::test_one_audit_entry_per_episode_however_long_it_lasts
mutate heap_floor_first_look_a_period_late src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled() && (uint32_t)(now_ms - _last) >= _period; }" unit:test_heap_floor
mutate heap_floor_first_look_a_period_late_sim src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled() && (uint32_t)(now_ms - _last) >= _period; }" sim/tests/test_heap_floor.py::test_the_first_look_comes_right_after_setup_not_a_minute_later
mutate heap_floor_period_ignored src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled(); }" unit:test_heap_floor
mutate heap_floor_unmeasurable_fires src/helpers/HeapFloor.h "if (!enabled() || !m.known) return false;" "if (!enabled()) return false;" unit:test_heap_floor
mutate heap_floor_default_value_changed src/helpers/HeapFloor.h "#define HEAP_FLOOR_BYTES 20480 " "#define HEAP_FLOOR_BYTES 10240 " sim/tests/test_heap_floor.py::test_a_healthy_heap_reports_no_low_state_and_the_default_floor
mutate heap_floor_flag_not_reported src/helpers/cli/MeconCli.h "floor.low() ? 1 : 0," "0," unit:test_heap_floor
mutate heap_floor_flag_not_reported_sim src/helpers/cli/MeconCli.h "floor.low() ? 1 : 0," "0," sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_value_not_reported src/helpers/cli/MeconCli.h "(unsigned long)floor.floorBytes());" "0UL);" unit:test_heap_floor
mutate heap_floor_marker_dropped src/helpers/connmode/ConnStatus.h "  if (!s.heap_low) return;" "  return;" unit:test_heap_floor
mutate heap_floor_marker_dropped_sim src/helpers/connmode/ConnStatus.h "  if (!s.heap_low) return;" "  return;" sim/tests/test_heap_floor.py::test_the_marker_is_on_the_home_status_line_only_while_low
mutate heap_floor_marker_overflows_the_line src/helpers/connmode/ConnStatus.h "if (len + 1 + (sizeof(MARK) - 1) <= HOME_COLUMNS &&" "if (len + 1 + (sizeof(MARK) - 1) <= 99 &&" unit:test_heap_floor
mutate heap_floor_marker_not_filled_in examples/companion_radio/ui-new/ConnStatusScreen.h "  s.heap_low = heapFloor().low();
" "" sim/tests/test_heap_floor.py::test_the_marker_is_on_the_home_status_line_only_while_low
mutate heap_floor_not_run_from_the_loop examples/companion_radio/main.cpp "  heapFloor().loop(millis(), rtc_clock.getCurrentTime());
" "" sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_not_started examples/companion_radio/main.cpp "heapFloor().begin(HEAP_FLOOR_BYTES);" "heapFloor().begin(0);" sim/tests/test_heap_floor.py::test_a_healthy_heap_reports_no_low_state_and_the_default_floor


# --- the simulated BLE transport: each BLE rule that was unit-only, broken end to end (docs/SIMULATION.md)
mutate ble_wrong_pin_accepted variants/sim/sim_ble.h "&& (uint32_t)given == _pin) {" "&& true) {" sim/tests/test_companion_ble.py::test_a_wrong_pin_is_disconnected_and_nothing_is_carried
mutate ble_paired_on_connect variants/sim/sim_ble.h "_client = c; _paired = false; _line_len = 0; _state = IDLE;" "_client = c; _paired = true; _line_len = 0; _state = IDLE;" sim/tests/test_companion_ble.py::test_frames_before_pairing_are_not_accepted
mutate ble_data_before_pairing_waits_for_a_line variants/sim/sim_ble.h "if (_line_len == 0 && ch != 'P') { reject(\"pairing rejected: data before pairing\"); return; }" "" sim/tests/test_companion_ble.py::test_frames_before_pairing_are_not_accepted
mutate ble_default_pin_is_trusted variants/sim/sim_ble.h "return _pin != 0 && _pin != 123456; }   // as the real class" "return _pin != 0; }" sim/tests/test_companion_ble.py::test_with_the_default_pin_management_is_refused_and_messaging_works
mutate ble_every_pin_is_trusted variants/sim/sim_ble.h "return _pin != 0 && _pin != 123456; }   // as the real class" "return true; }" sim/tests/test_companion_ble.py::test_with_the_default_pin_management_is_refused_and_messaging_works
mutate ble_link_survives_disable variants/sim/sim_ble.h "    dropClient(\"disabled: link dropped\");
" "" sim/tests/test_companion_ble.py::test_the_bluetooth_page_switches_the_link_off_and_on
mutate posture_ble_default_pin_starts_sim src/helpers/Posture.h "return !isHardened(posture) || (active_pin != 0 && active_pin != 123456);" "return true;" sim/tests/test_companion_ble.py::test_hardened_with_the_default_pin_does_not_start_ble
mutate connmode_ble_started_in_wifi_mode_sim examples/companion_radio/main.cpp "if (!ConnMode::bleMayStart(conn_mode)) {" "if (false) {" sim/tests/test_companion_ble.py::test_switching_to_wifi_mode_takes_ble_down_and_a_reboot_does_not_bring_it_back
mutate connmode_ble_may_always_start_sim src/helpers/connmode/ConnMode.h "inline bool bleMayStart(uint8_t mode) { return sanitize(mode) == BLE; }" "inline bool bleMayStart(uint8_t mode) { return true; }" sim/tests/test_companion_ble.py::test_switching_to_wifi_mode_takes_ble_down_and_a_reboot_does_not_bring_it_back
mutate table_paired_key_in_hardened_sim src/helpers/cli/CliPolicy.h "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           NOT_PERMITTED,   ALLOW) }," "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW) }," sim/tests/test_companion_ble.py::test_hardened_with_a_per_device_pin_starts_ble_but_keeps_the_rules
mutate table_paired_factory_reset_in_hardened_sim src/helpers/cli/CliPolicy.h "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           NOT_PERMITTED,   ALLOW) }," "/* paired BLE     H */   CLI_ROW(ALLOW,           ALLOW,           KEY_HARDENED,    ALLOW,           ALLOW,           ALLOW) }," sim/tests/test_companion_ble.py::test_hardened_with_a_per_device_pin_starts_ble_but_keeps_the_rules
mutate policy_flag_ignored_for_the_lowering_rule_sim src/helpers/cli/CliPolicy.h "if (r == R_ALLOW && (flags & F_CABLE_ONLY) && p != P_CABLE && p != P_SIGNED) r = R_NOT_PERMITTED;" "if (false) r = R_NOT_PERMITTED;" sim/tests/test_companion_ble.py::test_hardened_with_a_per_device_pin_starts_ble_but_keeps_the_rules
mutate frame_context_trusts_every_interface_sim examples/companion_radio/MyMesh.cpp "if (_serial && _serial->isTrustedTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);" "if (_serial) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_PIN, _prefs.posture);" sim/tests/test_companion_ble.py::test_with_the_default_pin_management_is_refused_and_messaging_works

# --- a reply goes back to the interface its request came from, and that interface's trust decides the command
mutate reply_routed_to_every_interface src/helpers/MultiSerialInterface.h "if(_bound != nullptr && iface.instance != _bound){" "if(false){" sim/tests/test_companion_ble.py::test_interleaved_requests_are_answered_on_their_own_transport
mutate reply_and_trust_from_the_wrong_interface src/helpers/MultiSerialInterface.h "          _last_rx = iface.instance;" "          _last_rx = _last_rx ? _last_rx : iface.instance;" sim/tests/test_companion_ble.py::test_a_default_pin_ble_frame_is_never_decided_with_the_cables_trust
mutate reply_bound_to_the_whole_loop_pass_only src/helpers/MultiSerialInterface.h "  void bindReply(const BaseSerialInterface* src) override { _bound = src; }" "  void bindReply(const BaseSerialInterface* src) override { (void)src; }" sim/tests/test_companion_ble.py::test_interleaved_requests_are_answered_on_their_own_transport

if [ -z "$ONLY" ] && [ "$DRY" = 0 ]; then build; fi   # restore the binaries after a full run

# audit caller of a default-PIN BLE refusal (#53)
mutate audit_default_pin_ble_recorded_as_net src/helpers/cli/CliPolicy.h "v.caller = (p == P_UNTRUSTED && c.transport == T_BLE) ? Posture::CALLER_BLE : auditCaller(p);" "v.caller = auditCaller(p);" unit:test_cli_engine
mutate audit_default_pin_ble_frame_context_is_tcp examples/companion_radio/MyMesh.cpp "if (_serial && _serial->isBleTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_DEFAULT_PIN, _prefs.posture);" "" sim/tests/test_companion_ble.py
mutate audit_multi_interface_ble_not_forwarded src/helpers/MultiSerialInterface.h "return _bound != nullptr && _bound->isBleTransport();" "return false;" sim/tests/test_companion_ble.py
# duty-cycle floor for the KISS modem, sensor and secure-chat roles (#20)
mutate duty_kiss_sends_without_the_gate examples/kiss_modem/KissModem.cpp "if (!dutyAllowsSend(_tx_est_ms)) {" "if (false) {" unit:test_kiss_modem
mutate duty_kiss_unknown_channel_sends examples/kiss_modem/KissModem.cpp "if (!(_chan_freq_mhz > 0)) { _duty_refuse = true; return false; }" "if (!(_chan_freq_mhz > 0)) { return true; }" unit:test_kiss_modem
mutate duty_kiss_airtime_never_recorded examples/kiss_modem/KissModem.cpp "  _duty.record(millis(), took > _tx_est_ms ? took : _tx_est_ms);" "" unit:test_kiss_modem
mutate duty_kiss_set_radio_keeps_old_channel examples/kiss_modem/KissModem.cpp "  _chan_freq_mhz = _config.freq_hz / 1000000.0f;   // the floor follows the radio at once" "" unit:test_kiss_modem
mutate duty_kiss_floor_ignored examples/kiss_modem/KissModem.cpp "uint32_t bp = regulatory::dutyFloor(_chan_freq_mhz, _chan_bw_khz).limit_bp;" "uint32_t bp = 10000;" unit:test_kiss_modem
mutate duty_kiss_oversized_frame_held_forever examples/kiss_modem/KissModem.cpp "  if (est_ms > limit) { _duty_refuse = true; return false; }" "" unit:test_kiss_modem
mutate duty_sensor_channel_not_reported examples/simple_sensor/SensorMesh.h "return radio_freq > 0;" "return false;" sim/tests/test_duty_roles.py
mutate duty_sensor_temp_radio_not_tracked examples/simple_sensor/SensorMesh.cpp "    radio_freq = pending_freq; radio_bw = pending_bw;" "" sim/tests/test_duty_roles.py
mutate duty_secure_chat_channel_not_reported examples/simple_secure_chat/main.cpp "return _radio_freq > 0;" "return false;" sim/tests/test_duty_roles.py
mutate duty_secure_chat_channel_not_noted examples/simple_secure_chat/main.cpp "  the_mesh.noteRadioApplied(the_mesh.getFreqPref(), LORA_BW);" "" sim/tests/test_duty_roles.py
# lock-step virtual time (Issue #65): a node that reads the wall clock under --lockstep is not repeatable
mutate lockstep_node_reads_the_wall_clock variants/sim/sim_radio.cpp "  uint64_t nowUs() override { return g_ls_now - g_ls_join; }" "  uint64_t nowUs() override { return sim_real_clock()->nowUs(); }" sim/tests/test_lockstep.py::test_the_same_seeds_give_the_same_transmissions
# --- the Companion's mesh path (sim/tests/test_mesh_protocol.py, #63): ACK, path learning, advert auto-add
mutate mesh_ack_not_sent_in_the_returned_path src/helpers/BaseChatMesh.cpp "PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 6);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);" "PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 6);" sim/tests/test_mesh_protocol.py::test_direct_message_is_acknowledged_flood_and_direct
mutate mesh_direct_ack_not_sent src/helpers/BaseChatMesh.cpp "    if (a2) sendDirect(a2, dest.out_path, dest.out_path_len, d);" "" sim/tests/test_mesh_protocol.py::test_direct_message_is_acknowledged_flood_and_direct
mutate mesh_path_not_stored src/helpers/BaseChatMesh.cpp "  from.out_path_len = mesh::Packet::copyPath(from.out_path, out_path, out_path_len);  // store a copy of path, for sendDirect()" "  from.out_path_len = OUT_PATH_UNKNOWN;" sim/tests/test_mesh_protocol.py::test_path_is_learned_from_the_first_flood_and_the_next_message_goes_direct
mutate mesh_autoadd_flag_ignored_always_add examples/companion_radio/MyMesh.cpp "  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;" "  if (true) {
    return true;" sim/tests/test_mesh_protocol.py::test_heard_advert_is_only_reported_when_auto_add_is_off
mutate mesh_autoadd_flag_ignored_never_add examples/companion_radio/MyMesh.cpp "  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;" "  if (false) {
    return true;" sim/tests/test_mesh_protocol.py::test_heard_advert_is_added_as_a_contact_when_auto_add_is_on
# lock-step (Issue #65 phase 4): a node whose clock picks up a few ms of the wall clock still runs and transmits, so the
# precondition holds and only the identical-tx_log comparison can catch it (its timers fire at wall-dependent quanta)
mutate lockstep_node_clock_jitters_with_the_wall_clock variants/sim/sim_radio.cpp "  uint64_t nowUs() override { return g_ls_now - g_ls_join; }" "  uint64_t nowUs() override { return g_ls_now - g_ls_join + sim_real_clock()->nowUs() % 5000; }" sim/tests/test_lockstep.py::test_the_same_seeds_give_the_same_transmissions
# a contact save looks up no file that is not there (#76)
mutate save_preclean_lookups_back src/helpers/AtomicFile.h '#ifndef ATOMIC_FILE_TWO_STEP_SWAP
  if (fs->exists(tmp)) fs->remove(tmp);' '#if 1
  if (fs->exists(tmp)) fs->remove(tmp);' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate save_reads_absent_bucket src/helpers/ContactStore.cpp '  bool have = !knownAbsent(b) && _be->readBucket(' '  bool have = _be->readBucket(' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate save_new_bucket_hint_dropped src/helpers/ContactStore.cpp '_scratch, BUCKET_BYTES, knownAbsent(b))) return false;' '_scratch, BUCKET_BYTES, false)) return false;' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate bucket_map_not_set_on_write src/helpers/ContactStore.cpp '  if (_bpresent && b < _bpresent_bits) _bpresent[b >> 3] |= (uint8_t)(1u << (b & 7));' '' $CT_SIM
# --- the ether's capture effect and channel-activity notice (Issue #64; sim/ether.py, sim/tests/test_ether_physics.py)
EP=sim/tests/test_ether_physics.py
mutate capture_later_packet_never_wins sim/ether.py 'if lead >= self.capture_db and start < other["lock_us"]:' 'if False:' $EP
mutate capture_earlier_packet_never_wins sim/ether.py 'elif -lead >= self.capture_db:' 'elif False:' $EP
mutate capture_equal_strength_survives sim/ether.py 'elif -lead >= self.capture_db:' 'elif -lead >= 0:' $EP
mutate capture_just_below_threshold_survives sim/ether.py 'elif -lead >= self.capture_db:' 'elif -lead >= self.capture_db - 0.2:' $EP
mutate capture_late_packet_wins_after_the_lock sim/ether.py 'if lead >= self.capture_db and start < other["lock_us"]:' 'if lead >= self.capture_db:' $EP
mutate capture_lock_window_is_the_whole_preamble sim/ether.py 'return max(preamble_symbols(sf) - LOCK_SYMBOLS, 0) * (1 << sf) / bw_khz * 1000.0' 'return preamble_symbols(sf) * (1 << sf) / bw_khz * 1000.0' $EP
mutate busy_notice_not_sent sim/ether.py '            self._send(dst, "B", struct.pack("<I", int(toa)))' '            pass' $EP
mutate busy_notice_not_sent_lockstep sim/ether.py '            self._send(dst, "B", struct.pack("<I", int(toa)))' '            pass' sim/tests/test_lockstep.py::test_a_node_defers_its_transmission_while_a_neighbour_transmits
mutate radio_never_reports_busy variants/sim/sim_radio.cpp 'bool SimRadio::isReceiving() { loop(); return sim_clock()->nowUs() < _i->busy_until_us; }' 'bool SimRadio::isReceiving() { loop(); return false; }' sim/tests/test_lockstep.py::test_a_node_defers_its_transmission_while_a_neighbour_transmits
# nodes booted together do not share duty-window bin edges (#79)
mutate duty_phase_always_zero src/Mesh.cpp '  return _rng->nextInt(0, DUTY_BIN_MS);' '  return 0;' sim/tests/test_lockstep.py::test_nodes_booted_together_do_not_release_on_the_same_bin_edge
mutate duty_phase_not_applied src/Dispatcher.cpp '  duty.reset(_ms->getMillis() - (getDutyPhaseMs() % DUTY_BIN_MS));' '  duty.reset(_ms->getMillis());' sim/tests/test_lockstep.py::test_nodes_booted_together_do_not_release_on_the_same_bin_edge
# --- device interface core (docs/contract/DEVICE_INTERFACE.md): undo each rule and test_iface must go red
mutate iface_auth_refused_handler_still_runs src/helpers/iface/IfaceDispatch.cpp "else if (!_auth || !_auth(_auth_user, *op, req)) {" "else if (false) {" unit:test_iface
mutate iface_dedupe_cache_returns_nothing src/helpers/iface/IfaceCache.h "if (!_e[i].used || strcmp(_e[i].id, id) != 0) continue;" "if (true) continue;" unit:test_iface
mutate iface_reassembly_timeout_ignored src/helpers/iface/IfaceFrames.cpp "if (_active && (uint32_t)(now_ms - _start) >= REASSEMBLY_TIMEOUT_MS)" "if (false)" unit:test_iface
mutate iface_id_charset_unchecked src/helpers/iface/IfaceEnvelope.cpp "req.id, sizeof(req.id), CS_ID, 1)" "req.id, sizeof(req.id), CS_ANY, 1)" unit:test_iface
mutate iface_duplicate_keys_accepted src/helpers/iface/IfaceJson.cpp "if (keyEq(d, keys[j], c)) return J_DUP_KEY;" "if (false) return J_DUP_KEY;" unit:test_iface
mutate iface_new_xid_does_not_discard src/helpers/iface/IfaceFrames.cpp "if (_active && xid != _xid) drop();   // one message at a time: the newer one wins" "" unit:test_iface
mutate iface_events_take_the_result_slot src/helpers/iface/IfaceFrames.cpp '    if (_count >= QUEUE_MESSAGES - 1 ||' '    if (_count >= QUEUE_MESSAGES ||' unit:test_iface
mutate iface_reads_are_cached src/helpers/iface/IfaceDispatch.cpp '  if (n > 0 && cache && op && op->mutating && (st == ST_OK || st == ST_FAILED))' '  if (n > 0 && cache && op && (st == ST_OK || st == ST_FAILED))' unit:test_iface
mutate iface_dropped_message_keeps_its_buffer src/helpers/iface/IfaceFrames.h ' _mask = 0; _have_last = false; release(); }' ' _mask = 0; _have_last = false; }' unit:test_iface
mutate iface_duplicate_skips_the_authority_check src/helpers/iface/IfaceDispatch.cpp '      if (dop && (!_auth || !_auth(_auth_user, *dop, req)))' '      if (false)' unit:test_iface
mutate iface_expired_ids_forgotten src/helpers/iface/IfaceCache.h '      if (_h[i] == h) return EXPIRED;' '      if (false) return EXPIRED;' unit:test_iface
mutate iface_auth_ignores_the_caller examples/companion_radio/MeconIface.cpp '  return Cli::decide(self->_ctx, cls).allow;' '  return true;' sim/tests/test_device_interface.py::test_the_default_pin_is_refused_like_its_cli_line
mutate iface_reply_not_bound_to_its_transport examples/companion_radio/MyMesh.h '    _serial->bindReply(src);
    _serial->writeFrame(frame, len);' '    _serial->bindReply(nullptr);
    _serial->writeFrame(frame, len);' sim/tests/test_device_interface.py::test_a_result_goes_only_to_the_transport_that_asked
mutate iface_epoch_not_counted src/helpers/iface/DeviceMeta.h '  e++;' '' sim/tests/test_device_interface.py::test_device_id_is_kept_and_epoch_counts_boots
mutate iface_hello_without_node_key examples/companion_radio/MeconIface.cpp '  w.key("node_key"); w.str(key);' '' sim/tests/test_device_interface.py::test_every_request_fixture_against_the_device
mutate iface_config_line_check_ignores_the_caller examples/companion_radio/MeconIface.cpp '  return Cli::decide(static_cast<MeconIface*>(user)->_ctx, r.cls, r.flags).allow;' '  return true;' sim/tests/test_device_interface.py::test_hello_and_config_over_a_verified_broker
mutate iface_cli_line_never_remote examples/companion_radio/MeconIface.cpp '  const bool known = mesh->handleCommand(line, self->_ctx.remote ? 1 : 0, buf);' '  const bool known = mesh->handleCommand(line, 0, buf);' sim/tests/test_device_interface.py::test_hello_and_config_over_a_verified_broker
mutate iface_mqtt_request_topic_not_subscribed src/helpers/mqtt/MqttController.cpp '  if (_iface_fn) _psc.subscribe(_iface_req);' '' sim/tests/test_device_interface.py::test_hello_and_config_over_a_verified_broker
mutate iface_subscription_never_lapses src/helpers/iface/Streams.cpp '      if (sub.active && (int32_t)(now - sub.until) >= 0) sub.active = false;' '' sim/tests/test_device_interface.py::test_a_subscription_lapses_at_its_lifetime
mutate iface_sub_stop_does_not_stop src/helpers/iface/Streams.cpp '  if (set) set->s[s].active = false;' '' sim/tests/test_device_interface.py::test_health_events_follow_a_subscription_and_stop_with_it
mutate iface_mqtt_events_not_published examples/companion_radio/MeconIface.cpp '    if (_mqtt_subs.s[s].active && _ev_publish) _ev_publish(STREAMS[s].name, ev, (size_t)n);' '' sim/tests/test_device_interface.py::test_health_events_over_mqtt
mutate config_apply_skips_the_authority_check src/helpers/iface/IfaceConfig.cpp '    if (!allowed) { c.error = keyError("refused", key); return ST_REFUSED; }' '' unit:test_iface_config
mutate config_index_unbounded src/helpers/iface/IfaceConfig.cpp 'if (matches(table[i].key, key, len, index) && *index <= table[i].idx_max) return &table[i];' 'if (matches(table[i].key, key, len, index)) return &table[i];' unit:test_iface_config
mutate config_leading_zero_accepted src/helpers/iface/IfaceConfig.cpp '  if (dn > 3 || (dn > 1 && d[0] == ' '  if (dn > 3 || (false && d[0] == ' unit:test_iface_config
mutate config_empty_profile_not_null src/helpers/iface/IfaceConfig.cpp '    *is_null = !(field == F_WIFI_SSID' '    *is_null = false && !(field == F_WIFI_SSID' unit:test_iface_config
mutate config_radio_format_unchecked src/helpers/iface/IfaceConfig.cpp '      if (k.format && !checkFormat(k.format, out)) {' '      if (false) {' unit:test_iface_config
mutate iface_cache_entry_too_small_for_an_apply src/helpers/iface/IfaceCache.h '#define IFACE_CACHE_ENTRY_CAP 384' '#define IFACE_CACHE_ENTRY_CAP 128' unit:test_iface_config
# Deployment authority, device side (the security review of 2026-10-25)
mutate dep_membership_stale_admission_ok src/helpers/deployment/DepRecord.cpp '      if (m.admitted_revision <= cur.authority_revision) return BAD_AUTHORITY_SET;   // new here, admitted before?' '' unit:test_dep_record
mutate dep_membership_lowered_ok src/helpers/deployment/DepRecord.cpp '    } else if (m.admitted_revision < old->admitted_revision) {' '    } else if (false) {' unit:test_dep_record
mutate dep_admitted_not_stored src/helpers/deployment/DepRecord.cpp '    x.adm[i].admitted_revision = p.u32();' '    p.u32();' unit:test_dep_record
mutate dep_update_skips_membership src/helpers/deployment/DepProvision.cpp '  if (checkMembership(cur, *set) != OK) return P_BAD_AUTHORITY_SET;' '' unit:test_dep_provision
mutate dep_provision_skips_membership src/helpers/deployment/DepProvision.cpp '  if (se == OK && has && checkMembership(*cur, *set) != OK) return P_BAD_AUTHORITY_SET;' '' unit:test_dep_provision
mutate dep_duplicate_text_not_compared src/helpers/deployment/DepProvision.cpp '  if (se == DUPLICATE && (p.set_len != cur->set_len' '  if (false && (p.set_len != cur->set_len' unit:test_dep_provision
mutate dep_update_duplicate_not_compared src/helpers/deployment/DepProvision.cpp '    if (set_len != cur.set_len || memcmp(set_text, current.set_text, set_len) != 0 ||' '    if (false &&' unit:test_dep_provision
mutate dep_weak_dak_pinned src/helpers/deployment/DepProvision.cpp '    if (p.n_rotations > 0 || weakKey(p.dak)) return P_UNTRUSTED_AUTHORITY_KEY;' '    if (p.n_rotations > 0) return P_UNTRUSTED_AUTHORITY_KEY;' unit:test_dep_provision
mutate dep_provision_rotation_applied src/helpers/deployment/DepProvision.cpp '    if (p.n_rotations > 0) return P_UNTRUSTED_AUTHORITY_KEY;' '' unit:test_dep_provision
mutate dep_duplicate_member_key_ok src/helpers/deployment/DepAuthority.cpp '      if (memcmp(s->members[j].instance_id, mb.instance_id, ID_LEN) == 0 ||
          memcmp(s->members[j].public_key, mb.public_key, KEY_LEN) == 0)' '      if (memcmp(s->members[j].instance_id, mb.instance_id, ID_LEN) == 0)' unit:test_dep_authority
mutate dep_weak_member_key_ok src/helpers/deployment/DepAuthority.cpp '    if (weakKey(mb.public_key)) return BAD_AUTHORITY_SET;' '' unit:test_dep_authority
mutate dep_weak_rotation_key_ok src/helpers/deployment/DepAuthority.cpp ' && to <= MAX_EPOCH && !weakKey(key)) {' ' && to <= MAX_EPOCH) {' unit:test_dep_authority
mutate dep_weak_key_test_disabled src/helpers/deployment/DepAuthority.cpp '  return !fe_isnonzero(p.X) && !fe_isnonzero(d);   // the identity: X = 0, Y = Z' '  return false;' unit:test_dep_authority
mutate dep_set_epoch_unbounded src/helpers/deployment/DepAuthority.cpp ' || s->authority_epoch == 0 || s->authority_epoch > MAX_EPOCH)' ')' unit:test_dep_authority
mutate dep_wrong_deployment_code src/helpers/deployment/DepAuthority.cpp '  if (memcmp(out->deployment_id, ctx.deployment_id, ID_LEN) != 0) return WRONG_DEPLOYMENT;' '  if (memcmp(out->deployment_id, ctx.deployment_id, ID_LEN) != 0) return BAD_AUTHORITY_SET;' unit:test_dep_authority
mutate dep_fallback_not_flagged src/helpers/deployment/DepState.cpp '    s->other_damaged = c[other]->data && !ok[other];' '    s->other_damaged = false;' unit:test_dep_state
mutate dep_fallback_not_reported src/helpers/deployment/DepState.cpp '    w.key("fallback"); w.boolean(s->other_damaged);' '' unit:test_dep_state
mutate dep_lost_marks_fresh src/helpers/deployment/DepState.cpp '  s->blocked = reconcileMarks(distrust ? nullptr : &lm.marks, s->set, &s->marks);' '  s->blocked = reconcileMarks(&lm.marks, s->set, &s->marks);' unit:test_dep_state
mutate dep_replay_window_reopened src/helpers/deployment/DepReplay.cpp '  return job_id == w.mark ? RV_DUPLICATE : RV_TOO_OLD;' '  return job_id == w.mark ? RV_DUPLICATE : (w.mark - job_id < 24 ? RV_NEW : RV_TOO_OLD);' unit:test_dep_replay
mutate dep_replay_mark_reruns src/helpers/deployment/DepReplay.cpp '  if (job_id > w.mark) return RV_NEW;' '  if (job_id >= w.mark) return RV_NEW;' unit:test_dep_replay
mutate dep_marks_readmission_kept src/helpers/deployment/DepRecord.cpp '    } else if (mem.admitted_revision > stored->m[j].admitted_revision) {' '    } else if (false) {' unit:test_dep_record
mutate dep_marks_fallback_reopens src/helpers/deployment/DepRecord.cpp '      nm.wm = stored->m[j].wm;                            // the same admission, or the record fell back: keep it' '      nm.wm.mark = stored->m[j].admitted_revision == mem.admitted_revision ? stored->m[j].wm.mark : 0;' unit:test_dep_record
mutate dep_marks_missing_member_fresh src/helpers/deployment/DepRecord.cpp '      nm.wm.mark = mem.admitted_revision > stored->revision ? 0 : MARK_BLOCKED;' '      nm.wm.mark = 0;' unit:test_dep_record
mutate dep_marks_lost_fresh src/helpers/deployment/DepRecord.cpp '      nm.wm.mark = MARK_BLOCKED;                          // the marks were lost: nothing to trust' '      nm.wm.mark = 0;' unit:test_dep_record
mutate dep_removal_newer_first src/helpers/deployment/DepRecord.h '  order[0] = known ? 1 - newer_slot : 0;' '  order[0] = known ? newer_slot : 0;' unit:test_dep_record
mutate dep_job_id_optional src/helpers/deployment/DepWrapper.cpp '    if (out->cls != OPC_PLAIN && !out->has_job) goto done;' '' unit:test_dep_wrapper
mutate dep_unknown_op_plain src/helpers/deployment/DepWrapper.cpp '  return OPC_JOB;
}' '  return OPC_PLAIN;
}' unit:test_dep_wrapper
mutate dep_issuer_unbound src/helpers/deployment/DepWrapper.cpp '    if (memcmp(bound_issuer, issuer, ID_LEN) != 0) { e = WR_BAD_SIGNATURE; goto done; }' '' unit:test_dep_wrapper
mutate dep_nonce_unchecked src/helpers/deployment/DepWrapper.cpp '    if (out->cls == OPC_SENSITIVE) {' '    if (false) {' unit:test_dep_wrapper
mutate dep_private_stream_plain src/helpers/deployment/DepWrapper.cpp '    if (strcmp(name, p) == 0) return true;' '    if (strcmp(name, p) == 0) return false;' unit:test_dep_wrapper
# the Repeater and Room server console interface, and the per-role key table
mutate config_onoff_word_dropped src/helpers/iface/IfaceConfig.cpp '      const bool onoff = (k.flags & KF_ONOFF) != 0;' '      const bool onoff = false;' unit:test_iface_config
mutate config_server_table_ignored src/helpers/iface/IfaceConfig.cpp '  *n = h->keys ? h->nkeys : NKEYS;
  return h->keys ? h->keys : KEYS;' '  *n = NKEYS; (void)h;
  return KEYS;' unit:test_iface_config
mutate config_latitude_unbounded src/helpers/iface/IfaceConfig.cpp '    const double lim = lat ? 90 : 180;' '    const double lim = 180;' unit:test_iface_config
mutate console_prefix_not_handed_over examples/simple_repeater/main.cpp '      the_iface.startLine();' '      (void)0;' sim/tests/test_device_interface_server.py::test_hello_names_the_role
mutate console_line_unbounded src/helpers/iface/ConsoleIface.cpp '  if (_len >= MAX_LINE) { _overflow = true; return; }' '  if (_len >= MAX_LINE + 4000) { _overflow = true; return; }' sim/tests/test_device_interface_server.py::test_a_line_too_long_is_refused_not_truncated
mutate console_echo_line_not_ended src/helpers/iface/ConsoleIface.cpp '  out.print("\r\n");   // ends the console' '  // ends the console' sim/tests/test_device_interface_server.py::test_status_has_no_contact_list
mutate console_sub_stop_ignored src/helpers/iface/ConsoleIface.cpp 'Status opSubStop(OpContext& c) { return subStop(c, &self(c)->subs(), self(c)->produced()); }' 'Status opSubStop(OpContext& c) { return subStop(c, nullptr, self(c)->produced()); }' sim/tests/test_device_interface_server.py::test_health_events_come_back_as_console_lines
mutate console_room_reports_repeater examples/simple_room_server/main.cpp 'ifaceBattery, "room",' 'ifaceBattery, "repeater",' sim/tests/test_device_interface_server.py::test_hello_names_the_role
# telemetry streams (docs/contract/TELEMETRY.md)
mutate stream_first_event_not_immediate src/helpers/iface/Streams.cpp '  if (!sub.active && !def.events()) *started = s;' '  if (false) *started = s;' sim/tests/test_telemetry_streams.py::test_a_new_subscription_after_a_stop_gets_an_event_at_once
mutate stream_power_claims_to_tell src/helpers/iface/Streams.cpp '  const bool can_tell = b.isPwrMgtInitialised();' '  const bool can_tell = true;' sim/tests/test_telemetry_streams.py::test_each_stream_sends_its_first_event_at_once
mutate stream_unknown_reset_reason_reported src/helpers/iface/Streams.cpp '  const bool known = why && strcmp(why, "Not available") != 0;' '  const bool known = why != nullptr;' sim/tests/test_telemetry_streams.py::test_each_stream_sends_its_first_event_at_once
mutate stream_tx_flood_direct_swapped src/helpers/iface/Streams.cpp '  w.key("flood"); w.unum(d.getNumSentFlood());' '  w.key("flood"); w.unum(d.getNumSentDirect());' sim/tests/test_telemetry_streams.py::test_radio_counts_a_transmission_repeater
mutate stream_period_unbounded src/helpers/iface/Streams.cpp '  if (pt >= 0 && !jsonGetInt(*c.doc, pt, def.min_s, def.max_s, &period)) return ST_INVALID;' '  if (pt >= 0 && !jsonGetInt(*c.doc, pt, 1, 100000, &period)) return ST_INVALID;' sim/tests/test_telemetry_streams.py::test_the_stream_table_bounds_requests
mutate obs_companion_sink_not_registered examples/companion_radio/MeconIface.cpp '  observeSink().fn = onObserve;' '  (void)0;' sim/tests/test_telemetry_streams.py::test_observations_carry_each_received_packet_as_heard
mutate obs_hex_nibbles_swapped src/helpers/iface/Streams.cpp '  for (int i = 0; i < len; i++) { data[n++] = H[raw[i] >> 4]; data[n++] = H[raw[i] & 15]; }' '  for (int i = 0; i < len; i++) { data[n++] = H[raw[i] & 15]; data[n++] = H[raw[i] >> 4]; }' sim/tests/test_telemetry_streams.py::test_observations_carry_each_received_packet_as_heard
mutate obs_limiter_refuses_all src/helpers/iface/Streams.h '    if (!tokens) { dropped++; return false; }' '    dropped++; return false;' sim/tests/test_telemetry_streams.py::test_observations_carry_each_received_packet_as_heard
mutate obs_event_stream_takes_a_period src/helpers/iface/Streams.cpp '  if (pt >= 0 && def.events()) { c.error = "no-period"; return ST_INVALID; }' '' sim/tests/test_telemetry_streams.py::test_an_event_stream_takes_no_period
mutate conn_change_not_detected examples/companion_radio/MeconIface.cpp '    if (sig != _conn_last) {' '    if (false) {' sim/tests/test_device_interface.py::test_connectivity_reports_the_links_and_each_change_at_once
mutate conn_stream_not_registered examples/companion_radio/main.cpp '  the_mesh.iface().setConnectivity(ifaceConnWrite, ifaceConnSignature);' '  (void)0;' sim/tests/test_device_interface.py::test_connectivity_reports_the_links_and_each_change_at_once
mutate conn_tls_names_swapped examples/companion_radio/main.cpp '  static const char* const TLS[] = {"plain", "verified", "unverified"};' '  static const char* const TLS[] = {"plain", "unverified", "verified"};' sim/tests/test_device_interface.py::test_connectivity_reports_the_links_and_each_change_at_once
mutate conn_reconnects_not_counted src/helpers/mqtt/MqttController.cpp '  _sessions++;' '' sim/tests/test_device_interface.py::test_connectivity_reports_the_links_and_each_change_at_once
mutate loopgap_never_raised src/helpers/iface/Streams.h '    if (last && now - last > max) max = now - last;' '' unit:test_streams
mutate loopgap_read_not_reset src/helpers/iface/Streams.h '  uint32_t take() { uint32_t m = max; max = 0; return m; }' '  uint32_t take() { return max; }' unit:test_streams
mutate obs_limiter_refills_to_full src/helpers/iface/Streams.h '      tokens = tokens + refill > OBS_RATE ? OBS_RATE : tokens + refill;' '      tokens = OBS_RATE;' unit:test_streams
mutate lpp_sign_ignored src/helpers/iface/Lpp.cpp '    if (t->is_signed && (raw & (1u << (8 * t->size - 1)))) v -= (int64_t)1 << (8 * t->size);' '' unit:test_lpp
mutate lpp_everything_signed src/helpers/iface/Lpp.cpp '    if (t->is_signed && (raw & (1u << (8 * t->size - 1))))' '    if ((raw & (1u << (8 * t->size - 1))))' unit:test_lpp
mutate lpp_gps_not_skipped src/helpers/iface/Lpp.cpp '    if (code == LPP_GPS) {' '    if (false) {' unit:test_lpp
mutate env_offered_without_sensors src/helpers/iface/ConsoleIface.h '  uint32_t streamsProduced() const { return STREAMS_PRODUCED | (_env_ok ? (1u << SID_ENVIRONMENT) : 0); }' '  uint32_t streamsProduced() const { return STREAMS_PRODUCED | (1u << SID_ENVIRONMENT); }' sim/tests/test_telemetry_streams.py::test_environment_is_absent_without_sensors
mutate env_not_checked_before_first_answer src/helpers/iface/ConsoleIface.cpp '  if (!_env_checked) refreshEnvironment(millis());' '' sim/tests/test_telemetry_streams.py::test_environment_carries_the_attached_sensors
mutate inv_empty_neighbour_slots_counted examples/simple_repeater/MyMesh.h '    for (int i = 0; i < MAX_NEIGHBOURS; i++) if (neighbours[i].heard_timestamp) n++;' '    for (int i = 0; i < MAX_NEIGHBOURS; i++) n++;' sim/tests/test_telemetry_streams.py::test_inventory_counts_a_heard_neighbour_on_the_repeater
mutate inv_contacts_swapped examples/companion_radio/MeconIface.cpp '      writeCount(w, "contacts", _mesh->getNumContacts(), _mesh->getContactCapacity());' '      writeCount(w, "contacts", _mesh->getContactCapacity(), _mesh->getNumContacts());' sim/tests/test_telemetry_streams.py::test_inventory_counts_on_the_companion
mutate inv_repeater_callback_missing examples/simple_repeater/main.cpp 'ifaceRadioCounts, &sensors, ifaceInventory,' 'ifaceRadioCounts, &sensors, nullptr,' sim/tests/test_telemetry_streams.py::test_inventory_counts_a_heard_neighbour_on_the_repeater
# --- the Deployment authority layer (docs/contract/AUTHORITY.md): undo each rule and its tests must go red
DA=sim/tests/test_deployment_authority.py
mutate deploy_mark_not_persisted_before_run src/helpers/iface/DeployAuth.cpp '    Dep::markAccepted(&wm, v.job_id);
    if (!persistMarks()) {' '    Dep::markAccepted(&wm, v.job_id);
    if (false) {' $DA::test_a_signed_config_apply_runs_and_a_resend_runs_nothing
mutate deploy_failed_mark_write_still_runs src/helpers/iface/DeployAuth.cpp '    if (!persistMarks()) {
      wm.mark = old;' '    if (false) {
      wm.mark = old;' $DA::test_a_mark_that_cannot_be_written_runs_nothing_and_is_not_consumed
mutate deploy_channel_nonce_not_compared src/helpers/deployment/DepWrapper.cpp 'strcmp(nonce, link_nonce) != 0)' 'false)' $DA::test_a_sensitive_operation_needs_this_links_nonce_once
mutate deploy_channel_nonce_reusable src/helpers/iface/DeployAuth.cpp '  if (v.cls == Dep::OPC_SENSITIVE) issueNonce(nonce_slot);   // used once' '' $DA::test_a_sensitive_operation_needs_this_links_nonce_once
mutate deploy_mqtt_provisions_unsigned src/helpers/iface/DeployAuth.cpp '  if (!has && !_signed && !present) { c.error = "unauthenticated"; return ST_REFUSED; }' '' $DA::test_mqtt_never_provisions_unsigned_and_never_writes_unsigned_after
mutate deploy_duplicate_runs_again src/helpers/iface/DeployAuth.cpp '    if (rv == Dep::RV_DUPLICATE) {' '    if (false) {' $DA::test_a_signed_config_apply_runs_and_a_resend_runs_nothing
mutate deploy_replay_runs src/helpers/iface/DeployAuth.cpp '    if (rv == Dep::RV_TOO_OLD) {' '    if (false) {' $DA::test_an_older_job_id_is_a_replay_and_a_newer_one_runs
mutate deploy_unsigned_write_after_enrolment src/helpers/iface/DeployAuth.cpp '  if (_dep_ls != Dep::LS_ABSENT && !_signed && op.priv != PRIV_READ &&' '  if (false && !_signed && op.priv != PRIV_READ &&' $DA::test_unsigned_writes_are_refused_once_enrolled_but_reads_work
mutate deploy_signed_principal_may_erase src/helpers/cli/CliPolicy.h '    /* signed         N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW,           NOT_PERMITTED,   ALLOW),' '    /* signed         N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   ALLOW,           ALLOW,           ALLOW),' unit:test_cli_engine
mutate deploy_signed_context_follows_the_signature_not_the_transport src/helpers/cli/CliPolicy.h '  if (c.remote != wire_remote) return P_UNTRUSTED;   // a local-looking remote caller, or the reverse: fail closed' '' unit:test_cli_engine
# --- the second slice (AUTHORITY.md sections 4, 9, 11): the provisioned marker, deployment.reset, the recovery paths
mutate deploy_marker_ignored_at_load src/helpers/iface/DeployAuth.cpp '    // No copy of the record, but the provisioned marker says there was one: corrupt, never absent (section 4).
    _dep_ls = Dep::withMarker(Dep::LS_ABSENT, Dep::markerRead());' '    _dep_ls = Dep::LS_ABSENT;' $DA::test_the_provisioned_marker_makes_lost_files_corrupt_not_absent
mutate deploy_marker_not_written_at_first_provisioning src/helpers/iface/DeployAuth.cpp '    if (!has && !Dep::markerSet()) { c.error = "storage"; st = ST_BUSY; goto done; }' '' $DA::test_the_provisioned_marker_makes_lost_files_corrupt_not_absent
mutate deploy_unsupported_marker_still_provisions src/helpers/iface/DeployAuth.cpp '  if (!has && Dep::markerRead() == Dep::MK_UNSUPPORTED) { c.error = "unsupported"; return ST_REFUSED; }' '' $DA::test_a_platform_with_no_marker_store_never_provisions_unsigned
mutate deploy_reset_unsigned_on_a_healthy_record src/helpers/iface/DeployAuth.cpp '    if (!_signed) { if (_dep) _dep->auth_rejections++; c.error = "unauthenticated"; return ST_REFUSED; }' '' $DA::test_an_unsigned_reset_of_a_healthy_record_is_refused_even_over_the_cable
mutate deploy_reset_over_a_broker src/helpers/iface/DeployAuth.cpp '    if (!usb) { _dep->auth_rejections++; c.error = "usb_only"; return ST_REFUSED; }' '' $DA::test_reset_and_unsigned_cli_writes_are_refused_over_mqtt
mutate deploy_reset_leaves_the_marker src/helpers/iface/DeployAuth.cpp 'st->removeBoth(marks_slot, Dep::SF_MARKS) && Dep::markerClear();' 'st->removeBoth(marks_slot, Dep::SF_MARKS);' $DA::test_a_signed_reset_over_usb_unenrols_and_keeps_the_rest
mutate deploy_reset_in_hardened src/helpers/iface/DeployAuth.cpp '  if (Posture::isHardened(cx.posture) && !(corrupt && Cli::unlockManagement(cx))) {' '  if (false) {' $DA::test_reset_needs_normal_posture_and_a_corrupt_record_in_hardened_needs_the_local_unlock
mutate deploy_ble_factory_reset_when_enrolled examples/companion_radio/MyMesh.cpp '    fctx.enrolled = _iface.enrolled();' '' $DA::test_a_ble_factory_reset_is_refused_once_enrolled_and_audited
mutate deploy_rebuild_without_reading_the_record examples/companion_radio/MyMesh.cpp '      MeconIface::RecordSnapshot* snap = _iface.snapshotRecord();
      if (!snap) {' '      MeconIface::RecordSnapshot* snap = _iface.snapshotRecord();
      if (false) {' $DA::test_a_rescue_rebuild_that_cannot_read_the_record_refuses_and_changes_nothing
mutate deploy_cli_gate_not_told_the_device_is_enrolled examples/companion_radio/MyMesh.cpp '    ctx.enrolled = _iface.enrolled();   // the one place that gates a command line: every adapter and the rescue console' '    ctx.enrolled = false;' $DA::test_an_unsigned_cli_write_over_the_usb_frame_is_refused_once_enrolled_but_a_read_works
mutate deploy_enrolled_rule_dropped_from_the_table src/helpers/cli/CliPolicy.h '  if (r == R_ALLOW && c.enrolled && p != P_SIGNED && cls != C_READ && !(cls == C_REBOOT && !c.remote) &&' '  if (false && c.enrolled && p != P_SIGNED && cls != C_READ && !(cls == C_REBOOT && !c.remote) &&' unit:test_cli_engine
mutate deploy_enrolled_rule_takes_the_cables_destructive_class src/helpers/cli/CliPolicy.h '      !(p == P_CABLE && cls == C_DESTRUCTIVE) && !unlock_posture)' '      false)' unit:test_cli_engine
mutate deploy_marker_alone_reads_as_absent src/helpers/deployment/DepMarker.h 'return (ls == LS_ABSENT && m == MK_PRESENT) ? LS_CORRUPT : ls;' 'return ls;' unit:test_dep_state
mutate deploy_remote_reboot_unsigned_when_enrolled src/helpers/cli/CliPolicy.h ' && !(cls == C_REBOOT && !c.remote) &&' ' && !(cls == C_REBOOT) &&' unit:test_cli_engine
mutate deploy_frames_not_told_enrolled examples/companion_radio/MyMesh.cpp '    fctx.enrolled = _iface.enrolled();' '    (void)0;' sim/tests/test_deployment_authority.py::test_a_signed_config_apply_runs_and_a_resend_runs_nothing

# ---- the authority layer on the Repeater and Room server console
DS=sim/tests/test_deployment_authority_server.py
mutate server_enrolled_flag_not_published src/helpers/iface/DeployAuth.cpp '  Cli::serverCaller().enrolled = _dep_ls != Dep::LS_ABSENT;' '  Cli::serverCaller().enrolled = false;' $DS::test_unsigned_writes_are_refused_once_enrolled_over_the_console
mutate server_contexts_ignore_the_enrolled_flag src/helpers/cli/CliCaller.h '  c.enrolled = s.enrolled;' '  c.enrolled = false;' unit:test_cli_engine
mutate server_remote_admin_not_told_enrolled src/helpers/cli/CliCaller.h '  c.enrolled = s.enrolled;' '  c.enrolled = s.enrolled && !c.remote;' $DS::test_lora_remote_admin_keeps_reads_only_once_enrolled
mutate server_signed_line_not_the_signed_principal src/helpers/iface/DeployAuth.cpp '  if (_env.publish_caller) Cli::serverCaller().signed_line = true;' '' $DS::test_a_signed_config_apply_runs_once_and_its_duplicate_and_replay_run_nothing
mutate server_signed_promotion_for_any_caller src/helpers/cli/CliCaller.h '  if (s.signed_line && !c.remote && c.transport == T_USB && c.auth == A_CABLE) c.auth = A_SIGNED;' '  if (s.signed_line) c.auth = A_SIGNED;' unit:test_cli_engine
mutate server_console_ignores_signed_wrappers src/helpers/iface/ConsoleIface.cpp '        if (DeployAuth::isWrapper(_line, _len))' '        if (false)' $DS::test_a_signed_config_apply_runs_once_and_its_duplicate_and_replay_run_nothing
mutate server_console_unsigned_write_allowed_once_enrolled src/helpers/iface/ConsoleIface.cpp '    case DeployAuth::PRE_DENY: return false;' '    case DeployAuth::PRE_DENY: return true;' $DS::test_unsigned_writes_are_refused_once_enrolled_over_the_console
mutate server_console_has_no_deployment_reset src/helpers/iface/ConsoleIface.cpp '  {"deployment.reset", opDeployReset, PRIV_WRITE, true},' '  {"deployment.resetx", opDeployReset, PRIV_WRITE, true},' $DS::test_a_signed_reset_over_the_console_unenrols_and_keeps_the_rest
mutate server_hello_hides_the_channel_nonce src/helpers/iface/ConsoleIface.cpp '  if (nonce && nonce[0]) { w.key("channel_nonce"); w.str(nonce); }' '' $DS::test_first_provisioning_over_the_console_and_the_read_back
mutate server_status_omits_the_deployment src/helpers/iface/ConsoleIface.cpp '  m->auth().writeStatus(w, hardened);' '' $DS::test_first_provisioning_over_the_console_and_the_read_back
mutate server_console_issues_no_nonce src/helpers/iface/ConsoleIface.cpp '        if (!_nonce[0]) DeployAuth::issueNonce(_nonce);   // issued when the link'"'"'s first request arrives' '' $DS::test_a_sensitive_operation_needs_the_consoles_nonce_once
mutate server_console_is_not_the_cable src/helpers/iface/ConsoleIface.cpp '        const Cli::Context cable(Cli::T_USB, Cli::A_CABLE, _host.posture(_host.user));' '        const Cli::Context cable(Cli::T_TCP, Cli::A_NONE, _host.posture(_host.user));' $DS::test_first_provisioning_over_the_console_and_the_read_back
mutate server_repeater_erase_keeps_the_marker examples/simple_repeater/MyMesh.cpp '  if (ok) Cli::serverFormatted();' '' $DS::test_erase_works_from_the_cable_once_enrolled_and_clears_the_marker
mutate server_room_erase_keeps_the_marker examples/simple_room_server/MyMesh.cpp '  if (ok) Cli::serverFormatted();' '' $DS::test_erase_works_from_the_cable_once_enrolled_and_clears_the_marker
mutate server_record_store_not_given examples/simple_room_server/main.cpp '                                       &dep_store, ifaceClock, &the_mesh.self_id };' '                                       nullptr, ifaceClock, &the_mesh.self_id };' $DS::test_first_provisioning_over_the_console_and_the_read_back
mutate server_format_hook_not_registered src/helpers/iface/DeployAuth.cpp '    Cli::serverCaller().on_format_user = this;' '    Cli::serverCaller().on_format_user = nullptr;' $DS::test_erase_works_from_the_cable_once_enrolled_and_clears_the_marker

# Local unlock (AUTHORITY.md sections 9 and 10): the handshake on the cable, what it opens, and the secret's confinement
mutate unlock_wrong_response_accepted src/helpers/iface/DeployAuth.cpp '  if (parsed && fresh && match) {' '  if (parsed && fresh) {' $DA::test_a_challenge_answers_one_attempt_only
mutate unlock_wrong_response_accepted_server src/helpers/iface/DeployAuth.cpp '  if (parsed && fresh && match) {' '  if (parsed && fresh) {' $DS::test_in_hardened_the_console_unlock_resets_a_corrupt_record_and_opens_nothing_else
mutate unlock_lockout_dropped src/helpers/iface/DeployAuth.cpp '  if (++_fails >= MAX_FAILURES) { _locked = true; _locked_ms = millis(); }' '  ++_fails;' $DA::test_a_wrong_answer_is_refused_and_counted_and_the_sixth_attempt_meets_the_lockout
mutate unlock_challenge_reusable src/helpers/iface/DeployAuth.cpp '  memset(l.challenge, 0, sizeof(l.challenge)); l.have_challenge = false;' '' $DA::test_a_challenge_answers_one_attempt_only
mutate unlock_allowed_over_mqtt src/helpers/iface/DeployAuth.cpp '  if (!cable) return "usb_only";' '  if (false) return "usb_only";' $DA::test_the_handshake_is_refused_over_mqtt_and_to_a_signed_request
mutate unlock_hardened_reset_without_unlock src/helpers/iface/DeployAuth.cpp '  if (Posture::isHardened(cx.posture) && !(corrupt && Cli::unlockManagement(cx))) {' '  if (Posture::isHardened(cx.posture) && !corrupt) {' $DA::test_in_hardened_the_unlock_opens_reads_and_the_reset_of_a_corrupt_record_and_nothing_else
mutate unlock_secret_leaked_in_deployment_get src/helpers/iface/DeployAuth.cpp '  w.key("fallback"); w.boolean(s.other_damaged);' '  { char sx[2 * Dep::UNLOCK_LEN + 1]; hexTo(sx, rec.unlock_secret, Dep::UNLOCK_LEN); w.key("unlock"); w.str(sx); }
  w.key("fallback"); w.boolean(s.other_damaged);' $DA::test_the_secret_appears_in_no_reply_status_log_or_file_as_text
mutate unlock_survives_a_new_secret src/helpers/iface/DeployAuth.cpp '  if (!l || !l->unlocked || _dep_ls == Dep::LS_ABSENT || l->unlocked_epoch != _secret_epoch) return false;' '  if (!l || !l->unlocked || _dep_ls == Dep::LS_ABSENT) return false;' $DA::test_a_signed_set_unlock_replaces_the_secret_and_ends_every_older_unlock
mutate unlock_never_lapses src/helpers/iface/DeployAuth.cpp '  return (uint32_t)(millis() - l->unlocked_ms) < UNLOCK_MS;' '  return true;' $DA::test_an_unlock_lapses_after_ten_minutes
mutate unlock_challenge_never_expires src/helpers/iface/DeployAuth.cpp '                     (uint32_t)(millis() - l.challenge_ms) < CHALLENGE_MS;' '                     true;' $DA::test_an_expired_challenge_is_refused
mutate unlock_survives_a_new_session examples/companion_radio/MeconDeploy.cpp '  if (l) l->unlock.clear();' '' $DA::test_a_challenge_and_the_right_answer_unlock_the_cable_in_normal_and_it_opens_only_the_posture
mutate unlock_cli_flag_not_stamped examples/companion_radio/MyMesh.cpp '      ctx.unlocked = ctx.transport == Cli::T_USB && ctx.auth == Cli::A_CABLE && _iface.unlockedFor(_cmd_src);   // section 10' '      (void)0;' $DA::test_a_challenge_and_the_right_answer_unlock_the_cable_in_normal_and_it_opens_only_the_posture
mutate unlock_server_cli_flag_not_published src/helpers/iface/ConsoleIface.cpp '    Cli::serverCaller().unlocked = consoleUnlocked;   // the console lines of the stock CLI see the unlock too' '' $DS::test_the_console_unlock_opens_the_posture_in_normal_and_nothing_else
mutate unlock_policy_opens_hardened_posture src/helpers/cli/CliPolicy.h '  const bool unlock_posture = unlockManagement(c) && cls == C_POSTURE && c.posture == 0;' '  const bool unlock_posture = unlockManagement(c) && cls == C_POSTURE;' unit:test_cli_engine
mutate unlock_policy_opens_writes src/helpers/cli/CliPolicy.h '  const bool unlock_posture = unlockManagement(c) && cls == C_POSTURE && c.posture == 0;' '  const bool unlock_posture = unlockManagement(c) && c.posture == 0;' unit:test_cli_engine
mutate unlock_policy_honoured_for_any_principal src/helpers/cli/CliPolicy.h 'inline bool unlockManagement(const Context& c) { return c.unlocked && principalOf(c) == P_CABLE; }' 'inline bool unlockManagement(const Context& c) { return c.unlocked; }' unit:test_cli_engine
mutate unlock_hmac_ignores_the_message src/helpers/deployment/DepHmac.cpp '  update(&c, msg, msg_len);' '' unit:test_dep_hmac
mutate unlock_compare_checks_one_byte src/helpers/deployment/DepHmac.cpp '  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);' '  for (size_t i = 0; i < 1 && i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);' unit:test_dep_hmac
mutate unlock_set_unlock_not_sensitive src/helpers/deployment/DepWrapper.cpp ' || strcmp(op, "deployment.set_unlock") == 0 ||
      strcmp(op, "identity.rotate") == 0)' ' ||
      strcmp(op, "identity.rotate") == 0)' unit:test_dep_wrapper

mutate unlock_salvage_takes_the_older_frame src/helpers/deployment/DepState.cpp '      if (s->has_salvage && gen <= best) continue;' '' unit:test_dep_state

# --- second review (H1): the rescue console's file commands
mutate rescue_files_not_locked examples/companion_radio/MyMesh.cpp '    if (file_cmd && rescueFilesLocked()) {' '    if (false) {' sim/tests/test_deployment_authority.py::test_the_rescue_file_commands_are_refused_while_a_record_exists_healthy_or_corrupt
mutate rescue_files_open_in_hardened examples/companion_radio/MyMesh.cpp '  return _prefs.posture != Posture::NORMAL;
}' '  return false;
}' sim/tests/test_deployment_authority.py::test_the_rescue_file_commands_are_refused_in_hardened_with_no_record_and_work_in_normal_standalone

# --- second review (M1): an unsigned first provisioning is the cable's alone
mutate provision_unsigned_over_ble src/helpers/iface/DeployAuth.cpp '  const bool present = cx.transport == Cli::T_USB && cx.auth == Cli::A_CABLE;' '  const bool present = (cx.transport == Cli::T_USB && cx.auth == Cli::A_CABLE) || (cx.transport == Cli::T_BLE && cx.auth == Cli::A_PAIRED_PIN);' sim/tests/test_deployment_authority.py::test_an_unsigned_first_provisioning_over_ble_is_refused_whatever_the_pin_and_the_cable_still_works

# --- second review (M2): signed lowering, and Hardened only with a local unlock secret in the record
mutate posture_signed_may_not_lower src/helpers/cli/CliPolicy.h '(flags & F_CABLE_ONLY) && p != P_CABLE && p != P_SIGNED) r = R_NOT_PERMITTED;' '(flags & F_CABLE_ONLY) && p != P_CABLE) r = R_NOT_PERMITTED;' unit:test_cli_engine
mutate posture_hardened_without_secret_allowed src/helpers/cli/CliPolicy.h '  if (r == R_ALLOW && (flags & F_RAISES) && c.enrolled && !c.has_unlock_secret) r = R_NO_UNLOCK_SECRET;' '' unit:test_cli_engine
mutate posture_raise_not_flagged src/helpers/cli/CliRegistry.h 'r.flags = strcmp(arg, "hardened") != 0 ? F_CABLE_ONLY : F_RAISES;' 'r.flags = strcmp(arg, "hardened") != 0 ? F_CABLE_ONLY : 0;' unit:test_cli_engine
mutate posture_companion_secret_not_told examples/companion_radio/MyMesh.cpp 'the rescue console
    ctx.has_unlock_secret = _iface.hasUnlockSecret();' 'the rescue console' sim/tests/test_deployment_authority.py::test_an_unlocked_cable_may_enter_hardened_because_the_record_holds_the_secret_and_without_one_nobody_unsigned_may
mutate posture_server_secret_not_told src/helpers/cli/CliCaller.h '  c.has_unlock_secret = s.has_secret && s.has_secret(s.has_secret_user);' '  c.has_unlock_secret = false;' sim/tests/test_deployment_authority_server.py::test_an_unlocked_console_may_enter_hardened_because_the_record_holds_the_secret

# --- second review (L1): a damaged marks copy blocks every member
mutate marks_fallback_not_detected src/helpers/deployment/DepRecord.cpp '  if (out->state == LS_OK) out->other_damaged = c[1 - out->slot]->data && !intact[1 - out->slot];' '' unit:test_dep_state
mutate marks_fallback_trusted src/helpers/deployment/DepState.cpp 'reconcileMarks(distrust ? nullptr : &lm.marks, s->set, &s->marks);' 'reconcileMarks(s->marks_lost ? nullptr : &lm.marks, s->set, &s->marks);' sim/tests/test_deployment_authority.py::test_a_damaged_marks_copy_blocks_every_member_so_a_captured_request_cannot_run_again
mutate marks_fallback_not_reported src/helpers/iface/DeployAuth.cpp '  w.key("marks_fallback"); w.boolean(s.marks_fallback);' '  w.key("marks_fallback"); w.boolean(false);' sim/tests/test_deployment_authority.py::test_a_damaged_marks_copy_blocks_every_member_so_a_captured_request_cannot_run_again

# --- second review (L2): the stock-array rescue rebuild puts the marker back right after the format
mutate rescue_rebuild_marker_late examples/companion_radio/MyMesh.cpp '      if (!_iface.restoreMarker(snap)) Serial.println("  Error: the provisioned marker could not be written back; use deployment.reset");' '' sim/tests/test_deployment_authority.py::test_a_stock_array_rescue_rebuild_puts_the_marker_back_before_anything_else

# --- second review (L3): the unlock secret travels in the clear, so only over the cable
mutate unlock_secret_set_over_any_transport src/helpers/iface/DeployAuth.cpp '  if (!viaCable()) { c.error = "usb_only"; return ST_REFUSED; }' '' sim/tests/test_deployment_authority.py::test_the_unlock_secret_is_accepted_only_over_the_usb_cable_even_in_a_signed_request
mutate unlock_secret_provisioned_over_any_transport src/helpers/iface/DeployAuth.cpp '  if (k->p.has_unlock && !viaCable()) { c.error = "usb_only"; goto done; }' '' sim/tests/test_deployment_authority.py::test_the_unlock_secret_is_accepted_only_over_the_usb_cable_even_in_a_signed_request

# --- provenance (AUTHORITY.md 3.4) and identity.rotate (section 11)
PV=sim/tests/test_provenance.py
IR=sim/tests/test_identity_rotate.py
mutate prov_result_signature_dropped src/helpers/iface/Provenance.cpp '  if (!_auth || (!_auth->enrolled() && !_force)) return (int)len;   // a standalone device signs nothing' '  return (int)len;' $PV::test_companion_results_and_events_are_signed_once_a_record_exists
mutate prov_console_event_signature_dropped src/helpers/iface/ConsoleIface.cpp '    if (n > 0) n = _prov.sign(STREAMS[s].name, ev, (size_t)n, sizeof(ev));   // -1: not signable, so not sent' '' $PV::test_console_results_and_events_are_signed_once_a_record_exists
mutate prov_wrong_domain src/helpers/iface/Provenance.cpp 'const char DOMAIN[] = "MECON-UPLINK-v1";' 'const char DOMAIN[] = "MECON-UPLINK-v2";' $PV::test_companion_results_and_events_are_signed_once_a_record_exists
mutate prov_unsigned_epoch_still_signs src/helpers/iface/Provenance.cpp '  if (!_epoch_saved || _seq == 0xFFFFFFFFu) {' '  if (_seq == 0xFFFFFFFFu) {' $PV::test_an_unsaved_boot_epoch_signs_nothing
mutate rotate_without_signature src/helpers/iface/DeployAuth.cpp '  if (!_signed) { _dep->auth_rejections++; c.error = "unauthenticated"; return ST_REFUSED; }
  return ST_OK;' '  return ST_OK;' $IR::test_rotation_needs_a_signature_and_a_readable_record
mutate rotate_without_a_record src/helpers/iface/DeployAuth.cpp '  if (_dep_ls == Dep::LS_ABSENT) { c.error = "not_enrolled"; return ST_REFUSED; }
  if (_dep_ls != Dep::LS_OK || !_dep) { if (_dep) _dep->auth_rejections++; c.error = "deployment_record_corrupt"; return ST_REFUSED; }' '' $IR::test_rotation_needs_a_signature_and_a_readable_record
mutate rotate_keeps_contacts examples/companion_radio/MyMesh.cpp '    dropped = cs.clearAll();' '    dropped = true;' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rotate_drops_configuration examples/companion_radio/MyMesh.cpp '  if (_listener) _listener->onQueueSizeChanged(0);
  return dropped;' '  if (_listener) _listener->onQueueSizeChanged(0);
  strcpy(_prefs.node_name, "NONAME"); savePrefs();
  return dropped;' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rotate_not_swapped examples/companion_radio/MyMesh.cpp '  self_id = fresh;' '  (void)fresh;' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rotate_unenrols examples/companion_radio/MyMesh.cpp '  self_id = fresh;' '  self_id = fresh;
  _store->removeDeploymentCopies(-1, DataStore::DEP_RECORD);' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rotate_not_sensitive src/helpers/deployment/DepWrapper.cpp ' || strcmp(op, "deployment.set_unlock") == 0 ||
      strcmp(op, "identity.rotate") == 0)' ' || strcmp(op, "deployment.set_unlock") == 0)' $IR::test_rotation_needs_a_signature_and_a_readable_record
mutate revoked_issuer_reported_unknown src/helpers/deployment/DepWrapper.cpp 'e = set.isRevoked(issuer) ? WR_ISSUER_REVOKED : WR_UNKNOWN_ISSUER; goto done; }' 'e = WR_UNKNOWN_ISSUER; goto done; }' $IR::test_a_revoked_member_is_refused_and_cannot_rotate_the_key

# --- review 3 of PR 132 (AUTHORITY_REVIEW_2.md): one group of mutants per finding
mutate rot_statement_signed_by_new_key examples/companion_radio/MyMesh.cpp '    self_id.sign(sig, reinterpret_cast<const uint8_t*>(msg), (int)(sizeof(ROT_DOMAIN) + tl));' '    fresh.sign(sig, reinterpret_cast<const uint8_t*>(msg), (int)(sizeof(ROT_DOMAIN) + tl));' $IR::test_a_lost_rotate_result_is_recovered_from_the_stored_statement
mutate rot_statement_domain examples/companion_radio/MyMesh.cpp 'const char ROT_DOMAIN[] = "MECON-NODEROT-v1";' 'const char ROT_DOMAIN[] = "MECON-NODEROT-v2";' $IR::test_a_lost_rotate_result_is_recovered_from_the_stored_statement
mutate rotation_not_a_plain_read src/helpers/deployment/DepWrapper.cpp '"identity.rotation", "job.get", "job.list" };' '"job.get", "job.list" };' $IR::test_a_lost_rotate_result_is_recovered_from_the_stored_statement
mutate rot_stale_statement_kept examples/companion_radio/MyMesh.cpp '  if (!strstr(text, want)) { _store->removeRotation(); return; }   // the key change never took effect' '  if (!strstr(text, want)) { return; }   // the key change never took effect' $IR::test_a_cut_after_the_statement_and_before_the_key_save_leaves_the_key
mutate rotate_result_signed_with_new_key examples/companion_radio/MeconDeploy.cpp '  m->_old_id = old;   // signs this result; dispatch() wipes it' '  memset(old, 0, sizeof(*old)); delete old;' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rot_pending_drop_not_finished examples/companion_radio/MyMesh.cpp '  if (!dropOldKeyState()) return;   // stays pending: tried again at the next boot' '  if (true) return;   // stays pending: tried again at the next boot' $IR::test_a_cut_after_the_key_save_and_before_the_contacts_drop_finishes_at_boot
mutate rot_pending_never_cleared examples/companion_radio/MyMesh.cpp '  const size_t m = rotFile(out, sizeof(out), text, sig, false);' '  const size_t m = rotFile(out, sizeof(out), text, sig, true);' $IR::test_a_cut_after_the_key_save_and_before_the_contacts_drop_finishes_at_boot
mutate rot_pending_not_cleared_after_drop examples/companion_radio/MyMesh.cpp '      const size_t m = rotFile(out, sizeof(out), text, sig_b64, false);' '      const size_t m = rotFile(out, sizeof(out), text, sig_b64, true);' $IR::test_a_lost_rotate_result_is_recovered_from_the_stored_statement
mutate native_signer_signs_mecon_bytes examples/companion_radio/MyMesh.cpp '    if (sign_data && sign_data_len >= 6 && memcmp(sign_data, "MECON-", 6) == 0) {' '    if (false) {' $IR::test_the_native_signer_never_signs_bytes_that_look_like_a_mecon_message
mutate prov_result_names_no_op src/helpers/iface/Provenance.cpp '  if (a && a->op[0]) {' '  if (false) {' $PV::test_a_result_names_what_it_answers
mutate prov_result_names_no_job src/helpers/iface/Provenance.cpp '    const int n = a->has_job ? snprintf(' '    const int n = false ? snprintf(' $PV::test_a_result_names_what_it_answers
mutate prov_refusal_budget_unlimited src/helpers/iface/Provenance.cpp '  if (_tokens < 1000) return false;' '  if (_tokens < 1000) return true;' $PV::test_refusals_are_signed_within_a_budget_and_everything_else_always
mutate prov_reset_result_unsigned src/helpers/iface/Provenance.cpp '  _force = was_enrolled;' '  _force = false;' $PV::test_the_result_of_the_reset_that_removes_the_record_is_still_signed

# --dry ends here, after every mutant above has had its target checked (it used to exit two thirds of the way down)
if [ "$DRY" = 1 ]; then [ "$SURVIVED" = 0 ] && echo "DRY CHECK: every mutant target exists"; exit $SURVIVED; fi
echo "RESTORED; final clean run:"
if [ -z "$ONLY" ] && [ "$DRY" = 0 ]; then build; sim/run.sh python3 -m pytest sim/tests -q -p no:cacheprovider 2>&1 | tail -3; fi
if [ -n "$ONLY" ] && [ "$DRY" = 0 ] && [ "$MATCHED" = 0 ]; then
  echo "NOT RUN: mutant '$ONLY' was listed but never reached (is it after the end of the list?)"; SURVIVED=1
fi
exit $SURVIVED
