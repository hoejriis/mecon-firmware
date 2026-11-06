# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the private class and the location stream (TELEMETRY.md; owner's rule 2026-10-28: "All this should go via the broker to the backend")
PS=sim/tests/test_private_streams.py
mutate location_class_posture src/helpers/iface/Streams.cpp '  {"location", 30, 3600, 300, Cli::C_PRIVATE},' '  {"location", 30, 3600, 300, Cli::C_POSTURE},' $PS::test_location_reaches_a_verified_broker_session
mutate private_refused_to_the_broker src/helpers/cli/CliPolicy.h '/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW, ALLOW, NOT_PERMITTED),' '/* MQTT verified  N */ { CLI_ROW(ALLOW,           ALLOW,           NOT_PERMITTED,   NOT_PERMITTED,   NOT_PERMITTED,   ALLOW, NOT_PERMITTED, NOT_PERMITTED),' unit:test_cli_engine
mutate private_to_a_lora_admin src/helpers/cli/CliPolicy.h '/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW, NOT_PERMITTED, NOT_PERMITTED),' '/* remote admin   N */ { CLI_ROW(ALLOW,           ALLOW,           LOCAL_ONLY,      NOT_PERMITTED,   LOCAL_ONLY,      ALLOW, ALLOW, NOT_PERMITTED),' unit:test_cli_engine
mutate location_position_without_fix src/helpers/iface/Streams.h '  w.key("lat_e6"); if (f.fix) w.num(f.lat_e6); else w.null();' '  w.key("lat_e6"); w.num(f.lat_e6);' unit:test_streams
mutate location_age_never_null src/helpers/iface/Streams.h '  w.key("age_s"); if (f.age_s >= 0) w.unum((uint32_t)f.age_s); else w.null();' '  w.key("age_s"); w.unum((uint32_t)f.age_s);' unit:test_streams
mutate location_altitude_in_mm src/helpers/iface/Streams.cpp '    out.alt_m = (int32_t)(p->getAltitude() / 1000);' '    out.alt_m = (int32_t)p->getAltitude();' $PS::test_location_carries_the_fix_and_nulls_without_one
mutate location_claimed_without_receiver_companion examples/companion_radio/MeconIface.cpp '  _loc_ok = sensors.getLocationProvider() != nullptr;' '  _loc_ok = true;' "$PS::test_location_is_absent_without_a_receiver[companion_standalone]"
mutate location_claimed_without_receiver_console src/helpers/iface/ConsoleIface.cpp '  _loc_ok = _host.sensors->getLocationProvider() != nullptr;' '  _loc_ok = true;' "$PS::test_location_is_absent_without_a_receiver[repeater]"
mutate inv_page_without_tail_room src/helpers/iface/Streams.h '    const size_t need = (_count ? 1 : 0) + len + TAIL;' '    const size_t need = (_count ? 1 : 0) + len;' unit:test_streams
mutate inv_contact_lost_at_page_boundary examples/companion_radio/MeconIface.cpp '      _inv_cursor = before;              // this contact opens the next page' '' $PS::test_inventory_snapshot_carries_every_contact_and_channel_once_and_no_secret
mutate inv_snapshot_never_ends examples/companion_radio/MeconIface.cpp '  const size_t dn = ok ? page.finish(done) : 0;' '  const size_t dn = ok ? page.finish(false) : 0;' $PS::test_inventory_snapshot_carries_every_contact_and_channel_once_and_no_secret
# the security review of the private streams
mutate private_link_need_not_be_allowed examples/companion_radio/MeconIface.cpp '  if (cls == Cli::C_READ || Cli::decide(m->_link_ctx, cls).allow) return true;' '  return true;' $PS::test_a_signed_request_on_an_untrusted_link_gets_no_private_stream_over_it
mutate subs_survive_a_disconnect examples/companion_radio/MeconIface.cpp '      l.subs.clear();              // and its subscriptions and queued messages: the next client on it inherits nothing' '' $PS::test_a_subscription_does_not_pass_to_the_next_client_on_the_link
mutate json_utf8_unchecked src/helpers/iface/IfaceJson.cpp '      const size_t k = utf8Len(b + i, n - i);' '      const size_t k = 1;' unit:test_iface
mutate queue_room_check_too_generous src/helpers/iface/IfaceFrames.h '           len <= _cap - RESULT_RESERVE - _used;' '           len <= _cap - _used;' unit:test_iface
# the logs to a signed subscriber over a verified broker (#248)
mutate signed_logs_refused examples/companion_radio/MeconIface.cpp '  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED && Cli::decide(m->_link_ctx, Cli::C_PRIVATE).allow;' '  return false;' sim/tests/test_private_streams.py::test_a_signed_subscriber_takes_the_logs_over_a_verified_broker
mutate signed_logs_on_any_link examples/companion_radio/MeconIface.cpp '  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED && Cli::decide(m->_link_ctx, Cli::C_PRIVATE).allow;' '  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED;' sim/tests/test_private_streams.py::test_a_signed_request_on_an_untrusted_link_gets_no_private_stream_over_it
mutate signed_logs_on_an_unverified_broker examples/companion_radio/MeconIface.cpp '  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED && Cli::decide(m->_link_ctx, Cli::C_PRIVATE).allow;' '  return cls == Cli::C_POSTURE && m->_ctx.auth == Cli::A_SIGNED;' sim/tests/test_private_streams.py::test_a_signed_subscriber_gets_no_logs_over_an_unverified_broker
# the screen stream
TS=sim/tests/test_telemetry_streams.py
mutate screen_pin_frame_sent src/helpers/iface/ScreenFeed.h '    if (_d->secretDrawn()) { _withheld = "pairing-pin"; return; }' '' $TS::test_a_frame_that_shows_the_pairing_pin_is_withheld
mutate screen_pin_not_noted examples/companion_radio/ui-new/UITask.cpp '        display.noteSecretDrawn();' '' $TS::test_a_frame_that_shows_the_pairing_pin_is_withheld
mutate screen_pin_mark_never_clears variants/sim/sim_display.cpp 'void SimDisplay::startFrame(ColorVal) { frameBegun(); ' 'void SimDisplay::startFrame(ColorVal) { ' $TS::test_a_frame_that_shows_the_pairing_pin_is_withheld
mutate screen_one_part_only src/helpers/iface/ScreenFeed.h 'const bool last = _withheld || _off + n >= _bytes;' 'const bool last = true;' $TS::test_a_screen_frame_arrives_in_parts_and_is_what_the_display_shows
mutate screen_offset_stuck src/helpers/iface/ScreenFeed.h '    _off += n;' '' $TS::test_a_screen_frame_arrives_in_parts_and_is_what_the_display_shows
mutate screen_frame_not_copied variants/sim/sim_display.cpp 'memcpy(out, _frame.data(), _frame.size() < 1024 ? _frame.size() : 1024);' '' $TS::test_a_screen_frame_arrives_in_parts_and_is_what_the_display_shows
mutate screen_period_unbounded src/helpers/iface/Streams.cpp '{"screen", 5, 3600, 60, Cli::C_PRIVATE}' '{"screen", 1, 3600, 60, Cli::C_PRIVATE}' $TS::test_a_stream_period_under_five_seconds_is_refused
mutate screen_console_not_wired src/helpers/iface/ConsoleIface.cpp '  screenTick(now);
' '' $TS::test_a_repeater_with_a_display_streams_it_over_the_console_and_one_without_has_no_stream
mutate screen_companion_frame_unsigned examples/companion_radio/MeconIface.cpp '    if (n > 0) n = _prov.sign(STREAMS[SID_SCREEN].name, ev, (size_t)n, ScreenFeed::EVENT_CAP);' '' sim/tests/test_private_streams.py::test_an_enrolled_device_signs_its_screen_frames
