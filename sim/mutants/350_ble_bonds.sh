# ble.bonds and device.reset bonds: counted, cleared from the cable only, at the next BLE start (#446)
TS=sim/tests/test_device_reset.py
T1="$TS::test_bonds_are_counted_and_cleared_from_the_cable_only_at_the_next_ble_start"
T2="$TS::test_a_bond_clear_waits_for_a_ble_start"
mutate bonds_clear_over_ble src/helpers/iface/ResetOps.cpp '  if (s == RS_BONDS && h.caller != Posture::CALLER_CABLE) { c.error = "usb-only"; return ST_REFUSED; }' '' "$T1"
mutate bonds_clear_never_runs examples/companion_radio/main.cpp '    if (the_mesh.bondsClearPending()) the_mesh.bondsCleared(bluetooth_interface.clearBonds() && bluetooth_interface.bondCount() == 0);' '' "$T1"
mutate bonds_clear_not_kept examples/companion_radio/MyMesh.cpp '      return _store->setBondsClearPending(true);' '      return true;' "$T2"
mutate bonds_count_while_off examples/companion_radio/MeconIface.cpp '  w.key("count"); if (n >= 0) w.unum((uint32_t)n); else w.null();' '  w.key("count"); w.unum(n >= 0 ? (uint32_t)n : 0);' "$T2"
