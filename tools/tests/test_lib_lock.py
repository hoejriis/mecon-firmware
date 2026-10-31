"""tools/lib_lock.py: the locked library set of each release environment. It records what a build resolved, and holds
every later build to exactly that set, against a fake libdeps directory (no PlatformIO here)."""
import json
import os
import subprocess
import sys

TOOL = os.path.join(os.path.dirname(__file__), "..", "lib_lock.py")


def lib(root, env, folder, name, version, owner="someone", uri=None):
    d = os.path.join(root, env, folder)
    os.makedirs(d, exist_ok=True)
    spec = {"owner": None if uri else owner, "id": 1, "name": name, "requirements": None, "uri": uri}
    json.dump({"type": "library", "name": name, "version": version, "spec": spec}, open(os.path.join(d, ".piopm"), "w"))


def run(tmp_path, *args):
    env = dict(os.environ, PLATFORMIO_LIBDEPS_DIR=str(tmp_path / "libdeps"), LIB_LOCK_FILE=str(tmp_path / "lock.json"))
    return subprocess.run([sys.executable, TOOL] + list(args), capture_output=True, text=True, env=env)


def test_a_build_that_matches_its_lock_passes(tmp_path):
    root = str(tmp_path / "libdeps")
    lib(root, "boardA", "base64", "base64", "1.4.0", owner="densaugeo")
    lib(root, "boardA", "Adafruit BusIO", "Adafruit BusIO", "1.17.4", owner="adafruit")
    lib(root, "boardA", "Local", "Local", "0.1.0", uri="file://arch/esp32/Local")
    r = run(tmp_path, "generate", "boardA")
    assert r.returncode == 0, r.stdout
    lock = json.load(open(tmp_path / "lock.json"))
    assert [l["name"] for l in lock["envs"]["boardA"]] == ["Adafruit BusIO", "base64", "Local"]
    r = run(tmp_path, "check", "boardA")
    assert r.returncode == 0 and "3 libraries, exactly as locked" in r.stdout, r.stdout


def test_any_drift_from_the_lock_fails_and_says_what(tmp_path):
    root = str(tmp_path / "libdeps")
    lib(root, "boardA", "base64", "base64", "1.4.0", owner="densaugeo")
    lib(root, "boardA", "Adafruit BusIO", "Adafruit BusIO", "1.17.4", owner="adafruit")
    assert run(tmp_path, "generate", "boardA").returncode == 0
    lib(root, "boardA", "Adafruit BusIO", "Adafruit BusIO", "1.17.5", owner="adafruit")   # the registry moved on
    lib(root, "boardA", "Extra", "Extra", "2.0.0")                                       # a new dependency appeared
    os.remove(os.path.join(root, "boardA", "base64", ".piopm"))                          # and one went away
    r = run(tmp_path, "check", "boardA")
    assert r.returncode == 1
    assert "Adafruit BusIO: locked 1.17.4, built with 1.17.5" in r.stdout
    assert "not in the lock: Extra 2.0.0" in r.stdout
    assert "locked but not installed: base64 1.4.0" in r.stdout


def test_an_environment_without_a_lock_fails_check_but_installs_nothing(tmp_path):
    lib(str(tmp_path / "libdeps"), "boardB", "base64", "base64", "1.4.0", owner="densaugeo")
    r = run(tmp_path, "check", "boardB")
    assert r.returncode == 1 and "boardB has no entry" in r.stdout, r.stdout
    r = run(tmp_path, "install", "boardB")
    assert r.returncode == 0 and "nothing to install" in r.stdout, r.stdout


def test_a_library_with_no_version_to_lock_is_refused(tmp_path):
    root = str(tmp_path / "libdeps")
    lib(root, "boardC", "fromgit", "fromgit", "0.0.0", uri="https://github.com/someone/fromgit.git")
    r = run(tmp_path, "generate", "boardC")
    assert r.returncode == 1 and "has no version to lock" in r.stdout, r.stdout
    assert not os.path.exists(tmp_path / "lock.json"), "nothing is written when a library cannot be locked"


def test_install_asks_platformio_for_exact_versions(tmp_path):
    root = str(tmp_path / "libdeps")
    lib(root, "boardA", "base64", "base64", "1.4.0", owner="densaugeo")
    lib(root, "boardA", "Local", "Local", "0.1.0", uri="file://arch/esp32/Local")
    assert run(tmp_path, "generate", "boardA").returncode == 0
    fake = tmp_path / "bin"
    fake.mkdir()
    (fake / "pio").write_text("#!/bin/sh\necho \"$@\" > \"%s\"\n" % (tmp_path / "pio-args"))
    (fake / "pio").chmod(0o755)
    env = dict(os.environ, PATH=str(fake) + os.pathsep + os.environ["PATH"],
               PLATFORMIO_LIBDEPS_DIR=str(tmp_path / "libdeps"), LIB_LOCK_FILE=str(tmp_path / "lock.json"))
    r = subprocess.run([sys.executable, TOOL, "install", "boardA"], capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stdout + r.stderr
    assert (tmp_path / "pio-args").read_text().split() == [
        "pkg", "install", "-e", "boardA", "--no-save", "-l", "densaugeo/base64@1.4.0"], "an in-repo library is not fetched"
