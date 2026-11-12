# ble.pin.set, the tz.offset key, clock.back and contact.capacity (#494)
T=sim/tests/test_companion_settings_ops.py
F=examples/companion_radio/MeconIface.cpp
mutate pinset_cached $F '  {"ble.pin.set", opBlePinSet, PRIV_WRITE, false},' '  {"ble.pin.set", opBlePinSet, PRIV_WRITE, true},' "$T"
mutate pinset_any_class $F '  {"ble.pin.set", opBlePinSet, PRIV_WRITE, false},' '  {"ble.pin.set", opBlePinSet, PRIV_READ, false},' "$T"
mutate clockback_any_caller $F '  if (Cli::principalOf(m->callerContext()) != Cli::P_CABLE) { c.error = "cable-only"; return ST_REFUSED; }' '' "$T"
mutate capacity_unmigrated_dropped $F '  w.key("unmigrated"); w.unum(mesh->unmigratedContacts());' '' "$T"
mutate capacity_ram_wrong $F '  w.key("ram_bytes"); w.unum(st.ram_bytes);' '  w.key("ram_bytes"); w.unum(st.loads);' "$T"
mutate pinset_range_loose examples/companion_radio/MyMesh.cpp '  if (pin != 0 && (pin < 100000 || pin > 999999)) return false;' '' "$T"
mutate tz_cli_wraps examples/companion_radio/MyMesh.cpp '    const float tz = atof(&command[14]);   // checked before the cast: 250 must not wrap to -6' '    const int8_t tz = atof(&command[14]);' "$T"
mutate tz_key_range src/helpers/iface/IfaceConfig.cpp '  {"tz.offset", K_INT, -12, 14,' '  {"tz.offset", K_INT, -12, 15,' "$T"
