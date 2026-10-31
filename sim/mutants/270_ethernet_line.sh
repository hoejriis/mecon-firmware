# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the Ethernet console's line assembler (LineAssembler.h): the bound, over-long lines, a new connection
EL=sim/tests/test_ethernet_console.py
mutate eth_line_overflow_not_dropped src/helpers/LineAssembler.h '    if (len >= size - 1) { dropping = true; buf[0] = 0; return MORE; }' '    if (len >= size - 1) return MORE;' unit:test_line_assembler
mutate eth_line_drop_not_reported src/helpers/LineAssembler.h '      if (dropping) { dropping = false; buf[0] = 0; return TOO_LONG; }' '      if (dropping) { dropping = false; buf[0] = 0; return MORE; }' "$EL::test_an_over_long_line_is_refused_and_its_tail_never_runs[repeater_eth]"
mutate eth_line_reset_ignored variants/sim/SimEthernetCLI.h '    ethernet_line.reset();   // a partial line of the previous client is not the start of this one' '' "$EL::test_a_new_connection_starts_an_empty_line[repeater_eth]"
