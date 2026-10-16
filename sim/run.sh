#!/bin/sh
# Run a command inside the dev container with the repo mounted. Usage: sim/run.sh <cmd...>
# Long jobs: run detached and redirect output to .sim-logs/<name>.log, then poll the file.
# On a CI runner the toolchain is installed directly: SIM_NO_DOCKER=1 runs the command as is.
if [ "${SIM_NO_DOCKER:-0}" = "1" ]; then exec "$@"; fi
exec docker run --rm -v "$(pwd)":/workspaces/mecon-firmware -v mecon-firmware-platformio:/home/dev/.platformio mecon-firmware-dev "$@"
