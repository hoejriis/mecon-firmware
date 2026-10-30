# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
mutate deploy_unsigned_write_after_enrolment src/helpers/iface/DeployAuth.cpp '  if (_dep_ls != Dep::LS_ABSENT && !_signed && !plain_read &&' '  if (false && !_signed && !plain_read &&' $DA::test_unsigned_writes_are_refused_once_enrolled_but_reads_work
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
mutate server_record_store_not_given examples/simple_room_server/main.cpp '                                       &dep_store, ifaceClock, &the_mesh.self_id, &job_store, &snap_store };' '                                       nullptr, ifaceClock, &the_mesh.self_id, &job_store, &snap_store };' $DS::test_first_provisioning_over_the_console_and_the_read_back
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
mutate rotate_result_signed_with_new_key examples/companion_radio/MeconDeploy.cpp '  m->_old_id = old;   // signs this result; dispatch() wipes it' '  Iface::wipe(old, sizeof(*old)); delete old;' $IR::test_rotation_changes_the_key_and_keeps_everything_else
mutate rot_pending_drop_not_finished examples/companion_radio/MyMesh.cpp '  if (!dropOldKeyState()) return;   // stays pending: tried again at the next boot' '  if (true) return;   // stays pending: tried again at the next boot' $IR::test_a_cut_after_the_key_save_and_before_the_contacts_drop_finishes_at_boot
mutate rot_pending_never_cleared examples/companion_radio/MyMesh.cpp '  const size_t m = rotFile(out, sizeof(out), text, sig, false);' '  const size_t m = rotFile(out, sizeof(out), text, sig, true);' $IR::test_a_cut_after_the_key_save_and_before_the_contacts_drop_finishes_at_boot
mutate rot_pending_not_cleared_after_drop examples/companion_radio/MyMesh.cpp '      const size_t m = rotFile(out, sizeof(out), text, sig_b64, false);' '      const size_t m = rotFile(out, sizeof(out), text, sig_b64, true);' $IR::test_a_lost_rotate_result_is_recovered_from_the_stored_statement
mutate native_signer_signs_mecon_bytes examples/companion_radio/MyMesh.cpp '    if (sign_data && sign_data_len >= 6 && memcmp(sign_data, "MECON-", 6) == 0) {' '    if (false) {' $IR::test_the_native_signer_never_signs_bytes_that_look_like_a_mecon_message
mutate prov_result_names_no_op src/helpers/iface/Provenance.cpp '  if (a && a->op[0]) {' '  if (false) {' $PV::test_a_result_names_what_it_answers
mutate prov_result_names_no_job src/helpers/iface/Provenance.cpp '    const int n = a->has_job ? snprintf(' '    const int n = false ? snprintf(' $PV::test_a_result_names_what_it_answers
mutate prov_refusal_budget_unlimited src/helpers/iface/Provenance.cpp '  if (_tokens < 1000) return false;' '  if (_tokens < 1000) return true;' $PV::test_refusals_are_signed_within_a_budget_and_everything_else_always
mutate prov_reset_result_unsigned src/helpers/iface/Provenance.cpp '  _force = was_enrolled;' '  _force = false;' $PV::test_the_result_of_the_reset_that_removes_the_record_is_still_signed

# the signed device_id binding (docs/contract/AUTHORITY.md section 3.5)
DB=sim/tests/test_device_id_binding.py
mutate devid_domain_wrong src/helpers/iface/DevIdSig.h 'const char DEVID_DOMAIN[] = "MECON-DEVID-v1";' 'const char DEVID_DOMAIN[] = "MECON-DEVID-v0";' unit:test_deployment_vectors
mutate devid_key_not_signed src/helpers/iface/DevIdSig.h '  memcpy(out + n, node_key, 32);' '  memset(out + n, 0, 32);' unit:test_deployment_vectors
mutate devid_nonce_not_signed src/helpers/iface/DevIdSig.h '  return n + nl;' '  return n;' unit:test_deployment_vectors
mutate devid_nonce_unchecked src/helpers/iface/DevIdSig.h '  if (nl && !devIdNonceValid(nonce, nl)) return 0;' '  if (false) return 0;' unit:test_deployment_vectors
mutate devid_nonce_one_too_long src/helpers/iface/DevIdSig.h 'len > DEVID_NONCE_MAX) return false;' 'len > DEVID_NONCE_MAX + 1) return false;' unit:test_deployment_vectors
mutate devid_hello_companion_unsigned examples/companion_radio/MeconIface.cpp '  const Status ds = writeDeviceIdSig(c, mesh->self_id, m->deviceIdHex());' '  const Status ds = ST_OK;' $DB::test_companion_hello_and_cli_sign_the_device_id
mutate devid_hello_console_unsigned src/helpers/iface/ConsoleIface.cpp '    const Status ds = writeDeviceIdSig(c, *m->host().identity, m->deviceIdHex());' '    const Status ds = ST_OK;' $DB::test_console_hello_and_cli_sign_the_device_id
mutate devid_hello_bad_nonce_accepted src/helpers/iface/DevIdHello.h '  if (t >= 0 && !jsonGetString(*c.doc, t, nonce, sizeof(nonce), CS_NAME, 1)) return ST_INVALID;' '  if (false) return ST_INVALID;' $DB::test_companion_hello_and_cli_sign_the_device_id
mutate devid_cli_server_any_suffix src/helpers/CommonCLI.cpp '(config[9] == 0 || config[9] == '"' '"')) {' 'true) {' $DB::test_console_hello_and_cli_sign_the_device_id
mutate devid_cli_companion_nonce_ignored examples/companion_radio/MyMesh.cpp '    const char* nonce = has_arg ? &command[14] : "";' '    const char* nonce = "";' $DB::test_companion_hello_and_cli_sign_the_device_id
mutate devid_readback_without_pair src/helpers/iface/DeployAuth.cpp '  if (bind_key) {' '  if (false) {' $DB::test_companion_provisioning_read_back_carries_the_pair
mutate devid_duplicate_readback_without_pair src/helpers/iface/DeployAuth.cpp '      writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false, _env.node_key);' '      writeFields(w, *_dep, k->cur.rec, k->cur.set_text, false);' $DB::test_companion_provisioning_read_back_carries_the_pair

mutate devid_rotation_read_open examples/companion_radio/MeconIface.cpp '  if (strcmp(op.name, "deployment.get") == 0 || strcmp(op.name, "deployment.authority_update") == 0) return true;' '  if (strcmp(op.name, "deployment.get") == 0 || strcmp(op.name, "identity.rotation") == 0 || strcmp(op.name, "deployment.authority_update") == 0) return true;' $DB::test_after_a_rotation_hello_signs_with_the_new_key_and_untrusted_links_never_see_the_id
mutate devid_get_carries_rotation_to_anyone examples/companion_radio/MeconDeploy.cpp '  if (st == ST_OK && Cli::decide(self(c)->_ctx, Cli::C_READ).allow) writeRotationMember' '  if (st == ST_OK) writeRotationMember' $DB::test_after_a_rotation_hello_signs_with_the_new_key_and_untrusted_links_never_see_the_id
mutate devid_cli_empty_nonce_fixed_form src/helpers/CommonCLI.cpp '    else if (has_arg && !*nonce) strcpy(reply, "Error: bad nonce");' '    else if (false) strcpy(reply, "Error: bad nonce");' $DB::test_console_hello_and_cli_sign_the_device_id
mutate cache_hit_under_another_op src/helpers/iface/IfaceCache.h '      if (strcmp(_e[i].op, op) != 0) return CONFLICT;' '      if (false) return CONFLICT;' $DB::test_a_stored_write_result_cannot_be_fetched_under_another_operation
mutate request_depth_too_shallow_for_broker_roles src/helpers/iface/IfaceJson.h 'const int MAX_DEPTH = 5;' 'const int MAX_DEPTH = 4;' unit:test_iface sim/tests/test_deployment_authority.py::test_broker_roles_are_parsed_over_the_cable_and_signed
