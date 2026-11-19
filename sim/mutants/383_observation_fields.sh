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
