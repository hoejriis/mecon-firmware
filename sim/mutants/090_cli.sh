# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
