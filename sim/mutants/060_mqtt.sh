# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
