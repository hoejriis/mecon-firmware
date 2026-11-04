"""tools/pio_fetch.py against a fake `pio` that fails on the tries it is told to (#258)."""
import os
import stat
import subprocess
import sys

TOOL = os.path.join(os.path.dirname(__file__), "..", "pio_fetch.py")


def fake_pio(tmp_path, failures, message="HTTPClientError"):
    """A `pio` that prints `message` and exits 1 for its first `failures` calls, and records its arguments."""
    p = tmp_path / "pio"
    p.write_text("""#!/bin/sh
n=$(cat "%(d)s/calls" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "%(d)s/calls"
echo "$@" >> "%(d)s/args"
if [ "$n" -le %(f)d ]; then echo "%(m)s"; exit 1; fi
echo installed
""" % {"d": tmp_path, "f": failures, "m": message})
    p.chmod(p.stat().st_mode | stat.S_IEXEC)
    return tmp_path


def run(tmp_path, *envs):
    env = dict(os.environ, PATH="%s:%s" % (tmp_path, os.environ["PATH"]), PIO_FETCH_DELAY_SCALE="0")
    return subprocess.run([sys.executable, TOOL] + list(envs), capture_output=True, text=True, env=env)


def calls(tmp_path):
    return int((tmp_path / "calls").read_text())


def test_a_download_failure_on_the_first_try_is_retried_and_passes(tmp_path):
    fake_pio(tmp_path, 1)
    r = run(tmp_path, "sim_repeater")
    assert r.returncode == 0 and calls(tmp_path) == 2 and "try 2 of 3" in r.stdout


def test_a_persistent_download_failure_is_reported_as_infrastructure(tmp_path):
    fake_pio(tmp_path, 99)
    r = run(tmp_path, "sim_repeater")
    assert r.returncode == 3 and calls(tmp_path) == 3
    assert "::error::infrastructure" in r.stdout and "not a verdict on the code" in r.stdout


def test_a_failure_that_is_not_a_download_leaves_the_verdict_to_the_build(tmp_path):
    # An env that fails on stock MeshCore must not read as an outage, or cost three tries.
    fake_pio(tmp_path, 99, "UnknownBoard: Unknown board ID")
    r = run(tmp_path, "some_env")
    assert r.returncode == 0 and calls(tmp_path) == 1 and "not on a download" in r.stdout


def test_every_env_is_named_to_pio(tmp_path):
    fake_pio(tmp_path, 0)
    assert run(tmp_path, "a", "b").returncode == 0
    assert (tmp_path / "args").read_text().strip() == "pkg install -e a -e b"


def test_no_env_is_a_usage_error(tmp_path):
    fake_pio(tmp_path, 0)
    assert run(tmp_path).returncode != 0
