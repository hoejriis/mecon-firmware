# gps.setloc saves only a fix (#570): src/helpers/iface/ConsoleIface.cpp
T=sim/tests/test_server_gps_sensor_ops.py
mutate setloc_without_fix src/helpers/iface/ConsoleIface.cpp '  if (strncmp(g, "on, ", 4) != 0 || !strstr(g, ", fix,")) { c.error = "no-fix"; return ST_FAILED; }' '  (void)g;' $T
mutate setloc_no_receiver_misnamed src/helpers/iface/ConsoleIface.cpp '  if (strcmp(g, "Can'"'"'t find GPS") == 0) { c.error = "no-gps"; return ST_FAILED; }' '  (void)0;' $T
