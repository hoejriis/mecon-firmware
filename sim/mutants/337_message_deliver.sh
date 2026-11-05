# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# message.deliver: marked, not on the air, once however it comes, refused rather than dropped, and signed once enrolled
MD=sim/tests/test_message_deliver.py
mutate deliver_unmarked examples/companion_radio/MyMesh.cpp '  memcpy(&out_frame[i], MARK, mark);
  i += mark;' '' $MD::test_a_supplied_message_reaches_the_stock_app_marked_and_nothing_goes_on_the_air
mutate deliver_source_always_radio examples/companion_radio/MyMesh.cpp '    e->from_manager = true;' '' $MD::test_a_supplied_message_reaches_the_stock_app_marked_and_nothing_goes_on_the_air
mutate deliver_source_trusts_the_text src/helpers/iface/InboxOps.cpp '  w.key("source"); w.str(e.from_manager ? "manager" : "radio");' '  w.key("source"); w.str(e.from_manager || strstr(e.text, "[via manager]") ? "manager" : "radio");' $MD::test_a_real_reception_says_radio_even_with_the_marker_in_its_text
mutate deliver_creates_no_contact_check examples/companion_radio/MyMesh.cpp '    if (!c) return Iface::DELIVER_NOT_FOUND;' '    (void)c;' $MD::test_what_cannot_be_delivered_is_an_answer
mutate deliver_text_is_cut examples/companion_radio/MyMesh.cpp '  if ((size_t)i + mark + len > MAX_FRAME_SIZE) return Iface::DELIVER_TOO_LONG;   // refused rather than cut' '  if ((size_t)i + mark + len > MAX_FRAME_SIZE) len = MAX_FRAME_SIZE - i - mark;' $MD::test_what_cannot_be_delivered_is_an_answer
mutate deliver_no_duplicate_check examples/companion_radio/MyMesh.cpp '  if (_iface.inbox().wasHeard(id)) return Iface::DELIVER_DUPLICATE;' '' $MD::test_one_message_reaches_the_app_once_however_it_comes
mutate deliver_not_remembered examples/companion_radio/MyMesh.cpp '  _iface.inbox().remember(id, true);
  if (Iface::InboxEntry* e' '  if (Iface::InboxEntry* e' $MD::test_one_message_reaches_the_app_once_however_it_comes
mutate deliver_radio_not_remembered examples/companion_radio/MyMesh.cpp '  _iface.inbox().remember(id, again);
' '' $MD::test_one_message_reaches_the_app_once_however_it_comes
mutate deliver_rf_copy_queued_again examples/companion_radio/MyMesh.cpp '  if (again) return false;
' '' $MD::test_a_real_copy_arriving_after_a_delivery_is_not_queued_again
mutate deliver_full_queue_drops examples/companion_radio/MyMesh.cpp '  if (!room) return Iface::DELIVER_QUEUE_FULL;   // the native queue would drop it without a word' '' $MD::test_a_full_queue_answers_busy_rather_than_dropping
mutate deliver_needs_no_signature examples/companion_radio/MeconIface.cpp '  {"message.deliver", opMsgDeliver, PRIV_WRITE, true},' '  {"message.deliver", opMsgDeliver, PRIV_READ, true},' $MD::test_an_enrolled_device_delivers_only_for_a_signature
mutate deliver_not_kept_by_id examples/companion_radio/MeconIface.cpp '  {"message.deliver", opMsgDeliver, PRIV_WRITE, true},' '  {"message.deliver", opMsgDeliver, PRIV_WRITE, false},' $MD::test_one_message_reaches_the_app_once_however_it_comes
mutate deliver_identity_ignores_the_time src/helpers/iface/InboxOps.cpp '  add(reinterpret_cast<const uint8_t*>(&ts), sizeof(ts));
' '' $MD::test_one_message_reaches_the_app_once_however_it_comes
