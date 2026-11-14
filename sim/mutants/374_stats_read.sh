# The stock stats-* counters are reads on a Repeater or Room server (#563): src/helpers/cli/CliRegistry.h
mutate stats_packets_write src/helpers/cli/CliRegistry.h '    {"stats-packets",          M_EXACT,   ROLE_REPEATER,                  C_READ,' '    {"stats-packets",          M_EXACT,   ROLE_REPEATER,                  C_WRITE,' unit:test_cli_engine
