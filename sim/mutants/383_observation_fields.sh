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
#endif
  DispatcherAction action' '#endif
  DispatcherAction action' $O1
mutate obsout_no_route_reported_as_filtered src/Mesh.cpp '!room ? Iface::OUTCOME_NO_ROUTE : Iface::OUTCOME_FILTERED' '!room ? Iface::OUTCOME_FILTERED : Iface::OUTCOME_FILTERED' $O2
mutate obsout_flood_not_forwarded_untagged src/Mesh.cpp '  if (packet->isRouteFlood()) {   // why it goes no further' '  if (false) {   // why it goes no further' $O3
mutate obsout_direct_in_turn_not_filtered src/Mesh.cpp '    if (self_id.isHashMatch(pkt->path, pkt->getPathHashSize())) noteRxOutcome(Iface::OUTCOME_FILTERED);   // its turn, but forwarding is not allowed here
' '' $O3
mutate obsout_held_back_packet_takes_the_last_outcome src/Dispatcher.cpp '  uint8_t obs_outcome = Iface::OUTCOME_UNKNOWN;' '  uint8_t obs_outcome = rx_outcome;' $O5
mutate obsout_not_in_event src/helpers/iface/Streams.cpp 'info.outcome < sizeof(OUTCOMES) / sizeof(OUTCOMES[0]) ? info.outcome : 0' '0' $O1
mutate obsout_names_swapped src/helpers/iface/Streams.cpp '"\"forwarded\"", "\"duplicate\""' '"\"duplicate\"", "\"forwarded\""' $O4
# The counters field of the compact report (TELEMETRY.md): CompactReport.h, Streams.cpp, StandingOps.cpp, tools/mecon_report.py
RC=sim/tests/test_report_counters.py
CR=unit:test_compact_report
mutate rptctr_not_encoded src/helpers/iface/CompactReport.h '  if (fields & RF_COUNTERS) {
    p = put16' '  if (false) {
    p = put16' $CR
mutate rptctr_first_two_swapped src/helpers/iface/CompactReport.h 'p = put16(p, d.rx_dup); p = put16(p, d.rx_dropped);' 'p = put16(p, d.rx_dropped); p = put16(p, d.rx_dup);' $CR
mutate rptctr_length_ignores_the_field src/helpers/iface/CompactReport.h '((fields & RF_COUNTERS) ? REPORT_COUNTERS_BYTES : 0);' '0;' $CR
mutate rptctr_bit_not_allowed src/helpers/iface/CompactReport.h 'const uint8_t RF_ALL = RF_HEALTH | RF_RADIO | RF_LOCATION | RF_COUNTERS;' 'const uint8_t RF_ALL = RF_HEALTH | RF_RADIO | RF_LOCATION;' $CR
mutate rptctr_older_fields_moved src/helpers/iface/CompactReport.h '*p++ = d.err_flags; *p++ = (uint8_t)d.noise_dbm;' '*p++ = (uint8_t)d.noise_dbm; *p++ = d.err_flags;' $CR
mutate rptctr_duplicates_filled_from_drops src/helpers/iface/Streams.cpp 'd.rx_dup = (uint16_t)disp.getNumRxDup();' 'd.rx_dup = (uint16_t)disp.getNumRxDropped();' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_queue_figures_swapped src/helpers/iface/Streams.cpp 'd.queue_depth = (uint8_t)(depth < 0 ? 0 : depth > 255 ? 255 : depth);' 'd.queue_depth = (uint8_t)(free < 0 ? 0 : free > 255 ? 255 : free);' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_not_filled src/helpers/iface/ConsoleIface.cpp '  fillReportCounters(d, *s->_host.dispatcher);
' '' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_not_produced_by_the_console src/helpers/iface/ConsoleIface.cpp '  return RF_HEALTH | RF_RADIO | RF_COUNTERS | (loc ? RF_LOCATION : 0);' '  return RF_HEALTH | RF_RADIO | (loc ? RF_LOCATION : 0);' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_not_produced_by_the_companion examples/companion_radio/MeconIface.cpp '  return RF_HEALTH | RF_RADIO | RF_COUNTERS | (readLocation(sensors.getLocationProvider(), f) ? RF_LOCATION : 0);' '  return RF_HEALTH | RF_RADIO | (readLocation(sensors.getLocationProvider(), f) ? RF_LOCATION : 0);' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_companion_not_filled examples/companion_radio/MeconIface.cpp '  fillReportCounters(d, *m->_mesh);
' '' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_name_unknown src/helpers/iface/StandingOps.cpp '{"health", "radio", "location", "counters"};' '{"health", "radio", "location", "counter"};' $RC::test_the_counters_can_be_asked_for_by_a_standing_instruction
mutate rptctr_name_unknown_native src/helpers/iface/StandingOps.cpp 'd.tok[arr].size > 4)' 'd.tok[arr].size > 3)' unit:test_standing
mutate rptctr_decoder_skips_a_byte_too_few tools/mecon_report.py '        p += 16' '        p += 15' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
mutate rptctr_relay_cuts_the_field examples/companion_radio/MeconIface.cpp '  const size_t len = REPORT_HEADER + reportFieldsLen(fields) + REPORT_SIG;' '  const size_t len = REPORT_HEADER + ((fields & RF_HEALTH) ? 8 : 0) + ((fields & RF_RADIO) ? 12 : 0) + ((fields & RF_LOCATION) ? 14 : 0) + REPORT_SIG;' $RC::test_a_report_with_counters_carries_what_the_radio_stream_counts
