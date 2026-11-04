#!/bin/sh
# usage: sim/build.sh [env...]  (default: every sim env)  -> .sim-logs/build.log ; prints errors
#        sim/build.sh --envs   prints the default env list
# All envs go through one pio invocation: PlatformIO removes the build dirs of envs it was not asked for.
cd "$(dirname "$0")/.." || exit 1
ENVS=${*:-"sim_repeater sim_sensor sim_room_server sim_repeater_eth sim_room_server_eth sim_companion sim_companion_stockcontacts sim_companion_wifi sim_companion_wifi_usb sim_companion_standalone sim_companion_standalone_stockcontacts sim_repeater_asan sim_companion_asan sim_companion_wifi_usb_asan"}
DEFAULT_ENVS="sim_repeater sim_room_server sim_repeater_eth sim_room_server_eth sim_companion sim_companion_stockcontacts sim_companion_wifi sim_companion_wifi_usb sim_companion_standalone sim_companion_standalone_stockcontacts sim_repeater_asan sim_companion_asan sim_companion_wifi_usb_asan"
if [ "${1:-}" = "--envs" ]; then echo "$DEFAULT_ENVS"; exit 0; fi   # for tools/pio_fetch.py
ENVS=${*:-$DEFAULT_ENVS}
ARGS=""; for e in $ENVS; do ARGS="$ARGS -e $e"; done
mkdir -p .sim-logs
sim/run.sh pio run $ARGS > .sim-logs/build.log 2>&1; echo "exit=$?"
grep -E "error|Error|SUCCESS|FAILED" .sim-logs/build.log | head -25
