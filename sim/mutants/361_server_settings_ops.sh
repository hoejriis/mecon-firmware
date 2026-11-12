# The Repeater and Room server flood limits, board.get, board.adc.set and power.saving.set (#493)
T=sim/tests/test_server_settings_ops.py
F=src/helpers/iface/ConsoleIface.cpp
mutate srv_board_get_write_class $F '  {"board.get", opBoardGet, PRIV_READ, false},' '  {"board.get", opBoardGet, PRIV_WRITE, false},' "$T"
mutate srv_power_saving_read_class $F '  {"power.saving.set", opPowerSavingSet, PRIV_WRITE, true},' '  {"power.saving.set", opPowerSavingSet, PRIV_READ, true},' "$T"
mutate srv_power_saving_inverted $F '  if (!h.run(h.user, on ? "powersaving on" : "powersaving off", reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }' '  if (!h.run(h.user, on ? "powersaving off" : "powersaving on", reply, sizeof(reply))) { c.error = "refused"; return ST_FAILED; }' "$T"
mutate srv_powersaving_read_dropped $F '  w.key("powersaving"); if (ps_read) w.boolean(strcmp(r, "on") == 0); else w.null();' '  w.key("powersaving"); w.null();' "$T"
mutate srv_adc_unsupported_ok $F '  if (strncmp(reply, "OK", 2) != 0) { c.error = strstr(reply, "unsupported") ? "unsupported" : "refused"; return ST_FAILED; }' '' "$T"
mutate srv_boot_reason_dropped $F '  strOrNull(w, "boot_reason", cliValue(c, "get pwrmgt.bootreason", r, sizeof(r)));' '  strOrNull(w, "boot_reason", nullptr);' "$T"
mutate srv_multiplier_places_loose src/helpers/iface/IfaceConfig.cpp '    return decimal(p, &p, 0, 10) && *p == 0 && (!dot || strlen(dot + 1) <= 3);' '    return decimal(p, &p, 0, 10) && *p == 0;' "$T"
mutate srv_flood_unscoped_range src/helpers/iface/IfaceConfig.cpp '  {"flood.max.unscoped", K_INT, 0, 64,' '  {"flood.max.unscoped", K_INT, 0, 65,' "$T"
mutate srv_flood_advert_line src/helpers/iface/IfaceConfig.cpp '"set flood.max.advert %s"},' '"set flood.max %s"},' "$T"
