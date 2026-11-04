# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# message.*: outcomes correlated on the packet hash, a repeat is no second send, a text is never cut, the reads are
# private and an enrolled device sends only for a signature
MO=sim/tests/test_message_ops.py
mutate message_channel_waits_for_an_ack src/helpers/iface/MessageOps.h '  return e.state >= MSG_ACKED || (e.state == MSG_TRANSMITTED && e.channel >= 0);' '  return e.state >= MSG_ACKED;' unit:test_message_table $MO::test_a_channel_message_reaches_a_stock_receiver_and_ends_transmitted
mutate message_ack_before_a_transmission src/helpers/iface/MessageOps.cpp '    if (e.state == MSG_TRANSMITTED || e.state == MSG_TIMED_OUT) { e.state = MSG_ACKED; e.tell = true; }' '    if (e.state != MSG_ACKED) { e.state = MSG_ACKED; e.tell = true; }' unit:test_message_table
mutate message_late_ack_ignored src/helpers/iface/MessageOps.cpp '    if (e.state == MSG_TRANSMITTED || e.state == MSG_TIMED_OUT) { e.state = MSG_ACKED; e.tell = true; }' '    if (e.state == MSG_TRANSMITTED) { e.state = MSG_ACKED; e.tell = true; }' unit:test_message_table
mutate message_estimate_counts_from_the_request src/helpers/iface/MessageOps.cpp '    e.since_ms = now;   // the estimate counts from here, not from a request that may have waited an hour for airtime' '' unit:test_message_table
mutate message_evicts_an_open_entry src/helpers/iface/MessageOps.cpp '      if (messageFinal(e) && (!slot || e.seq < slot->seq)) slot = &e;' '      if (!slot || e.seq < slot->seq) slot = &e;' unit:test_message_table
mutate message_uncollected_goes_first src/helpers/iface/MessageOps.cpp '      if (messageFinal(e) && e.collected && (!slot || e.seq < slot->seq)) slot = &e;' '      if (false) slot = &e;' unit:test_message_table
mutate message_lost_report_reads_failed src/helpers/iface/MessageOps.cpp '      e.state = MSG_UNKNOWN;   // the queue lost it without saying so: never read as failed, or as sent' '      e.state = MSG_FAILED;' unit:test_message_table
mutate message_repeat_is_a_second_send src/helpers/iface/MessageOps.cpp '  if (known) {   // a retry whose reply was lost, the result cache having moved on: never a second send' '  if (false) {' $MO::test_a_repeated_request_is_one_transmission
mutate message_outcomes_full_still_sends src/helpers/iface/MessageOps.cpp '  if (!e) { c.error = "outcomes-full"; return ST_BUSY; }' '  if (!e) { static MessageEntry spare; e = &spare; }' $MO::test_a_full_table_of_open_sends_answers_busy_and_a_final_one_makes_room
mutate message_channel_text_is_cut examples/companion_radio/MyMesh.cpp '  if (strlen(_prefs.node_name) + 2 + len > MAX_TEXT_LEN) return Iface::MS_TOO_LONG;' '' $MO::test_text_too_long_is_refused_rather_than_cut
mutate message_direct_text_limit_gone src/helpers/iface/MessageOps.cpp '  if (len > h->text_max) { c.error = "text-too-long"; return ST_FAILED; }' '' $MO::test_text_too_long_is_refused_rather_than_cut
mutate message_hash_not_captured examples/companion_radio/MyMesh.cpp '  if (_msg_capture) const_cast<mesh::Packet*>(pkt)->calculatePacketHash(_msg_capture->hash);' '' $MO::test_a_direct_message_reaches_a_stock_receiver_and_its_ack_is_correlated
mutate message_hold_not_reported src/Dispatcher.cpp '        onDutyHold(outbound, wait);' '' $MO::test_a_send_waiting_for_airtime_says_so_and_still_goes
mutate message_ack_not_matched examples/companion_radio/MyMesh.cpp '    if (_iface.messages() && _iface.messages()->onAck(ack_code, to)) {' '    if (false) {' $MO::test_a_direct_message_reaches_a_stock_receiver_and_its_ack_is_correlated
mutate message_send_needs_no_signature examples/companion_radio/MeconIface.cpp '  {"message.send", opMsgSend, PRIV_WRITE, true},' '  {"message.send", opMsgSend, PRIV_READ, true},' $MO::test_an_enrolled_device_sends_only_for_a_signature
mutate message_send_not_mutating examples/companion_radio/MeconIface.cpp '  {"message.send", opMsgSend, PRIV_WRITE, true},' '  {"message.send", opMsgSend, PRIV_WRITE, false},' $MO::test_a_repeated_request_is_one_transmission
mutate message_get_is_not_private examples/companion_radio/MeconIface.cpp 'Status opMsgGet(OpContext& c) { return mayReadPrivate(c) ? withMessages(c, opMessageGet, false) : ST_REFUSED; }' 'Status opMsgGet(OpContext& c) { return withMessages(c, opMessageGet, false); }' $MO::test_a_caller_the_cli_table_does_not_trust_sends_nothing
mutate message_list_is_not_private examples/companion_radio/MeconIface.cpp 'Status opMsgList(OpContext& c) { return mayReadPrivate(c) ? withMessages(c, opMessageList, false) : ST_REFUSED; }' 'Status opMsgList(OpContext& c) { return withMessages(c, opMessageList, false); }' $MO::test_a_caller_the_cli_table_does_not_trust_sends_nothing
mutate message_event_names_no_state src/helpers/iface/MessageOps.cpp '  w.key("state"); w.str(messageStateName(e.state));
  w.endObject();
  return w.ok();' '  w.endObject();
  return w.ok();' $MO::test_a_direct_message_reaches_a_stock_receiver_and_its_ack_is_correlated
