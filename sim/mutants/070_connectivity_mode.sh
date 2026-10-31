# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
# the restart after a mode switch counts from the interface's reply, not from the line (a long config.apply lost its reply)
mutate connmode_restart_counts_from_the_line examples/companion_radio/MeconIface.cpp '    _mesh->restartConnectivityClock();' '' sim/tests/test_connmode_reply.py::test_the_reply_survives_a_slow_flash_after_the_mode_line
# Only the switch's own reply restarts the clock: a host polling every 300 ms must not postpone the restart (#226)
mutate connmode_every_reply_postpones_the_restart examples/companion_radio/MyMesh.h '    if (_connmode_restart_at != 0 && _connmode_reply_owed) {' '    if (_connmode_restart_at != 0) {' sim/tests/test_connmode_reply.py::test_a_host_polling_faster_than_the_delay_does_not_postpone_the_restart
