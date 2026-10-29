# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #1: the runtime stream's gnss object keeps its nulls
mutate gnss_fix_never_null src/helpers/iface/Streams.h '  w.key("fix"); if (g.fix < 0) w.null(); else w.boolean(g.fix != 0);' '  w.key("fix"); w.boolean(g.fix != 0);' unit:test_streams
mutate gnss_age_never_null src/helpers/iface/Streams.h '  w.key("fix_age_s"); if (g.fix_age_s < 0) w.null(); else w.unum((uint32_t)g.fix_age_s);' '  w.key("fix_age_s"); w.unum((uint32_t)g.fix_age_s);' unit:test_streams

mutate power_detect_ignored src/helpers/iface/Streams.h '  const bool can_tell = b.isPwrMgtInitialised() || b.hasPowerDetect();' '  const bool can_tell = b.isPwrMgtInitialised();' unit:test_streams
mutate gnss_written_without_a_receiver src/helpers/iface/Streams.cpp '  if (b.getGnssHealth(g)) writeGnss(w, g);' '  b.getGnssHealth(g); writeGnss(w, g);' sim/tests/test_telemetry_streams.py::test_runtime_has_no_gnss_object_without_a_receiver
