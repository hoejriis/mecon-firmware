# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
