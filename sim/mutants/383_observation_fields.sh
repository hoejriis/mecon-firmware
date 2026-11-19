# The observations stream's fingerprint, radio parameters and handling outcome (TELEMETRY.md):
# src/Dispatcher.cpp, src/Mesh.cpp, src/helpers/iface/Streams.cpp, the roles' radio parameters
TOPIC=383_observation_fields
OF=sim/tests/test_observation_fields.py
FPT=$OF::test_an_observation_carries_the_fingerprint_of_a_valid_packet_and_none_for_a_frame_that_is_not_one
mutate obsfp_not_in_event src/helpers/iface/Streams.cpp '  if (fp) {
    int k' '  if (false) {
    int k' $FPT
mutate obsfp_nibbles_swapped src/helpers/iface/Streams.cpp 'fps[k++] = H[fp[i] >> 4]; fps[k++] = H[fp[i] & 15];' 'fps[k++] = H[fp[i] & 15]; fps[k++] = H[fp[i] >> 4];' $FPT
mutate obsfp_hash_not_taken src/Dispatcher.cpp '{ pkt->calculatePacketHash(obs_fp); obs_has_fp = true; }' '{ memset(obs_fp, 0, sizeof(obs_fp)); obs_has_fp = true; }' $FPT
mutate obsfp_given_to_a_frame_that_is_not_a_packet src/Dispatcher.cpp '    info.fp = obs_has_fp ? obs_fp : NULL;' '    info.fp = obs_fp;' $FPT
