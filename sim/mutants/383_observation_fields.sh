# The observations stream's fingerprint, radio parameters and handling outcome (TELEMETRY.md):
# src/Dispatcher.cpp, src/Mesh.cpp, src/helpers/iface/Streams.cpp, the roles' radio parameters
TOPIC=383_observation_fields
OF=sim/tests/test_observation_fields.py
FPT=$OF::test_an_observation_carries_the_fingerprint_of_a_valid_packet_and_none_for_a_frame_that_is_not_one
mutate obsfp_not_in_event src/helpers/iface/Streams.cpp '  if (fp) {
    k =' '  if (false) {
    k =' $FPT
mutate obsfp_nibbles_swapped src/helpers/iface/Streams.cpp 'fps[k++] = H[fp[i] >> 4]; fps[k++] = H[fp[i] & 15];' 'fps[k++] = H[fp[i] & 15]; fps[k++] = H[fp[i] >> 4];' $FPT
mutate obsfp_hash_not_taken src/Dispatcher.cpp '{ pkt->calculatePacketHash(obs_fp); obs_has_fp = true; }' '{ memset(obs_fp, 0, sizeof(obs_fp)); obs_has_fp = true; }' $FPT
mutate obsfp_given_to_a_frame_that_is_not_a_packet src/Dispatcher.cpp '    info.fp = obs_has_fp ? obs_fp : NULL;' '    info.fp = obs_fp;' $FPT
RPT=$OF::test_an_observation_carries_the_radio_parameters_the_radio_is_set_to
RTT=$OF::test_the_radio_parameters_follow_a_temporary_radio_change_and_not_only_the_stored_settings
mutate obsradio_freq_not_in_event src/helpers/iface/Streams.cpp '  if (info.freq_known) k +=' '  if (false) k +=' $RPT
mutate obsradio_modulation_not_in_event src/helpers/iface/Streams.cpp '  if (info.mod_known) snprintf' '  if (false) snprintf' $RPT
mutate obsradio_sf_and_cr_swapped src/Dispatcher.cpp 'getRadioModulation(info.sf, info.cr);' 'getRadioModulation(info.cr, info.sf);' $RTT
mutate obsradio_repeater_boot_not_tracked examples/simple_repeater/MyMesh.cpp 'radio_sf = _prefs.sf; radio_cr = _prefs.cr;
  radio_driver.setTxPower' 'radio_driver.setTxPower' $RPT
mutate obsradio_repeater_temp_not_tracked examples/simple_repeater/MyMesh.cpp ' radio_sf = pending_sf; radio_cr = pending_cr;' '' $RTT
mutate obsradio_companion_boot_not_tracked examples/companion_radio/MyMesh.cpp '_radio_sf = _prefs.sf; _radio_cr = _prefs.cr;
  radio_driver.setTxPower' 'radio_driver.setTxPower' $RPT
# The handling outcome. Not mutated: the filterRecvFloodPacket site in Mesh.cpp, which no role's filter reaches (the
# Companion's returns false and the others have none), so no test can drive it.
O1=$OF::test_a_repeater_reports_what_became_of_each_packet
O2=$OF::test_a_flood_packet_whose_path_is_full_has_no_route_on
O3=$OF::test_a_repeater_that_does_not_repeat_says_the_packet_was_filtered
O4=$OF::test_a_companion_does_not_forward_and_says_so
O5=$OF::test_a_flood_packet_held_back_by_the_receive_delay_is_reported_as_heard_with_no_outcome
mutate obsout_forwarded_not_tagged src/Dispatcher.cpp '    rx_outcome = Iface::OUTCOME_FORWARDED;
' '' $O1
mutate obsout_duplicate_not_tagged src/Dispatcher.h 'n_rx_dup++; noteRxOutcome(Iface::OUTCOME_DUPLICATE);' 'n_rx_dup++;' $O1
mutate obsout_not_next_hop_not_tagged src/Dispatcher.h 'n_rx_dropped++; noteRxOutcome(Iface::OUTCOME_NOT_NEXT_HOP);' 'n_rx_dropped++;' $O1
mutate obsout_unparsable_not_tagged src/Dispatcher.cpp '          obs_outcome = Iface::OUTCOME_UNPARSABLE;
' '' $O1
mutate obsout_stale_from_the_packet_before src/Dispatcher.cpp '  rx_outcome = Iface::OUTCOME_UNKNOWN;
  DispatcherAction action' '  DispatcherAction action' $O1
mutate obsout_no_route_reported_as_filtered src/Mesh.cpp '!room ? Iface::OUTCOME_NO_ROUTE : Iface::OUTCOME_FILTERED' '!room ? Iface::OUTCOME_FILTERED : Iface::OUTCOME_FILTERED' $O2
mutate obsout_flood_not_forwarded_untagged src/Mesh.cpp '  if (packet->isRouteFlood()) {   // why it goes no further' '  if (false) {   // why it goes no further' $O3
mutate obsout_direct_in_turn_not_filtered src/Mesh.cpp '    if (self_id.isHashMatch(pkt->path, pkt->getPathHashSize())) noteRxOutcome(Iface::OUTCOME_FILTERED);   // its turn, but forwarding is not allowed here
' '' $O3
mutate obsout_held_back_packet_takes_the_last_outcome src/Dispatcher.cpp '  uint8_t obs_outcome = Iface::OUTCOME_UNKNOWN;' '  uint8_t obs_outcome = rx_outcome;' $O5
mutate obsout_not_in_event src/helpers/iface/Streams.cpp 'info.outcome < sizeof(OUTCOMES) / sizeof(OUTCOMES[0]) ? info.outcome : 0' '0' $O1
mutate obsout_names_swapped src/helpers/iface/Streams.cpp '"\"forwarded\"", "\"duplicate\""' '"\"duplicate\"", "\"forwarded\""' $O4
