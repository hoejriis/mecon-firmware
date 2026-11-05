# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# inbox.*: the manager's copy is apart from the native queue, what cannot be kept is counted, an ack is held to its boot,
# and the reads are private
IB=sim/tests/test_inbox.py
mutate inbox_oldest_direct_goes_first src/helpers/iface/InboxOps.cpp '      if (_e[i].channel >= 0 && (!slot || _e[i].seq < slot->seq)) slot = &_e[i];' '      if (false) slot = &_e[i];' unit:test_inbox
mutate inbox_pushed_out_not_counted src/helpers/iface/InboxOps.cpp '        if (!slot || _e[i].seq < slot->seq) slot = &_e[i];
    _lost++;' '        if (!slot || _e[i].seq < slot->seq) slot = &_e[i];' unit:test_inbox
mutate inbox_no_slots_not_counted src/helpers/iface/InboxOps.cpp '  if (!_e) { _lost++; return nullptr; }' '  if (!_e) return nullptr;' unit:test_inbox
mutate inbox_ack_keeps_the_text src/helpers/iface/InboxOps.cpp '    if (_e[i].seq && _e[i].seq <= seq) memset(&_e[i], 0, sizeof(_e[i]));' '    if (_e[i].seq && _e[i].seq <= seq) _e[i].seq = 0;' unit:test_inbox
mutate inbox_ack_moves_the_mark_back src/helpers/iface/InboxOps.cpp '  if (seq > _acked) _acked = seq;' '  _acked = seq;' unit:test_inbox
mutate inbox_slot_reuse_keeps_the_text src/helpers/iface/InboxOps.cpp '  memset(slot, 0, sizeof(*slot));
  slot->seq = _received;' '  slot->seq = _received;' unit:test_inbox
mutate inbox_event_names_the_text src/helpers/iface/InboxOps.cpp '  w.key("lost"); w.unum(b.lost());
  w.endObject();
  return w.ok();' '  w.key("lost"); w.unum(b.lost());
  if (b.nextAfter(0)) { w.key("text"); w.str(b.nextAfter(0)->text); }
  w.endObject();
  return w.ok();' unit:test_inbox
mutate inbox_old_epoch_acks src/helpers/iface/InboxOps.cpp '  if ((uint32_t)epoch != h->epoch) { c.error = "wrong-epoch"; return ST_FAILED; }' '' $IB::test_an_ack_from_before_a_reboot_releases_nothing_received_since
mutate inbox_ack_above_received src/helpers/iface/InboxOps.cpp '  if ((uint32_t)upto > h->inbox->received()) { c.error = "not-received"; return ST_FAILED; }' '' $IB::test_a_reboot_loses_the_copy_and_the_epoch_says_so
mutate inbox_page_may_overflow src/helpers/iface/InboxOps.cpp '    if (n > 0 && w.size() + ENTRY_FIELDS_MAX + textJsonMax(*e) > PAGE_MAX) break;' '' $IB::test_a_page_stops_where_a_reply_would_overflow_and_the_next_read_continues
mutate inbox_limit_ignored src/helpers/iface/InboxOps.cpp '  for (int n = 0; e && n < limit; n++) {' '  for (int n = 0; e; n++) {' $IB::test_a_full_inbox_counts_what_it_loses_and_the_oldest_goes
mutate inbox_copy_drains_native examples/companion_radio/MeconIface.cpp '  _inbox_told = _inbox.received();' '  _inbox_told = _inbox.received(); { uint8_t f[256]; _mesh->getFromOfflineQueue(f); }' $IB::test_a_direct_message_is_read_and_the_stock_app_still_has_it
mutate inbox_not_private examples/companion_radio/MeconIface.cpp '  if (!mayReadPrivate(c)) return ST_REFUSED;
  InboxHost host' '  InboxHost host' $IB::test_a_caller_who_may_not_read_private_data_reads_nothing
mutate inbox_no_copy_of_a_direct examples/companion_radio/MyMesh.cpp '  if (!keepReceived(pkt, from.id.pub_key, -1, txt_type, sender_timestamp, extra_len >= 4 ? extra : nullptr, text)) return;' '' $IB::test_a_direct_message_is_read_and_the_stock_app_still_has_it
mutate inbox_no_copy_of_a_channel examples/companion_radio/MyMesh.cpp '  if (channel_idx < 127 && !keepReceived(pkt, nullptr, channel_idx, TXT_TYPE_PLAIN, timestamp, nullptr, text)) return;' '' $IB::test_a_channel_message_names_its_slot_and_no_sender
mutate inbox_enrolled_no_slots examples/companion_radio/MeconIface.cpp '  if (!_inbox.ready() && !_inbox_tried && _auth.enrolled()) {' '  if (false) {' $IB::test_an_enrolled_device_keeps_from_the_boot_on
mutate inbox_always_has_slots examples/companion_radio/MeconIface.cpp '  if (!_inbox.ready() && !_inbox_tried && _auth.enrolled()) {' '  if (!_inbox.ready() && !_inbox_tried) {' $IB::test_with_no_manager_nothing_is_kept_and_the_reception_is_counted
