# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- the Companion's mesh path (sim/tests/test_mesh_protocol.py, #63): ACK, path learning, advert auto-add
mutate mesh_ack_not_sent_in_the_returned_path src/helpers/BaseChatMesh.cpp "PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 6);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);" "PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 6);" sim/tests/test_mesh_protocol.py::test_direct_message_is_acknowledged_flood_and_direct
mutate mesh_direct_ack_not_sent src/helpers/BaseChatMesh.cpp "    if (a2) sendDirect(a2, dest.out_path, dest.out_path_len, d);" "" sim/tests/test_mesh_protocol.py::test_direct_message_is_acknowledged_flood_and_direct
mutate mesh_path_not_stored src/helpers/BaseChatMesh.cpp "  from.out_path_len = mesh::Packet::copyPath(from.out_path, out_path, out_path_len);  // store a copy of path, for sendDirect()" "  from.out_path_len = OUT_PATH_UNKNOWN;" sim/tests/test_mesh_protocol.py::test_path_is_learned_from_the_first_flood_and_the_next_message_goes_direct
mutate mesh_autoadd_flag_ignored_always_add examples/companion_radio/MyMesh.cpp "  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;" "  if (true) {
    return true;" sim/tests/test_mesh_protocol.py::test_heard_advert_is_only_reported_when_auto_add_is_off
mutate mesh_autoadd_flag_ignored_never_add examples/companion_radio/MyMesh.cpp "  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;" "  if (false) {
    return true;" sim/tests/test_mesh_protocol.py::test_heard_advert_is_added_as_a_contact_when_auto_add_is_on
# lock-step (Issue #65 phase 4): a node whose clock picks up a few ms of the wall clock still runs and transmits, so the
# precondition holds and only the identical-tx_log comparison can catch it (its timers fire at wall-dependent quanta)
mutate lockstep_node_clock_jitters_with_the_wall_clock variants/sim/sim_radio.cpp "  uint64_t nowUs() override { return g_ls_now - g_ls_join; }" "  uint64_t nowUs() override { return g_ls_now - g_ls_join + sim_real_clock()->nowUs() % 5000; }" sim/tests/test_lockstep.py::test_the_same_seeds_give_the_same_transmissions
# a contact save looks up no file that is not there (#76)
mutate save_preclean_lookups_back src/helpers/AtomicFile.h '#ifndef ATOMIC_FILE_TWO_STEP_SWAP
  if (fs->exists(tmp)) fs->remove(tmp);' '#if 1
  if (fs->exists(tmp)) fs->remove(tmp);' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate save_reads_absent_bucket src/helpers/ContactStore.cpp '  bool have = !knownAbsent(b) && _be->readBucket(' '  bool have = _be->readBucket(' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate save_new_bucket_hint_dropped src/helpers/ContactStore.cpp '_scratch, BUCKET_BYTES, knownAbsent(b))) return false;' '_scratch, BUCKET_BYTES, false)) return false;' $CT_SIM::test_a_contact_save_looks_up_no_file_that_is_not_there
mutate bucket_map_not_set_on_write src/helpers/ContactStore.cpp '  if (_bpresent && b < _bpresent_bits) _bpresent[b >> 3] |= (uint8_t)(1u << (b & 7));' '' $CT_SIM
