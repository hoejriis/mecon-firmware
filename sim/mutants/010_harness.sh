# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
mutate toa_constant variants/sim/sim_toa.h "(sf == 5 || sf == 6) ? 6.25 : 4.25" "(sf == 5 || sf == 6) ? 6.25 : 4.0" sim/tests/test_toa.py
mutate radio_drops_all variants/sim/sim_radio.cpp "if (_i->rxq.empty() || _i->tx_active) return 0;" "return 0;" sim/tests/test_mesh.py
mutate fs_write_discarded variants/sim/sim_fs.cpp "size_t w = fwrite(buf, 1, n, _p->fp);" "size_t w = n;" sim/tests/test_repeater_cli.py
mutate fault_hook_disabled variants/sim/sim_fs.cpp "if (g_sim.fault_after >= 0 && _p->fs_path == g_sim.fault_path) {" "if (false) {" sim/tests/test_atomic_storage.py::test_prefs_cut_in_temp_file_keeps_previous_prefs
