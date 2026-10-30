# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #165: an over-long serial console line is dropped up to its end, never run in part with its tail as a command
CL_SIM=sim/tests/test_console_line.py::test_an_over_long_line_is_dropped_and_its_tail_never_runs
mutate console_long_line_runs_its_tail_repeater examples/simple_repeater/main.cpp '    command_dropping = true;' '' "$CL_SIM[repeater-160]"
mutate console_long_line_runs_its_tail_room examples/simple_room_server/main.cpp '    command_dropping = true;' '' "$CL_SIM[room_server-152]"
mutate console_long_line_never_answered_repeater examples/simple_repeater/main.cpp '      if (c == '"'"'\r'"'"') { command_dropping = false; Serial.println("\n  -> ERR: line too long"); }' '      if (c == '"'"'\r'"'"') { command_dropping = false; }' "$CL_SIM[repeater-160]"
