# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# The Repeater and Room server on a broker: each line runs as the session, the same trust classes, and repeating apart
SM=sim/tests/test_server_mqtt.py
mutate server_net_line_runs_as_the_cable src/helpers/cli/CliCaller.h '  if (console && s.net_line) {' '  if (false) {' $SM::test_a_verified_session_never_gets_what_the_table_keeps_from_it
mutate server_mgmt_flag_only_on_a_companion src/helpers/cli/CliRegistry.h '  if (isMgmtFlag(cmd)) {' '  if (role == ROLE_COMPANION && isMgmtFlag(cmd)) {' $SM::test_a_verified_session_never_gets_what_the_table_keeps_from_it
mutate server_cli_refusal_reads_as_ok src/helpers/net/ServerNet.h '  if (!allowed) return Cli::Mqtt::S_REFUSED;' '' $SM::test_a_verified_session_never_gets_what_the_table_keeps_from_it
mutate server_broker_shares_the_console_nonce src/helpers/iface/ConsoleIface.h 'return _auth.helloNonce(_on_mqtt ? _mqtt_nonce : _nonce);' 'return _auth.helloNonce(_nonce);' $SM::test_signed_logs_over_a_verified_broker_on_a_repeater
mutate server_logs_for_any_broker_caller src/helpers/iface/ConsoleIface.cpp '    if (!Cli::decide(s->_ctx, cls).allow) return false;
    if (cls == Cli::C_READ' '    if (cls == Cli::C_READ || cls == Cli::C_POSTURE) return true;
    if (cls == Cli::C_READ' $SM::test_signed_logs_over_a_verified_broker_on_a_repeater
mutate server_broker_subs_are_the_console_s src/helpers/iface/ConsoleIface.h 'SubSet& subs() { return _on_mqtt ? _mqtt_subs : _subs; }' 'SubSet& subs() { return _subs; }' $SM::test_connectivity_and_health_reach_the_broker_and_a_subscription_lapses_with_the_session
mutate server_subs_outlive_the_session src/helpers/iface/ConsoleIface.cpp '    _mqtt_subs.clear();   // a subscription made over the broker lapses with the session' '' $SM::test_connectivity_and_health_reach_the_broker_and_a_subscription_lapses_with_the_session
mutate server_no_connectivity_stream src/helpers/net/ServerNet.h '  iface.setConnectivity(Iface::writeNetConnectivity, Iface::netConnSignature);   // reported even while Wi-Fi is off' '' $SM::test_connectivity_and_health_reach_the_broker_and_a_subscription_lapses_with_the_session
mutate server_typing_holds_the_broker_events src/helpers/iface/ConsoleIface.cpp '  if (_collecting && !mqttSubscribed()) return;' '  if (_collecting) return;' $SM::test_a_broker_event_goes_out_while_a_console_line_is_being_typed
mutate server_write_only_not_snapshotted src/helpers/iface/ConsoleIface.cpp '_host.read_secret ? confirmReadSecret : nullptr, confirmRecordCaller,' 'nullptr, confirmRecordCaller,' $SM::test_a_setting_applied_over_the_broker_needs_a_confirmation_and_comes_back_without_one
mutate server_job_event_not_on_the_broker src/helpers/iface/ConsoleIface.cpp '  if (n > 0 && net) s->_ev_publish("job", ev, (size_t)n);' '' $SM::test_a_setting_applied_over_the_broker_needs_a_confirmation_and_comes_back_without_one
mutate server_broker_roles_not_told src/helpers/net/ServerNet.h '    iface.setRolesListener([](const uint8_t* t, uint8_t n, void*) { mqttController().setBrokerTiers(t, n); }, nullptr);' '' $SM::test_the_deployment_broker_roles_order_the_brokers_on_a_server
mutate server_no_broker_profile_exists src/helpers/iface/ConsoleIface.cpp 'env.profile_exists = _host.profile_exists ? authProfileExists : nullptr;' 'env.profile_exists = nullptr;' $SM::test_the_deployment_broker_roles_order_the_brokers_on_a_server
mutate server_hardened_starts_wifi_for_a_plain_broker src/helpers/net/ServerNet.h '  if (on && Posture::isHardened(p.posture) && !mqtt_may_run) {' '  if (false) {' $SM::test_hardened_keeps_a_verified_broker_and_starts_no_wifi_for_a_plain_one
mutate server_settings_not_stored src/helpers/CommonCLI.h '    def("mqtt", mqtt);' '' $SM::test_the_settings_survive_a_reboot_and_the_session_comes_back
mutate server_wifi_keys_on_every_build src/helpers/iface/IfaceConfig.cpp '#ifdef ENABLE_WIFI_INTERFACE
#include "IfaceNetKeys.inc"
#endif' '#include "IfaceNetKeys.inc"' $SM::test_no_profile_starts_no_link_and_the_build_without_wifi_has_no_settings
mutate server_net_blocks_repeating examples/simple_repeater/main.cpp '  if (net_started) ServerNet::loop();' '  if (net_started) { ServerNet::loop(); return; }' $SM::test_repeating_goes_on_with_a_session_up_and_with_the_broker_gone
mutate server_clock_check_not_wired src/helpers/net/ServerNet.h '    mqttController().setClockKnown(clock_known);' '' $SM::test_the_clock_decides_whether_a_servers_anchor_judges_dates
