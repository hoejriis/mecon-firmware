#!/bin/sh
# usage: sim/build.sh [env...]  (default: every sim env)  -> .sim-logs/build.log ; prints errors
# All envs go through one pio invocation: PlatformIO removes the build dirs of envs it was not asked for.
cd "$(dirname "$0")/.." || exit 1
ENVS=${*:-"sim_repeater sim_room_server sim_companion sim_companion_wifi sim_companion_wifi_usb sim_repeater_asan sim_companion_asan"}
ARGS=""; for e in $ENVS; do ARGS="$ARGS -e $e"; done
mkdir -p .sim-logs
sim/run.sh pio run $ARGS > .sim-logs/build.log 2>&1; echo "exit=$?"
grep -E "error|Error|SUCCESS|FAILED" .sim-logs/build.log | head -25
