# A byte that arrives while the interface collects a line belongs to that line (docs/contract/DEVICE_INTERFACE.md section 7)
RACE_T=sim/tests/test_deployment_authority_server.py::test_a_long_console_line_paced_as_a_uart_delivers_it_loses_no_byte
mutate console_race_repeater examples/simple_repeater/main.cpp "  const bool stock_reads = !the_iface.collecting();" "  const bool stock_reads = true;" $RACE_T[repeater]
mutate console_race_room_server examples/simple_room_server/main.cpp "  const bool stock_reads = !the_iface.collecting();" "  const bool stock_reads = true;" $RACE_T[room_server]
