#!/bin/sh
# usage: sim/build.sh <env>   -> .sim-logs/build_<env>.log ; prints errors
cd "$(dirname "$0")/.." && sim/run.sh pio run -e "$1" > ".sim-logs/build_$1.log" 2>&1; echo "exit=$?"; grep -E "error|Error|SUCCESS|FAILED" ".sim-logs/build_$1.log" | head -${2:-25}
