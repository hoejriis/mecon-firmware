# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- a reply goes back to the interface its request came from, and that interface's trust decides the command
mutate reply_routed_to_every_interface src/helpers/MultiSerialInterface.h "if(_bound != nullptr && iface.instance != _bound){" "if(false){" sim/tests/test_companion_ble.py::test_interleaved_requests_are_answered_on_their_own_transport
mutate reply_and_trust_from_the_wrong_interface src/helpers/MultiSerialInterface.h "          _last_rx = iface.instance;" "          _last_rx = _last_rx ? _last_rx : iface.instance;" sim/tests/test_companion_ble.py::test_a_default_pin_ble_frame_is_never_decided_with_the_cables_trust
mutate reply_bound_to_the_whole_loop_pass_only src/helpers/MultiSerialInterface.h "  void bindReply(const BaseSerialInterface* src) override { _bound = src; }" "  void bindReply(const BaseSerialInterface* src) override { (void)src; }" sim/tests/test_companion_ble.py::test_interleaved_requests_are_answered_on_their_own_transport

if [ -z "$ONLY" ] && [ "$DRY" = 0 ]; then build; fi   # restore the binaries after a full run

# audit caller of a default-PIN BLE refusal (#53)
mutate audit_default_pin_ble_recorded_as_net src/helpers/cli/CliPolicy.h "v.caller = (p == P_UNTRUSTED && c.transport == T_BLE) ? Posture::CALLER_BLE : auditCaller(p);" "v.caller = auditCaller(p);" unit:test_cli_engine
mutate audit_default_pin_ble_frame_context_is_tcp examples/companion_radio/MyMesh.cpp "if (_serial && _serial->isBleTransport()) return Cli::Context(Cli::T_BLE, Cli::A_PAIRED_DEFAULT_PIN, _prefs.posture);" "" sim/tests/test_companion_ble.py
mutate audit_multi_interface_ble_not_forwarded src/helpers/MultiSerialInterface.h "return _bound != nullptr && _bound->isBleTransport();" "return false;" sim/tests/test_companion_ble.py
# duty-cycle floor for the KISS modem, sensor and secure-chat roles (#20)
mutate duty_kiss_sends_without_the_gate examples/kiss_modem/KissModem.cpp "if (!dutyAllowsSend(_tx_est_ms)) {" "if (false) {" unit:test_kiss_modem
mutate duty_kiss_unknown_channel_sends examples/kiss_modem/KissModem.cpp "if (!(_chan_freq_mhz > 0)) { _duty_refuse = true; return false; }" "if (!(_chan_freq_mhz > 0)) { return true; }" unit:test_kiss_modem
mutate duty_kiss_airtime_never_recorded examples/kiss_modem/KissModem.cpp "  _duty.record(millis(), took > _tx_est_ms ? took : _tx_est_ms);" "" unit:test_kiss_modem
mutate duty_kiss_set_radio_keeps_old_channel examples/kiss_modem/KissModem.cpp "  _chan_freq_mhz = _config.freq_hz / 1000000.0f;   // the floor follows the radio at once" "" unit:test_kiss_modem
mutate duty_kiss_floor_ignored examples/kiss_modem/KissModem.cpp "uint32_t bp = regulatory::dutyFloor(_chan_freq_mhz, _chan_bw_khz).limit_bp;" "uint32_t bp = 10000;" unit:test_kiss_modem
mutate duty_kiss_oversized_frame_held_forever examples/kiss_modem/KissModem.cpp "  if (est_ms > limit) { _duty_refuse = true; return false; }" "" unit:test_kiss_modem
mutate duty_sensor_channel_not_reported examples/simple_sensor/SensorMesh.h "return radio_freq > 0;" "return false;" sim/tests/test_duty_roles.py
mutate duty_sensor_temp_radio_not_tracked examples/simple_sensor/SensorMesh.cpp "    radio_freq = pending_freq; radio_bw = pending_bw;" "" sim/tests/test_duty_roles.py
mutate duty_secure_chat_channel_not_reported examples/simple_secure_chat/main.cpp "return _radio_freq > 0;" "return false;" sim/tests/test_duty_roles.py
mutate duty_secure_chat_channel_not_noted examples/simple_secure_chat/main.cpp "  the_mesh.noteRadioApplied(the_mesh.getFreqPref(), LORA_BW);" "" sim/tests/test_duty_roles.py
# lock-step virtual time (Issue #65): a node that reads the wall clock under --lockstep is not repeatable
mutate lockstep_node_reads_the_wall_clock variants/sim/sim_radio.cpp "  uint64_t nowUs() override { return g_ls_now - g_ls_join; }" "  uint64_t nowUs() override { return sim_real_clock()->nowUs(); }" sim/tests/test_lockstep.py::test_the_same_seeds_give_the_same_transmissions
