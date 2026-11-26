# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the channel favourite mark (Issue #652): only a favourite channel notifies the screen and buzzer, every slot starts off,
# a replaced slot loses the mark, and the mark is saved with the channel table and read back
FAV_SIM=sim/tests/test_channel_favourite.py
mutate fav_gate_removed examples/companion_radio/MyMesh.cpp '    if (ChannelFav::notifies<ChannelDetails>(*this, channel_idx)) {' '    if (true) {' "$FAV_SIM::test_a_dm_notifies_and_a_channel_only_when_favourite"
mutate fav_default_on src/helpers/ChannelDetails.h '  uint8_t favourite = 0;' '  uint8_t favourite = 1;' unit:test_channel_favourite
mutate fav_mark_kept_on_replace examples/companion_radio/MeconIface.cpp '  ch.favourite = favourite ? 1 : 0;' '  ch.favourite = (favourite || before.favourite) ? 1 : 0;' "$FAV_SIM::test_a_replaced_or_removed_slot_loses_its_mark"
mutate fav_gate_ignores_the_mark src/helpers/ChannelFavourite.h '  return mesh.getChannel(slot, ch) && ch.favourite != 0;' '  return mesh.getChannel(slot, ch);' unit:test_channel_favourite
mutate fav_mark_not_saved examples/companion_radio/DataStore.cpp '      ChannelFav::encode(ch.favourite != 0, unused);' '      ChannelFav::encode(false, unused);' "$FAV_SIM::test_the_flag_is_set_by_channel_put_read_back_and_survives_a_reboot"
mutate fav_mark_not_loaded examples/companion_radio/DataStore.cpp '        ch.favourite = ChannelFav::decode(unused);' '        ch.favourite = 0;' "$FAV_SIM::test_the_flag_is_set_by_channel_put_read_back_and_survives_a_reboot"
mutate fav_any_nonzero_byte_is_a_mark src/helpers/ChannelFavourite.h 'return reserved[0] == MARK ? 1 : 0;' 'return reserved[0] != 0 ? 1 : 0;' unit:test_channel_favourite
mutate fav_put_ignores_the_flag src/helpers/iface/ChannelOps.cpp '  const bool written = h->set(h->user, slot, name, key, favourite);' '  const bool written = h->set(h->user, slot, name, key, false);' unit:test_channel_ops
