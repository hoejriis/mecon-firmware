#!/bin/sh
# Build the rebuilt Arduino IDF libraries (pioarduino hybrid compile, Issue #100) so the real firmware envs find them.
# Why a separate step: the hybrid pass runs inside the first env that needs it and matches that env's build_src_filter
# against a placeholder directory; our `../examples` entries escape it and the build fails. The env idf5_hybrid_libs
# has a plain filter. Idempotent: pio skips the rebuild when the libraries already match custom_sdkconfig.
# Usage: bin/idf5_libs.sh [env]   (CI: directly; locally: sim/run.sh bin/idf5_libs.sh)
# With an env name it does nothing unless that env carries custom_sdkconfig (the Heltec V3 and V4 envs), so a workflow
# can call it before every build, and it builds the stub with that env's library set (idf5_hybrid_libs for the V3,
# idf5_hybrid_libs_v4 for the V4). Without one it builds the V3's. Keep the V3 and V4 sets in separate core directories.
# Needs PyYAML in PlatformIO's Python (component removal); installed here if missing.
set -e
cd "$(dirname "$0")/.."
# The stub env to build: the one whose custom_sdkconfig is the given env's (the V3's or the V4's library set).
STUB=idf5_hybrid_libs
if [ -n "$1" ]; then
  STUB=$(pio project config --json-output | python3 -c "
import json, sys
cfg = {sec: dict(opts) for sec, opts in json.load(sys.stdin)}
want = cfg.get('env:' + sys.argv[1], {}).get('custom_sdkconfig')
if want is None: sys.exit(1)
for stub in ('idf5_hybrid_libs', 'idf5_hybrid_libs_v4'):
    if cfg.get('env:' + stub, {}).get('custom_sdkconfig') == want: print(stub); sys.exit(0)
sys.exit(2)" "$1") || { rc=$?; [ $rc = 1 ] && { echo "idf5_libs: $1 has no custom_sdkconfig, nothing to do"; exit 0; }; echo "idf5_libs: no stub env has $1's custom_sdkconfig"; exit 1; }
fi
PY=$(pio system info --json-output 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin)['python_exe']['value'])" 2>/dev/null || true)
[ -n "$PY" ] || PY=$(command -v python3)
"$PY" -c "import yaml" 2>/dev/null || "$PY" -m pip install --quiet PyYAML
exec pio run -e "$STUB"
