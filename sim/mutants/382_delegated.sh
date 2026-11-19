# Delegated receive identities (AUTHORITY section 10b; Issue #552): src/helpers/iface/DelegateSeal.cpp, DeployAuth.cpp,
# src/helpers/deployment/DepDelegates.cpp, src/Mesh.cpp
T=sim/tests/test_delegates.py
SEAL=src/helpers/iface/DelegateSeal.cpp
F=src/helpers/iface/DeployAuth.cpp
# the seal: every part of its context, its label and its tag
mutate dl_label $SEAL 'const char LABEL[] = "mecon delegate-seal v1";' 'const char LABEL[] = "mecon key-export v1";' unit:test_delegate_seal
mutate dl_job_unbound $SEAL '  msg[o++] = (uint8_t)(job_id >> 24); msg[o++] = (uint8_t)(job_id >> 16); msg[o++] = (uint8_t)(job_id >> 8); msg[o++] = (uint8_t)job_id;' '  o += 4;' unit:test_delegate_seal
mutate dl_slot_unbound $SEAL '  msg[o++] = slot;' '  msg[o++] = 0;' unit:test_delegate_seal
mutate dl_eph_unbound $SEAL '  memcpy(msg + o, eph, EPH); o += EPH;' '  memset(msg + o, 0, EPH); o += EPH;' unit:test_delegate_seal
mutate dl_pub_unbound $SEAL '  memcpy(msg + o, pub, 32); o += 32;' '  memset(msg + o, 0, 32); o += 32;' unit:test_delegate_seal
mutate dl_tag_ignored $SEAL '  const bool ok = Dep::constTimeEqual(tag, ct_tag + KEY, TAG);' '  const bool ok = true;' unit:test_delegate_seal
mutate dl_tag_short $SEAL '  const bool ok = Dep::constTimeEqual(tag, ct_tag + KEY, TAG);' '  const bool ok = Dep::constTimeEqual(tag, ct_tag + KEY, TAG - 1);' unit:test_delegate_seal
mutate dl_tag_over_less $SEAL 'void tagOf(const uint8_t k[32], const uint8_t* ct, uint8_t out[32]) { Dep::hmacSha256(k + 16, 16, ct, KEY, out); }' 'void tagOf(const uint8_t k[32], const uint8_t* ct, uint8_t out[32]) { Dep::hmacSha256(k + 16, 16, ct, KEY - 1, out); }' unit:test_delegate_seal
mutate dl_counter_stuck $SEAL '    for (int i = 15; i >= 0 && ++block[i] == 0; i--) {}   // the next counter value' '' $T::test_the_published_vector_is_what_the_firmware_opens
# the table at rest
mutate dlg_tag_ignored src/helpers/deployment/DepDelegates.cpp '  const bool ok = constTimeEqual(tag, in + DLG_HDR + m, DLG_TAG);' '  const bool ok = true;' unit:test_dep_delegates
mutate dlg_length_unbound src/helpers/deployment/DepDelegates.cpp '  if (m == 0 || m > DLG_PLAIN_MAX || m % DLG_REC != 0 || len != DLG_HDR + m + DLG_TAG || m > cap) return false;' '  if (m == 0 || m > DLG_PLAIN_MAX || m % DLG_REC != 0 || m > cap) return false;' unit:test_dep_delegates
mutate dlg_stored_in_clear $F '    ok = len && _env.dlg_blob->save(sealed, len);' '    ok = len && _env.dlg_blob->save(plain, o);' $T::test_a_member_provisions_an_identity_whose_key_is_sealed_in_flight_and_at_rest
# who may put, and when
mutate dl_not_enrolled $F '  if (ls == Dep::LS_ABSENT) { *err = "not_enrolled"; return false; }' '' $T::test_a_member_provisions_an_identity_whose_key_is_sealed_in_flight_and_at_rest
mutate dl_hardened_put $F '  if (Posture::isHardened(ctx.posture)) { *err = "hardened"; return false; }   // Hardened holds none' '' $T::test_hardened_and_reset_destroy_every_key_and_rotation_keeps_them
mutate dl_put_not_sensitive src/helpers/deployment/DepWrapper.cpp '  if (strcmp(op, "identity.delegate.put") == 0) return OPC_SENSITIVE;   // the sealed key of a delegated identity (AUTHORITY 10b)' '' $T::test_a_member_provisions_an_identity_whose_key_is_sealed_in_flight_and_at_rest
mutate dl_job_not_checked $F '  Iface::Dl::kdf(shared, _answering.job, (uint8_t)slot, sealed, pub, k);' '  Iface::Dl::kdf(shared, 0, (uint8_t)slot, sealed, pub, k);' $T::test_the_published_vector_is_what_the_firmware_opens
mutate dl_seal_unchecked $F '  if (!Iface::Dl::open(k, sealed + Iface::Dl::EPH, prv)) { c.error = "seal_invalid"; goto done; }' '  Iface::Dl::open(k, sealed + Iface::Dl::EPH, prv);' $T::test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted
mutate dl_bad_key_stored $F '  if (!mesh::LocalIdentity::validatePrivateKey(prv)) { c.error = "bad_key"; goto done; }' '' $T::test_the_published_vector_is_what_the_firmware_opens
mutate dl_key_mismatch $F '  if (memcmp(li->pub_key, pub, PUB_KEY_SIZE) != 0) { c.error = "key_mismatch"; goto done; }' '' $T::test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted
mutate dl_reserved_hash $F '  if (pub[0] == 0x00 || pub[0] == 0xFF) { c.error = "reserved_hash"; return ST_REFUSED; }' '' $T::test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted
mutate dl_same_as_node $F '  if (memcmp(pub, _env.node_id->pub_key, PUB_KEY_SIZE) == 0) { c.error = "same_as_node"; return ST_REFUSED; }' '' $T::test_the_published_vector_is_what_the_firmware_opens
mutate dl_duplicate_key $F '    if (i != (int)slot && _dlg[i] && memcmp(_dlg[i]->pub_key, pub, PUB_KEY_SIZE) == 0) { c.error = "duplicate"; return ST_REFUSED; }' '    ;' $T::test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted
mutate dl_slot_range $F '!jsonGetInt(d, ts, 0, DELEGATES_MAX - 1, &slot) ||' '!jsonGetInt(d, ts, 0, DELEGATES_MAX, &slot) ||' $T::test_the_sealed_key_must_open_for_this_device_job_slot_and_key_and_nothing_else_is_accepted
# where they go: Hardened, reset, factory reset (equivalent below)
mutate dl_hardened_keeps_them $F '  if (hardened && _dlg_key_present) delegatesDestroy();' '  (void)hardened;' $T::test_hardened_and_reset_destroy_every_key_and_rotation_keeps_them
mutate dl_hardened_never_reconciled examples/companion_radio/MeconIface.cpp '  _auth.delegateReconcile(Posture::isHardened(_mesh->postureValue()));     // and no delegated identity (Issue #552)' '' $T::test_hardened_and_reset_destroy_every_key_and_rotation_keeps_them
mutate dl_reset_keeps_them $F '  if (ok) delegatesDestroy();    // and so do the delegated identities' '  (void)0;' $T::test_hardened_and_reset_destroy_every_key_and_rotation_keeps_them
mutate dl_delete_keeps_the_file $F '    if (!delegatesSave()) { _dlg[slot] = old; _dlg_ch[slot] = old_ch; c.error = "storage"; return ST_BUSY; }' '' $T::test_a_member_provisions_an_identity_whose_key_is_sealed_in_flight_and_at_rest
# receiving
M=src/Mesh.cpp
mutate dl_receive_hook_off $M '        if (!found && pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {' '        if (false) {' $T::test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it
mutate dl_own_key_not_first $M '        if (!found && pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {' '        if (!found && !self_id.isHashMatch(&dest_hash) && pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {' $T::test_two_identities_that_share_a_hash_are_tried_in_order
mutate dl_ack_names_the_node $M '        _reply_id = &id;' '        _reply_id = NULL;' $T::test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it
mutate dl_any_kind_of_message $M '      if (len > 5 && (data[4] >> 2) == 0 /* TXT_TYPE_PLAIN */) {' '      if (len > 5) {' $T::test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it
mutate dl_inbox_without_identity src/helpers/iface/InboxOps.cpp '  if (e.has_identity) { w.key("identity"); hexOf(e.identity, sizeof(e.identity), hex); w.str(hex); }' '' $T::test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it
mutate dl_primary_tagged examples/companion_radio/MyMesh.cpp '  if (const mesh::LocalIdentity* di = _rx_delegate >= 0 ? delegatedIdentity(_rx_delegate) : nullptr) {   // Issue #552' '  if (const mesh::LocalIdentity* di = delegatedIdentity(0)) {   // Issue #552' $T::test_a_message_to_a_delegated_identity_is_received_acked_and_listed_with_it
mutate dl_removed_still_received $F '    if (_dlg[i] && n-- == 0) return _dlg[i];' '    if (n-- == 0) return _dlg[i];' $T::test_only_a_known_sender_to_a_present_identity_is_received
# dl_factory_keeps_them is equivalent in the simulator: the factory reset's format removes both files before delegatesDestroy runs,
# and the reboot that follows reloads from them (the continuity package's cont_reset mutant has the same note).
# A mutant on `di->isHashMatch(&dest_hash)` in Mesh.cpp is equivalent: a message whose destination hash does not match fails
# the MAC of every delegated identity, so the hash test only saves key agreements (a cost, not a check).
# the optional local channel (Issue #602)
MM=examples/companion_radio/MyMesh.cpp
T2=sim/tests/test_delegates.py
mutate dlc_delivery_off $MM '  if (lc >= 0 && getChannel(lc, lch) && lch.name[0]) {' '  if (false) {' $T2::test_a_message_to_an_identity_with_a_local_channel_shows_on_that_slot_and_lists_with_the_identity
mutate dlc_slot_not_bound $F '          _dlg_ch[slot] = (int8_t)((plain[o] >> 2) - 1);' '' $T2::test_a_local_channel_is_set_listed_stored_sealed_and_cleared_by_a_put_without_one
mutate dlc_slot_unsaved $F '      plain[o] = (uint8_t)(i | (_dlg_ch[i] + 1) << 2);' '      plain[o] = (uint8_t)i;' $T2::test_a_local_channel_is_set_listed_stored_sealed_and_cleared_by_a_put_without_one
mutate dlc_put_keeps_old_channel $F '    _dlg_ch[slot] = (int8_t)chan;' '    if (chan >= 0) _dlg_ch[slot] = (int8_t)chan;' $T2::test_a_local_channel_is_set_listed_stored_sealed_and_cleared_by_a_put_without_one
mutate dlc_name_dropped $MM '    snprintf(line, sizeof(line), "%s: %s", from.name, text);' '    snprintf(line, sizeof(line), "%s", text);' $T2::test_a_message_to_an_identity_with_a_local_channel_shows_on_that_slot_and_lists_with_the_identity
mutate dlc_inbox_dropped $MM '    if (!keepReceived(pkt, from.id.pub_key, -1, TXT_TYPE_PLAIN, sender_timestamp, nullptr, text)) return;
    char line' '    char line' $T2::test_a_message_to_an_identity_with_a_local_channel_shows_on_that_slot_and_lists_with_the_identity
mutate dlc_text_send_allowed $MM '    } else if (localOnly(channel_idx)) {
      writeErrFrame(ERR_CODE_NOT_PERMITTED);   // a delegated identity'"'"'s local channel is never transmitted
    } else {' '    } else {' $T2::test_the_local_channel_is_never_transmitted_on_and_a_send_on_it_is_refused
mutate dlc_data_send_allowed $MM '    } else if (localOnly(channel_idx)) {
      writeErrFrame(ERR_CODE_NOT_PERMITTED);   // a delegated identity'"'"'s local channel is never transmitted
    } else if (data_type == DATA_TYPE_RESERVED) {' '    } else if (data_type == DATA_TYPE_RESERVED) {' $T2::test_the_local_channel_is_never_transmitted_on_and_a_send_on_it_is_refused
mutate dlc_message_send_allowed $MM '  if (localOnly(slot)) return Iface::MS_LOCAL_ONLY;' '' $T2::test_the_local_channel_is_never_transmitted_on_and_a_send_on_it_is_refused
# dlc_lora_send_allowed ($MM loraChannelSend) and dlc_fmc_send_allowed (fmcSend): the same one-line guard on a path the simulator
# does not drive with a delegated slot; covered by review, not by a test.
# Resetting _dlg_ch[] on a delete or a destroy is equivalent: delegatedChannel() and localChannel() read a slot only while its identity
# is held, and a put sets the channel afresh.
