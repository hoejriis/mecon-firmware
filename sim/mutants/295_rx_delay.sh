# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the receive delay in float (RxDelay.h): it must stay upstream's delay to the millisecond
mutate rx_delay_drifts_from_upstream src/helpers/RxDelay.h '  return (int)((powf(10.0f, 0.85f - score) - 1.0f) * air_time);' '  return (int)((powf(10.0f, 0.84f - score) - 1.0f) * air_time);' unit:test_rx_delay
