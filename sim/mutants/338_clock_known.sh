# Only a real source sets the clock: nothing states a seed or a contact's time as the time (docs/contract/DEVICE_INTERFACE.md).
CK=sim/tests/test_clock_known.py
MI=examples/companion_radio/MeconIface.cpp
mutate clock_rx_ts_from_a_guess examples/companion_radio/MyMesh.cpp "e->rx_ts = getRTCClock()->knownTime();" "e->rx_ts = getRTCClock()->getCurrentTime();" $CK::test_a_message_has_no_received_time_until_the_clock_is_set
mutate clock_inbox_event_from_a_guess $MI "const uint32_t ts = _mesh->getRTCClock()->knownTime();
  int n = writeEvent(ev, RESULT_MAX, ev_name," "const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
  int n = writeEvent(ev, RESULT_MAX, ev_name," $CK::test_a_message_has_no_received_time_until_the_clock_is_set
mutate clock_observation_from_a_guess $MI "const uint32_t ts = m->_mesh->getRTCClock()->knownTime();
  int n = writeObservation(" "const uint32_t ts = m->_mesh->getRTCClock()->getCurrentTime();
  int n = writeObservation(" $CK::test_a_message_has_no_received_time_until_the_clock_is_set
mutate clock_stream_event_from_a_guess $MI "const uint32_t ts = _mesh->getRTCClock()->knownTime();
  const bool ts_known" "const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
  const bool ts_known" $CK::test_a_stream_event_has_no_time_until_the_clock_is_set
mutate clock_inventory_from_a_guess $MI "const uint32_t ts = _mesh->getRTCClock()->knownTime();
    int n = writeEvent(ev, INV_EVENT," "const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
    int n = writeEvent(ev, INV_EVENT," $CK::test_an_inventory_event_has_no_time_until_the_clock_is_set
mutate clock_log_event_from_a_guess $MI "const uint32_t ts = _mesh->getRTCClock()->knownTime();
  for (int j = 0; j < k; j++) {" "const uint32_t ts = _mesh->getRTCClock()->getCurrentTime();
  for (int j = 0; j < k; j++) {" $CK::test_an_audit_entry_keeps_no_guessed_time
mutate clock_job_event_from_a_guess examples/companion_radio/MeconDeploy.cpp "const uint32_t ts = m->_mesh->getRTCClock()->knownTime();" "const uint32_t ts = m->_mesh->getRTCClock()->getCurrentTime();" $CK::test_a_job_event_has_no_time_until_the_clock_is_set
mutate clock_repeater_job_event_from_a_guess examples/simple_repeater/main.cpp "return the_mesh.getRTCClock()->knownTime();" "return the_mesh.getRTCClock()->getCurrentTime();" "$CK::test_a_job_event_on_a_server_role_has_no_time_until_the_clock_is_set"
mutate clock_room_job_event_from_a_guess examples/simple_room_server/main.cpp "return the_mesh.getRTCClock()->knownTime();" "return the_mesh.getRTCClock()->getCurrentTime();" "$CK::test_a_job_event_on_a_server_role_has_no_time_until_the_clock_is_set"
mutate clock_audit_keeps_a_guess src/helpers/AuditRing.h "    if (_clock_set && !_clock_set()) ts = 0;   // a guessed time is not a record of when
" "" $CK::test_an_audit_entry_keeps_no_guessed_time
mutate clock_audit_check_not_wired examples/companion_radio/main.cpp "  Posture::audit().setClockCheck([]() -> bool { return rtc_clock.isSet(); });
" "" $CK::test_an_audit_entry_keeps_no_guessed_time
mutate clock_a_contact_sets_it src/helpers/BaseChatMesh.cpp "getRTCClock()->setGuess(latest + 1);" "getRTCClock()->setCurrentTime(latest + 1);" $CK::test_a_contacts_time_runs_the_clock_on_but_does_not_set_it
mutate clock_a_contact_replaces_a_set_time src/helpers/BaseChatMesh.cpp "  if (getRTCClock()->isSet()) return;   // a real time, kept over a soft reset or on a chip: a guess must not replace it
" "" $CK::test_a_set_clock_is_not_replaced_by_a_contacts_time
