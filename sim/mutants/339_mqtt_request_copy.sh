# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# A job event published during a broker request must not overwrite the request being handled
mutate mqtt_request_read_from_the_client_buffer src/helpers/mqtt/MqttController.cpp '_iface_fn(_iface_user, sessionContext(), req, len, out, IFACE_PACKET_BUFFER - 64)' '_iface_fn(_iface_user, sessionContext(), payload, len, out, IFACE_PACKET_BUFFER - 64)' sim/tests/test_device_interface.py::test_a_job_event_raised_inside_a_broker_request_leaves_the_request_whole
