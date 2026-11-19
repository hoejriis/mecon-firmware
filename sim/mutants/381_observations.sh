# The radio stream's always-on counters, its queue reading and the observations header fields (TELEMETRY.md):
# src/Dispatcher.cpp, src/Mesh.cpp, src/helpers/iface/Streams.cpp, src/helpers/iface/RawHeader.h
# Not mutated: n_tx_fail at the two send-failure sites (a send the radio refuses, a send that times out), which the
# simulated radio never produces; the duty-limit site is the one tests can reach.
TOPIC=381_observations
OC=sim/tests/test_observation_counters.py
mutate obsctr_dup_not_counted src/Mesh.cpp '  if (seen) countRxDup();' '  (void)seen;' $OC::test_the_same_packet_heard_twice_counts_one_duplicate
mutate obsctr_dropped_not_counted src/Mesh.cpp '    countRxDropped();
    return ACTION_RELEASE;' '    return ACTION_RELEASE;' $OC::test_a_direct_packet_for_another_hop_counts_as_dropped
mutate obsctr_unparsable_not_counted src/Dispatcher.cpp '          n_rx_unparsable++;
' '' $OC::test_a_frame_that_is_not_a_packet_counts_as_unparsable_and_nothing_else
mutate obsctr_unparsable_counts_every_frame src/Dispatcher.cpp '        if (tryParsePacket(pkt, raw, len)) {' '        n_rx_unparsable++;
        if (tryParsePacket(pkt, raw, len)) {' $OC::test_a_frame_that_is_not_a_packet_counts_as_unparsable_and_nothing_else
mutate obsctr_txfail_not_counted src/Dispatcher.cpp '        n_tx_fail++;
        logTxFail(outbound, outbound->getRawLength());
        releasePacket(outbound);
        outbound = NULL;
        return;' '        logTxFail(outbound, outbound->getRawLength());
        releasePacket(outbound);
        outbound = NULL;
        return;' $OC::test_a_packet_longer_than_the_whole_duty_limit_is_a_failed_send
mutate obsctr_duty_hold_not_counted src/Dispatcher.cpp '        n_duty_holds++;
' '' $OC::test_a_send_that_waits_for_airtime_is_a_duty_hold_and_sits_in_the_queue
mutate obsctr_cad_retry_not_counted src/Dispatcher.cpp '      n_cad_retries++;
' '' $OC::test_a_send_put_off_because_the_channel_is_busy_counts_each_retry
mutate obsctr_pool_dry_on_build_not_counted src/Dispatcher.cpp '    n_queue_full++;
    _err_flags |= ERR_EVENT_FULL;' '    _err_flags |= ERR_EVENT_FULL;' $OC::test_the_pool_running_dry_is_counted_each_time
mutate obsctr_pool_dry_on_receive_not_counted src/Dispatcher.cpp '        n_queue_full++;
' '' $OC::test_the_pool_running_dry_is_counted_each_time
mutate obsctr_clear_stats_keeps_new src/Dispatcher.h '    n_rx_dup = n_rx_dropped = n_rx_unparsable = n_tx_fail = n_cad_retries = n_duty_holds = n_queue_full = 0;
    _err_flags = 0;' '    _err_flags = 0;' $OC::test_clear_stats_zeroes_the_new_counters_with_the_rest
mutate obsctr_queue_depth_wrong src/helpers/iface/Streams.cpp 'w.key("depth"); w.num(d.getOutboundDepth());' 'w.key("depth"); w.num(0);' $OC::test_a_send_that_waits_for_airtime_is_a_duty_hold_and_sits_in_the_queue
mutate obsctr_queue_free_wrong src/helpers/iface/Streams.cpp 'w.key("free"); w.num(d.getPoolFree());' 'w.key("free"); w.num(d.getPoolFree() + d.getOutboundDepth());' $OC::test_a_send_that_waits_for_airtime_is_a_duty_hold_and_sits_in_the_queue
mutate obshdr_route_table_swapped src/helpers/iface/RawHeader.h '{"tflood", "flood", "direct", "tdirect"};' '{"tflood", "direct", "flood", "tdirect"};' unit:test_raw_header
mutate obshdr_transport_offset_ignored src/helpers/iface/RawHeader.h '(route == 0 || route == 3) ? 5 : 1;' '1;' unit:test_raw_header
mutate obshdr_reserved_mode_read src/helpers/iface/RawHeader.h '(raw[at] >> 6) != 3' 'true' unit:test_raw_header
mutate obshdr_hash_size_in_hops src/helpers/iface/RawHeader.h 'h.path_len = raw[at] & 63;' 'h.path_len = raw[at];' unit:test_raw_header
mutate obshdr_type_shift src/helpers/iface/RawHeader.h 'h.type = (raw[0] >> 2) & 0x0F;' 'h.type = raw[0] & 0x0F;' unit:test_raw_header
mutate obshdr_not_in_event src/helpers/iface/Streams.cpp 'hdr.path_known) snprintf(path, sizeof(path), "%u", (unsigned)hdr.path_len)' 'hdr.path_known) snprintf(path, sizeof(path), "%u", 0u)' $OC::test_an_observation_carries_the_type_route_and_hop_count_of_the_frame_as_heard
