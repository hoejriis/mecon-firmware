# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# --- issue #143: BLE DFU only off the device interface, and only with a chosen PIN
mutate ble_dfu_on_interface_builds src/helpers/BleDfuPolicy.h '  return !device_interface_build && pin_chosen_by_user && pin != 0 && pin != 123456;' '  return pin_chosen_by_user && pin != 0 && pin != 123456;' unit:test_ble_dfu_policy
mutate ble_dfu_with_the_default_pin src/helpers/BleDfuPolicy.h '  return !device_interface_build && pin_chosen_by_user && pin != 0 && pin != 123456;' '  return !device_interface_build && pin_chosen_by_user && pin != 0;' unit:test_ble_dfu_policy

mutate ble_start_ota_on_nrf52_builds src/helpers/BleDfuPolicy.h '  return opted_in && !device_interface_build && !nrf52_build;' '  return opted_in && !device_interface_build;' unit:test_ble_dfu_policy
mutate ota_on_without_the_opt_in src/helpers/BleDfuPolicy.h '  return opted_in && !device_interface_build && !nrf52_build;' '  return !device_interface_build && !nrf52_build;' unit:test_ble_dfu_policy
mutate start_ota_not_destructive src/helpers/cli/CliRegistry.h '    {"start ota",              M_PREFIX,  ROLE_REPEATER,                  C_DESTRUCTIVE,' '    {"start ota",              M_EXACT,   ROLE_REPEATER,                  C_DESTRUCTIVE,' unit:test_cli_engine

mutate start_ota_class_write src/helpers/cli/CliRegistry.h '    {"start ota",              M_PREFIX,  ROLE_REPEATER,                  C_DESTRUCTIVE,' '    {"start ota",              M_PREFIX,  ROLE_REPEATER,                  C_WRITE,' unit:test_cli_engine

mutate ble_dfu_with_a_session_pin src/helpers/BleDfuPolicy.h '  return !device_interface_build && pin_chosen_by_user && pin != 0 && pin != 123456;' '  return !device_interface_build && pin != 0 && pin != 123456;' unit:test_ble_dfu_policy
