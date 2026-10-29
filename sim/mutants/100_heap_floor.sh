# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- the enforced heap floor (docs/CONNECTIVITY_MODE.md): undo each rule and its tests must go red
mutate heap_floor_total_free_not_largest src/helpers/HeapFloor.h "if (m.largest < _floor) {" "if (m.free < _floor) {" unit:test_heap_floor
mutate heap_floor_total_free_not_largest_sim src/helpers/HeapFloor.h "if (m.largest < _floor) {" "if (m.free < _floor) {" sim/tests/test_heap_floor.py::test_the_figure_is_the_largest_block_not_the_total_free_heap
mutate heap_floor_no_hysteresis src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (m.largest >= _floor) {" unit:test_heap_floor
mutate heap_floor_no_hysteresis_sim src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (m.largest >= _floor) {" sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_no_audit_entry src/helpers/HeapFloor.h "audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR);" "(void)ts;" unit:test_heap_floor
mutate heap_floor_no_audit_entry_sim src/helpers/HeapFloor.h "audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR);" "(void)ts;" sim/tests/test_heap_floor.py::test_one_audit_entry_per_episode_however_long_it_lasts
mutate heap_floor_audit_on_every_look src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR), m.largest >= _floor + _hyst) {" unit:test_heap_floor
mutate heap_floor_audit_on_every_look_sim src/helpers/HeapFloor.h "} else if (m.largest >= _floor + _hyst) {" "} else if (audit.add(ts, Posture::EV_LOW_MEM, Posture::CALLER_SYSTEM, Posture::W_HEAP_FLOOR), m.largest >= _floor + _hyst) {" sim/tests/test_heap_floor.py::test_one_audit_entry_per_episode_however_long_it_lasts
mutate heap_floor_first_look_a_period_late src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled() && (uint32_t)(now_ms - _last) >= _period; }" unit:test_heap_floor
mutate heap_floor_first_look_a_period_late_sim src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled() && (uint32_t)(now_ms - _last) >= _period; }" sim/tests/test_heap_floor.py::test_the_first_look_comes_right_after_setup_not_a_minute_later
mutate heap_floor_period_ignored src/helpers/HeapFloor.h "return enabled() && (!_primed || (uint32_t)(now_ms - _last) >= _period); }" "return enabled(); }" unit:test_heap_floor
mutate heap_floor_unmeasurable_fires src/helpers/HeapFloor.h "if (!enabled() || !m.known) return false;" "if (!enabled()) return false;" unit:test_heap_floor
mutate heap_floor_default_value_changed src/helpers/HeapFloor.h "#define HEAP_FLOOR_BYTES 20480 " "#define HEAP_FLOOR_BYTES 10240 " sim/tests/test_heap_floor.py::test_a_healthy_heap_reports_no_low_state_and_the_default_floor
mutate heap_floor_flag_not_reported src/helpers/cli/MeconCli.h "floor.low() ? 1 : 0," "0," unit:test_heap_floor
mutate heap_floor_flag_not_reported_sim src/helpers/cli/MeconCli.h "floor.low() ? 1 : 0," "0," sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_value_not_reported src/helpers/cli/MeconCli.h "(unsigned long)floor.floorBytes());" "0UL);" unit:test_heap_floor
mutate heap_floor_marker_dropped src/helpers/connmode/ConnStatus.h "  if (!s.heap_low) return;" "  return;" unit:test_heap_floor
mutate heap_floor_marker_dropped_sim src/helpers/connmode/ConnStatus.h "  if (!s.heap_low) return;" "  return;" sim/tests/test_heap_floor.py::test_the_marker_is_on_the_home_status_line_only_while_low
mutate heap_floor_marker_overflows_the_line src/helpers/connmode/ConnStatus.h "if (len + 1 + (sizeof(MARK) - 1) <= HOME_COLUMNS &&" "if (len + 1 + (sizeof(MARK) - 1) <= 99 &&" unit:test_heap_floor
mutate heap_floor_marker_not_filled_in examples/companion_radio/ui-new/ConnStatusScreen.h "  s.heap_low = heapFloor().low();
" "" sim/tests/test_heap_floor.py::test_the_marker_is_on_the_home_status_line_only_while_low
mutate heap_floor_not_run_from_the_loop examples/companion_radio/main.cpp "  heapFloor().loop(millis(), rtc_clock.getCurrentTime());
" "" sim/tests/test_heap_floor.py::test_an_episode_starts_below_the_floor_holds_until_the_hysteresis_and_ends
mutate heap_floor_not_started examples/companion_radio/main.cpp "heapFloor().begin(HEAP_FLOOR_BYTES);" "heapFloor().begin(0);" sim/tests/test_heap_floor.py::test_a_healthy_heap_reports_no_low_state_and_the_default_floor
