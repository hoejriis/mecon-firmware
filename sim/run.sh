#!/bin/sh
# Run a command inside the dev container with the repo mounted. Usage: sim/run.sh <cmd...>
# Long jobs: run detached and redirect output to .sim-logs/<name>.log, then poll the file.
exec docker run --rm -v "$(pwd)":/workspaces/mecon-firmware -v mecon-firmware-platformio:/home/dev/.platformio mecon-firmware-dev "$@"
