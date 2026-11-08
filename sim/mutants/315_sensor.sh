# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the Sensor role (#249)
mutate sensor_dutycycle_stock_report examples/simple_sensor/SensorMesh.cpp '    formatDutyCycleReply(reply);   // the same report as the other roles, not the stock user setting alone' '    _cli.handleCommand(sender_timestamp, command, reply);' sim/tests/test_sensor.py::test_get_dutycycle_reports_the_band_and_floor_like_the_other_roles
mutate sensor_radio_channel_not_reported examples/simple_sensor/SensorMesh.h '    return radio_freq > 0;' '    return false;' sim/tests/test_sensor.py::test_get_dutycycle_reports_the_band_and_floor_like_the_other_roles
mutate sensor_default_password_opens examples/simple_sensor/SensorMesh.cpp '  const bool admin_open = !Posture::isDefaultAdminPassword(_prefs.password, ADMIN_PASSWORD);' '  const bool admin_open = true;' sim/tests/test_sensor.py::test_a_sensor_refuses_remote_admin_while_its_password_is_the_default
