# ble.pin.get: the cable always, a verified broker until enrolled, a signed member after; never a phone, never cached (#446)
TS=sim/tests/test_ble_pin.py
T1="$TS::test_the_cable_reads_the_pin_before_and_after_enrolment_and_a_paired_phone_never"
T2="$TS::test_a_broker_session_reads_it_until_enrolled_then_only_signed"
mutate pin_cable_refused examples/companion_radio/MeconIface.cpp '  const bool ok = p == Cli::P_CABLE || p == Cli::P_SIGNED || (p == Cli::P_MQTT && !m->enrolled());' '  const bool ok = p == Cli::P_SIGNED || (p == Cli::P_MQTT && !m->enrolled());' "$T1"
mutate pin_broker_once_enrolled examples/companion_radio/MeconIface.cpp '  const bool ok = p == Cli::P_CABLE || p == Cli::P_SIGNED || (p == Cli::P_MQTT && !m->enrolled());' '  const bool ok = p == Cli::P_CABLE || p == Cli::P_SIGNED || p == Cli::P_MQTT;' "$T2"
mutate pin_to_a_phone examples/companion_radio/MeconIface.cpp '  const bool ok = p == Cli::P_CABLE || p == Cli::P_SIGNED || (p == Cli::P_MQTT && !m->enrolled());' '  const bool ok = p == Cli::P_CABLE || p == Cli::P_PAIRED || p == Cli::P_SIGNED || (p == Cli::P_MQTT && !m->enrolled());' "$T1"
mutate pin_cached examples/companion_radio/MeconIface.cpp '  {"ble.pin.get", opBlePin, PRIV_READ, false},' '  {"ble.pin.get", opBlePin, PRIV_READ, true},' "$T1"
