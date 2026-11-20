# The battery level in the health stream (TELEMETRY.md): src/helpers/BatteryLevel.h
TOPIC=384_battery_level
BL=unit:test_battery_level
BS=sim/tests/test_battery_level.py
BLW=$BS::test_the_level_walks_ok_low_critical_ok_with_hysteresis
mutate battery_hysteresis_removed src/helpers/BatteryLevel.h 'HYST_MV = 100; ' 'HYST_MV = 0;   ' $BL
mutate battery_hysteresis_removed_sim src/helpers/BatteryLevel.h 'HYST_MV = 100; ' 'HYST_MV = 0;   ' $BLW
mutate battery_invalid_read_as_critical src/helpers/BatteryLevel.h '{ _sum += mv; _n++; }' '{ _sum += mv; _n++; } else { _n++; }' $BL
mutate battery_invalid_read_as_critical_sim src/helpers/BatteryLevel.h '{ _sum += mv; _n++; }' '{ _sum += mv; _n++; } else { _n++; }' $BS::test_an_invalid_reading_is_unknown_never_critical
mutate battery_mean_is_the_last_sample src/helpers/BatteryLevel.h '{ _sum += mv; _n++; }' '{ _sum = (uint16_t)(mv * (_n + 1)); _n++; }' $BL
mutate battery_mean_is_the_last_sample_sim src/helpers/BatteryLevel.h '{ _sum += mv; _n++; }' '{ _sum = (uint16_t)(mv * (_n + 1)); _n++; }' $BS::test_a_dip_shorter_than_the_mean_does_not_move_the_level
mutate battery_counter_not_incremented src/helpers/BatteryLevel.h 'if (level() != before) _changes++;' 'if (level() != before) _changes += 0;' $BL
mutate battery_counter_not_incremented_sim src/helpers/BatteryLevel.h 'if (level() != before) _changes++;' 'if (level() != before) _changes += 0;' $BLW
mutate battery_thresholds_swapped src/helpers/BatteryLevel.h 'LOW_MV = 3550;        // below this: low
    static const uint16_t CRITICAL_MV = 3400;' 'LOW_MV = 3400;        // below this: low
    static const uint16_t CRITICAL_MV = 3550;' $BL
mutate battery_thresholds_swapped_sim src/helpers/BatteryLevel.h 'LOW_MV = 3550;        // below this: low
    static const uint16_t CRITICAL_MV = 3400;' 'LOW_MV = 3400;        // below this: low
    static const uint16_t CRITICAL_MV = 3550;' $BLW
