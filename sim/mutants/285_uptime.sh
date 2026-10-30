# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #165: uptime keeps counting where a 32-bit millis() wraps after 49.7 days
mutate uptime_wraps_like_millis src/helpers/Uptime.h '  total += (uint32_t)(now_ms - last);   // unsigned difference: right across a wrap' '  total = now_ms;' unit:test_uptime
mutate uptime_wraps_like_millis_iface src/helpers/Uptime.h '  total += (uint32_t)(now_ms - last);   // unsigned difference: right across a wrap' '  total = now_ms;' sim/tests/test_uptime.py::test_the_interface_uptime_keeps_counting_across_the_millis_wrap
mutate stats_uptime_from_millis src/helpers/StatsFormatHelper.h '      (unsigned)Uptime::secs(ms.getMillis()),   // not millis()/1000, which wraps after 49.7 days (#165)' '      ms.getMillis() / 1000,' sim/tests/test_uptime.py::test_the_repeater_stats_uptime_keeps_counting_across_the_millis_wrap
