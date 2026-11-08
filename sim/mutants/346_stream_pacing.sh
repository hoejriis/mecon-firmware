# First snapshots take turns (#100), and the boot event still follows the runtime snapshot
TS=sim/tests/test_telemetry_streams.py
mutate first_snapshots_burst src/helpers/iface/Streams.h '  static const uint32_t FIRST_GAP_MS = 500;' '  static const uint32_t FIRST_GAP_MS = 0;' "$TS::test_first_snapshots_of_streams_started_together_take_turns[companion]"
mutate boot_before_snapshot_companion examples/companion_radio/MeconIface.cpp '  if (_boot_pending && (due & (1u << Iface::SID_RUNTIME))) {   // once per epoch, right after the first runtime snapshot' '  if (_boot_pending) {' "$TS::test_the_boot_event_comes_once_per_epoch[companion_standalone]"
