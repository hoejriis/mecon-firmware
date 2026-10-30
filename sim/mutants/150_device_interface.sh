# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
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
mutate iface_mqtt_events_not_published examples/companion_radio/MeconIface.cpp '  if (_mqtt_subs.s[s].active && _ev_publish) _ev_publish(STREAMS[s].name, ev, n);' '' sim/tests/test_device_interface.py::test_health_events_over_mqtt
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
mutate stream_power_claims_to_tell src/helpers/iface/Streams.h '  const bool can_tell = b.isPwrMgtInitialised() || b.hasPowerDetect();' '  const bool can_tell = true;' sim/tests/test_telemetry_streams.py::test_each_stream_sends_its_first_event_at_once
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
mutate obs_limiter_refills_to_full src/helpers/iface/Streams.h '      tokens = tokens + refill > RATE ? RATE : tokens + refill;' '      tokens = RATE;' unit:test_streams
mutate lpp_sign_ignored src/helpers/iface/Lpp.cpp '    if (t->is_signed && (raw & (1u << (8 * t->size - 1)))) v -= (int64_t)1 << (8 * t->size);' '' unit:test_lpp
mutate lpp_everything_signed src/helpers/iface/Lpp.cpp '    if (t->is_signed && (raw & (1u << (8 * t->size - 1))))' '    if ((raw & (1u << (8 * t->size - 1))))' unit:test_lpp
mutate lpp_gps_not_skipped src/helpers/iface/Lpp.cpp '    if (code == LPP_GPS) {' '    if (false) {' unit:test_lpp
mutate env_offered_without_sensors src/helpers/iface/ConsoleIface.h '    return STREAMS_PRODUCED | (_env_ok ? (1u << SID_ENVIRONMENT) : 0) | (_loc_ok ? (1u << SID_LOCATION) : 0);' '    return STREAMS_PRODUCED | (1u << SID_ENVIRONMENT) | (_loc_ok ? (1u << SID_LOCATION) : 0);' sim/tests/test_telemetry_streams.py::test_environment_is_absent_without_sensors
mutate env_not_checked_before_first_answer src/helpers/iface/ConsoleIface.cpp '  if (!_env_checked) refreshEnvironment(millis());' '' sim/tests/test_telemetry_streams.py::test_environment_carries_the_attached_sensors
mutate inv_empty_neighbour_slots_counted examples/simple_repeater/MyMesh.h '    for (int i = 0; i < MAX_NEIGHBOURS; i++) if (neighbours[i].heard_timestamp) n++;' '    for (int i = 0; i < MAX_NEIGHBOURS; i++) n++;' sim/tests/test_telemetry_streams.py::test_inventory_counts_a_heard_neighbour_on_the_repeater
mutate inv_contacts_swapped examples/companion_radio/MeconIface.cpp '      writeCount(w, "contacts", _mesh->getNumContacts(), _mesh->getContactCapacity());' '      writeCount(w, "contacts", _mesh->getContactCapacity(), _mesh->getNumContacts());' sim/tests/test_telemetry_streams.py::test_inventory_counts_on_the_companion
# telemetry: link state, runtime boot event and heap_low, the logs stream
mutate conn_ble_state_not_reported examples/companion_radio/main.cpp '  w.key("connected"); w.boolean(interface_manager.isInterfaceConnected(InterfaceType::Bluetooth));' '  w.key("connected"); w.boolean(true);' sim/tests/test_telemetry_streams.py::test_ble_link_changes_are_events
mutate conn_ble_change_not_detected examples/companion_radio/main.cpp '  sig |= (uint64_t)(interface_manager.isInterfaceConnected(InterfaceType::Bluetooth) ? 1 : 0) << 32;' '' sim/tests/test_telemetry_streams.py::test_ble_link_changes_are_events
mutate conn_usb_claims_a_host examples/companion_radio/main.cpp '  return Serial ? 1 : 0;' '  return 1;' sim/tests/test_telemetry_streams.py::test_usb_link_changes_are_events
mutate conn_usb_change_not_detected examples/companion_radio/main.cpp '  sig |= (uint64_t)(ifaceUsbHostLink() + 1) << 33;' '' sim/tests/test_telemetry_streams.py::test_usb_link_changes_are_events
mutate conn_signature_truncated examples/companion_radio/MeconIface.cpp '    const uint64_t sig = _conn_sig();' '    const uint32_t sig = _conn_sig();' sim/tests/test_telemetry_streams.py::test_ble_link_changes_are_events
mutate runtime_heap_low_inverted examples/companion_radio/MeconIface.cpp 'heapFloor().low() ? 1 : 0) : -1);' 'heapFloor().low() ? 0 : 1) : -1);' sim/tests/test_telemetry_streams.py::test_runtime_carries_heap_low_on_the_companion
mutate runtime_heap_low_on_the_repeater src/helpers/iface/ConsoleIface.cpp 'case SID_RUNTIME: writeRuntime(w, *_host.board, _gap); break;' 'case SID_RUNTIME: writeRuntime(w, *_host.board, _gap, 0); break;' sim/tests/test_telemetry_streams.py::test_runtime_has_no_heap_low_on_the_repeater
mutate boot_event_resent_companion examples/companion_radio/MeconIface.cpp '      _boot_sent = true;' '      _boot_sent = false;' sim/tests/test_telemetry_streams.py::test_the_boot_event_comes_once_per_epoch
mutate boot_event_resent_console src/helpers/iface/ConsoleIface.cpp '      _boot_sent = true;' '      _boot_sent = false;' sim/tests/test_telemetry_streams.py::test_the_boot_event_comes_once_per_epoch
mutate boot_event_never_sent src/helpers/iface/ConsoleIface.h '    if (stream == SID_RUNTIME && !_boot_sent) _boot_pending = true;' '' sim/tests/test_telemetry_streams.py::test_the_boot_event_comes_once_per_epoch
mutate boot_event_hardware_is_role src/helpers/iface/Streams.cpp '  w.key("hardware"); w.str(hardware);' '  w.key("hardware"); w.str(role);' sim/tests/test_telemetry_streams.py::test_the_boot_event_comes_once_per_epoch
mutate logs_class_not_checked src/helpers/iface/Streams.cpp '  if (!allow || !allow(allow_user, def.cls)) return ST_REFUSED;' '' sim/tests/test_device_interface.py::test_logs_are_refused_over_a_verified_mqtt_session_but_not_to_the_cable
mutate logs_class_is_read src/helpers/iface/Streams.cpp '  {"logs", 0, 0, 0, Cli::C_POSTURE},' '  {"logs", 0, 0, 0, Cli::C_READ},' sim/tests/test_device_interface.py::test_logs_are_refused_over_a_verified_mqtt_session_but_not_to_the_cable
mutate logs_rate_unlimited src/helpers/iface/Streams.h '      if (limit.take(now)) out[n++] = r.e[i];' '      limit.take(now); out[n++] = r.e[i];' sim/tests/test_telemetry_streams.py::test_logs_rate_is_limited_and_drops_are_counted
mutate logs_unset_clock_reported_as_time src/helpers/iface/Streams.h '  if (a.ts >= 1704067200) snprintf' '  if (true) snprintf' unit:test_streams
mutate logs_what_ignores_the_event src/helpers/iface/Streams.h 'Posture::AuditRing::whatName(a.event, a.what)' 'Posture::AuditRing::whatName(0, a.what)' unit:test_streams
mutate logs_companion_never_polled examples/companion_radio/MeconIface.cpp '  const int k = _logs.poll(millis(), Posture::audit(), a, &dropped);' '  const int k = 0;' sim/tests/test_telemetry_streams.py::test_logs_send_a_posture_change_and_a_refusal_with_names
mutate logs_console_never_polled src/helpers/iface/ConsoleIface.cpp '    const int k = _logs.poll(now, Posture::audit(), a, &dropped);' '    const int k = 0;' sim/tests/test_telemetry_streams.py::test_logs_on_the_repeater_console
mutate logs_history_sent_on_subscribe examples/companion_radio/MeconIface.cpp '    _logs.skip(Posture::audit());   // what is recorded while nobody listens is history' '' sim/tests/test_telemetry_streams.py::test_logs_do_not_replay_history
mutate logs_poll_ignores_serial src/helpers/iface/Streams.h '      if ((int32_t)(r.serial[i] - (r.recorded - fresh)) <= 0) continue;   // recorded before the last poll' '' unit:test_streams
mutate logs_lost_records_not_counted src/helpers/iface/Streams.h '    limit.dropped += fresh - held;   // recorded since, but no longer shown' '' unit:test_streams
mutate logs_drops_never_cleared src/helpers/iface/Streams.h '    limit.dropped = 0;
    return n;' '    return n;' unit:test_streams
mutate audit_record_not_counted src/helpers/AuditRing.h '    serial[n] = ++recorded;' '    serial[n] = recorded;' unit:test_posture
mutate audit_serial_not_moved_with_a_fold src/helpers/AuditRing.h '      for (int i = at; i < n - 1; i++) { e[i] = e[i + 1]; serial[i] = serial[i + 1]; }' '      for (int i = at; i < n - 1; i++) { e[i] = e[i + 1]; }' unit:test_posture
mutate inv_repeater_callback_missing examples/simple_repeater/main.cpp 'ifaceRadioCounts, &sensors, ifaceInventory,' 'ifaceRadioCounts, &sensors, nullptr,' sim/tests/test_telemetry_streams.py::test_inventory_counts_a_heard_neighbour_on_the_repeater
