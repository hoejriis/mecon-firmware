# identity.key.export: clear only with no record (or a corrupt one), wrapped for the signer once enrolled (#446)
TS=sim/tests/test_key_export.py
T1="$TS::test_clear_with_no_record_wrapped_for_the_signing_member_once_enrolled[companion]"
T2="$TS::test_a_paired_phone_may_export_on_a_standalone_companion_only"
T3="$TS::test_a_broker_never_gets_it_unsigned"
T4="$TS::test_a_corrupt_record_leaves_the_cable_and_hardened_leaves_nothing"
mutate kx_clear_once_enrolled src/helpers/iface/KeyExportOps.cpp '    wrapped = h.caller == Cli::P_SIGNED && h.member_key && h.member_id;' '    clear = h.caller == Cli::P_CABLE;' "$T1"
mutate kx_phone_once_enrolled src/helpers/iface/KeyExportOps.cpp '    clear = h.caller == Cli::P_CABLE || (h.phone_allowed && h.caller == Cli::P_PAIRED);' '    clear = h.caller == Cli::P_CABLE || h.caller == Cli::P_PAIRED || h.caller == Cli::P_MQTT;' "$T3"
mutate kx_corrupt_shut src/helpers/iface/KeyExportOps.cpp '    clear = h.caller == Cli::P_CABLE;' '    clear = false;' "$T4"
mutate kx_hardened_open src/helpers/iface/KeyExportOps.cpp '  if (h.hardened) { c.error = "hardened"; return ST_REFUSED; }' '' "$T4"
mutate kx_label src/helpers/iface/KeyWrap.cpp 'const char LABEL[] = "mecon key-export v1";' 'const char LABEL[] = "mecon key-export v2";' "$T1"
mutate kx_counter_stuck src/helpers/iface/KeyWrap.cpp '    for (int i = 15; i >= 0 && ++block[i] == 0; i--) {}   // the next counter value' '' "$T1"
mutate kx_tag_short src/helpers/iface/KeyWrap.cpp '  sha.update(out, n);' '  sha.update(out, n - 1);' "$T1"
