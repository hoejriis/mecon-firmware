# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
