#!/bin/sh
# Build the rebuilt Arduino IDF libraries (pioarduino hybrid compile, Issue #100) so the real firmware envs find them.
# Why a separate step: the hybrid pass runs inside the first env that needs it and matches that env's build_src_filter
# against a placeholder directory; our `../examples` entries escape it and the build fails. The env idf5_hybrid_libs
# has a plain filter. Idempotent: pio skips the rebuild when the libraries already match custom_sdkconfig.
# Usage: bin/idf5_libs.sh [env]   (CI: directly; locally: sim/run.sh bin/idf5_libs.sh)
# With an env name it does nothing unless that env carries custom_sdkconfig (the ESP32-S3 Heltec envs), so a workflow
# can call it before every build.
# Needs PyYAML in PlatformIO's Python (component removal); installed here if missing.
set -e
cd "$(dirname "$0")/.."
if [ -n "$1" ]; then
  pio project config --json-output | python3 -c "
import json, sys
for sec, opts in json.load(sys.stdin):
    if sec == 'env:' + sys.argv[1]:
        sys.exit(0 if any(k == 'custom_sdkconfig' for k, v in opts) else 1)
sys.exit(1)" "$1" || { echo "idf5_libs: $1 has no custom_sdkconfig, nothing to do"; exit 0; }
fi
PY=$(pio system info --json-output 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin)['python_exe']['value'])" 2>/dev/null || true)
[ -n "$PY" ] || PY=$(command -v python3)
"$PY" -c "import yaml" 2>/dev/null || "$PY" -m pip install --quiet PyYAML
exec pio run -e idf5_hybrid_libs
