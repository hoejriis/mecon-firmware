# sensor.list/get/set and gps.* on the Repeater and Room server (#493)
T=sim/tests/test_server_gps_sensor_ops.py
F=src/helpers/iface/ConsoleIface.cpp
mutate srv_sensor_list_next_lost $F '    if (strncmp(p, "... next:", 9) == 0) next = strtol(p + 9, nullptr, 10);' '    if (strncmp(p, "... next:", 9) == 0) next = -1;' "$T"
mutate srv_sensor_list_cursor_ignored $F '  snprintf(line, sizeof(line), "sensor list %u", (unsigned)cursor);' '  snprintf(line, sizeof(line), "sensor list");' "$T"
mutate srv_sensor_get_null_ok $F '  if (strncmp(reply, "> ", 2) != 0) { c.error = "unknown-setting"; return ST_FAILED; }' '' "$T"
mutate srv_sensor_set_spaces $F '  if (strchr(value, '"' '"')) { c.error = "bad-value"; return ST_INVALID; }' '' "$T"
mutate srv_sensor_set_read_class $F '  {"sensor.set", opSensorSet, PRIV_WRITE, true},' '  {"sensor.set", opSensorSet, PRIV_READ, true},' "$T"
mutate srv_gps_caps_always $F '  for (const char* gcap : GPS_CAPABILITIES) add(gcap);
#endif' '#endif
  for (const char* gcap : {"gps.get", "gps.set", "gps.sync", "gps.setloc", "gps.advert.set"}) add(gcap);' "$T"
mutate srv_gps_fix_wrong $F '  w.key("fix"); if (powered) w.boolean(strstr(r, ", fix,") != nullptr); else w.null();' '  w.key("fix"); if (powered) w.boolean(strstr(r, "fix,") != nullptr); else w.null();' "$T"
mutate srv_gps_diag_any_reply $F '  const bool diag = receiver && h.run(h.user, "gps diag", r, sizeof(r)) && strncmp(r, "req:", 4) == 0;' '  const bool diag = receiver && h.run(h.user, "gps diag", r, sizeof(r));' "$T"
mutate srv_gps_advert_any_reply $F '  const bool policy = h.run(h.user, "gps advert", r, sizeof(r)) && strncmp(r, "> ", 2) == 0;' '  const bool policy = h.run(h.user, "gps advert", r, sizeof(r));' "$T"
mutate srv_gps_set_inverted $F '  const Status st = gpsLine(c, on ? "gps on" : "gps off");' '  const Status st = gpsLine(c, on ? "gps off" : "gps on");' "$T"
mutate srv_gps_sync_ok_anyway $F '  if (strcmp(reply, "ok") != 0) { c.error = "no-gps"; return ST_FAILED; }' '' "$T"
mutate srv_gps_policy_open $F '      (strcmp(p, "none") != 0 && strcmp(p, "share") != 0 && strcmp(p, "prefs") != 0))' '      false)' "$T"
mutate srv_gps_setloc_read_class $F '  {"gps.setloc", opGpsSetloc, PRIV_WRITE, true},' '  {"gps.setloc", opGpsSetloc, PRIV_READ, true},' "$T"
