# The remaining radio preferences as settings keys (#489)
T=sim/tests/test_radio_prefs.py
mutate radio_pref_key_missing src/helpers/iface/IfaceRadioKeys.inc '  {"multi.acks", K_INT, 0, 1, nullptr, KF_WRITABLE, 0, "get multi.acks", F_WHOLE, "set multi.acks %s"},' '' "$T"
mutate radio_pref_range_widened src/helpers/iface/IfaceRadioKeys.inc '  {"path.hash.mode", K_INT, 0, 2, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get path.hash.mode", F_WHOLE,' '  {"path.hash.mode", K_INT, 0, 3, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get path.hash.mode", F_WHOLE,' "$T"
mutate radio_pref_decimal_unbounded src/helpers/iface/IfaceConfig.cpp '                   : strcmp(format, "tx-delay") == 0 ? 2 : -1;' '                   : strcmp(format, "tx-delay") == 0 ? 20 : -1;' "$T"
mutate radio_pref_confirm_dropped src/helpers/iface/IfaceRadioKeys.inc '  {"int.thresh", K_INT, 0, 255, nullptr, KF_WRITABLE | KF_CONFIRM, 0, "get int.thresh", F_WHOLE, "set int.thresh %s"},' '  {"int.thresh", K_INT, 0, 255, nullptr, KF_WRITABLE, 0, "get int.thresh", F_WHOLE, "set int.thresh %s"},' "$T"
