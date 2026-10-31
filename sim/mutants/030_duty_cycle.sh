# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- duty-cycle rules (these run the 1.25 virtual hour load test, about 2.5 minutes each)
DC_LOAD="sim/tests/test_duty_cycle.py::test_sustained_load_stays_within_the_regulatory_limit"
mutate dutycycle_window_is_a_bucket src/Dispatcher.cpp "      if (duty.sum() + est > limit_ms) {
        uint32_t wait = duty.waitMs(now, duty.sum() + est - limit_ms);" "      static unsigned long bk_t = 0; static double bk_level = 0; static unsigned long bk_prev = 0;
      double bk_rate = (double)limit_ms / DUTY_WINDOW_MS;
      bk_level += (double)(total_air_time - bk_prev); bk_prev = total_air_time;
      bk_level -= (double)(now - bk_t) * bk_rate; if (bk_level < 0) bk_level = 0; bk_t = now;
      if (bk_level + est > limit_ms) {
        uint32_t wait = (uint32_t)((bk_level + est - limit_ms) / bk_rate) + 1;" $DC_LOAD
mutate dutycycle_floor_ignored src/Dispatcher.cpp "if (floor_bp < limit) limit = floor_bp;" "if (false) limit = floor_bp;" $DC_LOAD
mutate dutycycle_floor_clamped_like_the_cli src/Dispatcher.cpp "if (floor_bp < limit) limit = floor_bp;" "if (floor_bp < limit && floor_bp >= 1000) limit = floor_bp;" $DC_LOAD
mutate dutycycle_straddle_uses_the_looser_band src/helpers/RegulatoryDutyCycle.cpp "if (ov_hi - ov_lo > EDGE_TOLERANCE_KHZ && b.limit_bp < out.limit_bp) {" "if ((double)freq_mhz * 1000.0 >= b.lo_khz && (double)freq_mhz * 1000.0 < b.hi_khz && b.limit_bp < out.limit_bp) {" $DC_LOAD
mutate dutycycle_straddle_unit src/helpers/RegulatoryDutyCycle.cpp "if (ov_hi - ov_lo > EDGE_TOLERANCE_KHZ && b.limit_bp < out.limit_bp) {" "if ((double)freq_mhz * 1000.0 >= b.lo_khz && (double)freq_mhz * 1000.0 < b.hi_khz && b.limit_bp < out.limit_bp) {" unit:test_regulatory_duty_cycle
mutate dutycycle_radio_change_not_applied examples/companion_radio/MyMesh.cpp "_radio_freq = _prefs.freq; _radio_bw = _prefs.bw;   // the duty-cycle floor follows at once" "" sim/tests/test_duty_cycle.py::test_frequency_change_applies_without_reboot
mutate dutycycle_hour_not_seeded_at_boot src/helpers/DutyWindow.h "    if (is_seeded) return;
    is_seeded = true;" "    is_seeded = true;
    return;" sim/tests/test_duty_cycle.py::test_reboots_do_not_reset_the_hour
mutate dutycycle_room_server_floor_missing examples/simple_room_server/MyMesh.h "    return radio_freq > 0;" "    return false;" sim/tests/test_duty_cycle.py::test_room_server_gets_the_floor_too
