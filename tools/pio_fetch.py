#!/usr/bin/env python3
"""Install an env's PlatformIO packages and libraries before its build, retrying a failed download (#258).

    python3 tools/pio_fetch.py ENV [ENV ...]

A package or registry download that fails during a build looks like a broken build: a mutant "does not build", or an
env joins the failures the All boards verdict compares. Run this first, so that a build that fails afterwards failed on
the code.

Only a download error is retried. Any other failure exits 0 and leaves the verdict to the build: several board envs
fail on stock MeshCore (ci/known-upstream-failures.txt), and that must not read as an outage.

Exits 0 when the packages are in place or the failure is not a download's, 3 when a download fails on every try.
"""
import os
import re
import subprocess
import sys
import time

DOWNLOAD_ERROR = re.compile(r"HTTPClientError|ConnectionError|ConnectTimeout|ReadTimeout|Max retries exceeded|"
                            r"Temporary failure in name resolution|SSLError|Connection reset")
DELAYS_S = (30, 120)   # between the three tries
TRY_TIMEOUT_S = 900    # a download that hangs counts as a failed one


def attempt(envs):
    cmd = ["pio", "pkg", "install"]
    for e in envs:
        cmd += ["-e", e]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=TRY_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return 1, "timed out after %d s: ReadTimeout" % TRY_TIMEOUT_S
    return r.returncode, r.stdout


def main():
    envs = sys.argv[1:]
    if not envs:
        sys.exit("usage: pio_fetch.py ENV [ENV ...]")
    scale = float(os.environ.get("PIO_FETCH_DELAY_SCALE", "1"))   # the tests do not wait
    tries = len(DELAYS_S) + 1
    for n in range(1, tries + 1):
        rc, out = attempt(envs)
        if rc == 0:
            print("packages in place (try %d of %d)" % (n, tries))
            return 0
        if not DOWNLOAD_ERROR.search(out):
            print(out[-2000:])
            print("pio pkg install failed, not on a download: leaving the verdict to the build")
            return 0
        print("try %d of %d: a download failed\n%s" % (n, tries, out[-600:]))
        if n < tries:
            time.sleep(DELAYS_S[n - 1] * scale)
    print("::error::infrastructure: a PlatformIO download failed on all %d tries (%s). This is an outage, not a "
          "verdict on the code; re-run the failed jobs." % (tries, " ".join(envs)))
    return 3


if __name__ == "__main__":
    sys.exit(main())
