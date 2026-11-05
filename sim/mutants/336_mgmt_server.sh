# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# The management channel on Repeater and Room server: one slot, the shared operations, and repeating left alone
MS=sim/tests/test_mgmt_channel_server.py
mutate mgmt_srv_crc_not_checked src/helpers/iface/MgmtSlot.h '                    crcOf(img, IMAGE - 4) == get32(img + IMAGE - 4);' '                    true;' $MS::test_a_damaged_slot_makes_the_binding_stale_and_nothing_moves
mutate mgmt_srv_clear_leaves_the_key src/helpers/iface/MgmtSlot.h '      s->_store->discard();
      return true;' '      return true;' $MS::test_rotate_and_unbind_with_readback
mutate mgmt_srv_revert_not_wired src/helpers/iface/ConsoleIface.cpp '    if (!mgmtRevert(s->mgmtHost(), j)) return false;' '' $MS::test_a_signed_change_must_confirm_and_reverts_unconfirmed
mutate mgmt_srv_confirm_keeps_the_snapshot src/helpers/iface/ConsoleIface.cpp '    mgmtConfirmed(static_cast<ConsoleIface*>(user)->mgmtHost());' '' $MS::test_a_confirmed_rotation_stays_and_a_reboot_while_pending_reverts
mutate mgmt_srv_public_key_wrong src/helpers/iface/ConsoleIface.cpp '  mesh::Utils::sha256(_public_fp, MGMT_FP_BYTES, MGMT_PUBLIC_KEY, sizeof(MGMT_PUBLIC_KEY));' '' $MS::test_the_public_channel_cannot_be_bound
mutate mgmt_srv_put_needs_no_guard src/helpers/iface/ConsoleIface.cpp '  return mgmtGuardChannel(c, h, opChannelPut, "channel.put");' '  return withChannels(c, opChannelPut);' $MS::test_a_put_of_the_bound_slot_must_confirm
mutate mgmt_srv_not_signed src/helpers/iface/ConsoleIface.cpp '  {"mgmt.channel.rotate", opMgmtRotateW, PRIV_WRITE, true},' '  {"mgmt.channel.rotate", opMgmtRotateW, PRIV_READ, true},' $MS::test_a_signed_change_must_confirm_and_reverts_unconfirmed
mutate mgmt_srv_stops_repeating examples/simple_repeater/MyMesh.cpp '  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded' '  return false;
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded' $MS::test_repeating_goes_on_with_a_bound_channel
