# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- the ether's capture effect and channel-activity notice (Issue #64; sim/ether.py, sim/tests/test_ether_physics.py)
EP=sim/tests/test_ether_physics.py
mutate capture_later_packet_never_wins sim/ether.py 'if lead >= self.capture_db and start < other["lock_us"]:' 'if False:' $EP
mutate capture_earlier_packet_never_wins sim/ether.py 'elif -lead >= self.capture_db:' 'elif False:' $EP
mutate capture_equal_strength_survives sim/ether.py 'elif -lead >= self.capture_db:' 'elif -lead >= 0:' $EP
mutate capture_just_below_threshold_survives sim/ether.py 'elif -lead >= self.capture_db:' 'elif -lead >= self.capture_db - 0.2:' $EP
mutate capture_late_packet_wins_after_the_lock sim/ether.py 'if lead >= self.capture_db and start < other["lock_us"]:' 'if lead >= self.capture_db:' $EP
mutate capture_lock_window_is_the_whole_preamble sim/ether.py 'return max(preamble_symbols(sf) - LOCK_SYMBOLS, 0) * (1 << sf) / bw_khz * 1000.0' 'return preamble_symbols(sf) * (1 << sf) / bw_khz * 1000.0' $EP
mutate busy_notice_not_sent sim/ether.py '            self._send(dst, "B", struct.pack("<I", int(toa)))' '            pass' $EP
mutate busy_notice_not_sent_lockstep sim/ether.py '            self._send(dst, "B", struct.pack("<I", int(toa)))' '            pass' sim/tests/test_lockstep.py::test_a_node_defers_its_transmission_while_a_neighbour_transmits
mutate radio_never_reports_busy variants/sim/sim_radio.cpp 'bool SimRadio::isReceiving() { loop(); return sim_clock()->nowUs() < _i->busy_until_us; }' 'bool SimRadio::isReceiving() { loop(); return false; }' sim/tests/test_lockstep.py::test_a_node_defers_its_transmission_while_a_neighbour_transmits
# nodes booted together do not share duty-window bin edges (#79)
mutate duty_phase_always_zero src/Mesh.cpp '  return _rng->nextInt(0, DUTY_BIN_MS);' '  return 0;' sim/tests/test_lockstep.py::test_nodes_booted_together_do_not_release_on_the_same_bin_edge
mutate duty_phase_not_applied src/Dispatcher.cpp '  duty.reset(_ms->getMillis() - (getDutyPhaseMs() % DUTY_BIN_MS));' '  duty.reset(_ms->getMillis());' sim/tests/test_lockstep.py::test_nodes_booted_together_do_not_release_on_the_same_bin_edge
