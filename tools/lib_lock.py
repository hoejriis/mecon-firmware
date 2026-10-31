#!/usr/bin/env python3
"""The exact library set of each release environment (ci/lib-lock.json), so two builds of one commit link the same code.

`lib_deps` in platformio.ini and the variant files name version ranges (`^2.0.0`, `~1.4.0`), and the libraries those
pull in name none at all, so what a build links depends on the day it fetched. The lock records the resolved set per
environment, and CI holds every build to it without editing the upstream variant files:

    lib_lock.py generate ENV...   record what each environment resolved (read from its installed libraries)
    lib_lock.py install ENV       install the locked versions first; PlatformIO keeps an installed library that
                                  satisfies the range, so the build then resolves to exactly these
    lib_lock.py check ENV         after a build: fail unless the installed set is exactly the locked one

Libraries come from the registry (owner/name@version) or from this repository (a file:// path, pinned by the commit
itself). Anything else (a git URL, an archive) has no version the lock can hold, and generate refuses it.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.environ.get("LIB_LOCK_FILE") or os.path.join(ROOT, "ci", "lib-lock.json")   # LIB_LOCK_FILE: record elsewhere


def libdeps_dir(env):
    base = os.environ.get("PLATFORMIO_LIBDEPS_DIR") or os.path.join(ROOT, ".pio", "libdeps")
    return os.path.join(base, env)


def installed(env):
    """name -> {"owner", "name", "version"} or {"name", "uri"}, from each installed library's .piopm."""
    out = {}
    d = libdeps_dir(env)
    if not os.path.isdir(d):
        return out
    for entry in sorted(os.listdir(d)):
        p = os.path.join(d, entry, ".piopm")
        if not os.path.isfile(p):
            continue
        m = json.load(open(p))
        spec = m.get("spec") or {}
        uri = spec.get("uri")
        if uri:
            out[m["name"]] = {"name": m["name"], "uri": uri}
        else:
            out[m["name"]] = {"owner": spec.get("owner"), "name": spec.get("name") or m["name"], "version": m["version"]}
    return out


def load():
    if os.path.isfile(LOCK):
        return json.load(open(LOCK))
    return {"about": "", "envs": {}}


def save(lock):
    lock["about"] = ("The exact libraries each release environment links (tools/lib_lock.py). CI installs these "
                     "before a build and fails when a build resolves anything else.")
    with open(LOCK, "w") as f:
        json.dump(lock, f, indent=1, sort_keys=True)
        f.write("\n")


def generate(envs):
    lock = load()
    bad = []
    for env in envs:
        libs = installed(env)
        if not libs:
            bad.append("%s: no installed libraries (build or `pio pkg install -e %s` first)" % (env, env))
            continue
        for l in libs.values():
            if "uri" in l and not l["uri"].startswith("file://"):
                bad.append("%s: %s comes from %s, which has no version to lock" % (env, l["name"], l["uri"]))
            elif "uri" not in l and not l.get("owner"):
                bad.append("%s: %s has no registry owner" % (env, l["name"]))
        lock["envs"][env] = sorted(libs.values(), key=lambda l: l["name"].lower())
    if bad:
        print("\n".join(bad))
        return 1
    save(lock)
    print("locked %d environment(s) in %s" % (len(envs), os.path.relpath(LOCK, ROOT)))
    return 0


def install(env):
    libs = load()["envs"].get(env)
    if libs is None:   # not locked yet: the build resolves freely, and check then says so
        print("%s is not in %s: nothing to install" % (env, os.path.relpath(LOCK, ROOT)))
        return 0
    specs = ["%s/%s@%s" % (l["owner"], l["name"], l["version"]) for l in libs if "uri" not in l]
    if not specs:
        return 0
    cmd = ["pio", "pkg", "install", "-e", env, "--no-save"]
    for s in specs:
        cmd += ["-l", s]
    return subprocess.run(cmd).returncode


def check(env):
    want = {l["name"]: l for l in load()["envs"].get(env, [])}
    have = installed(env)
    problems = []
    for name in sorted(set(want) | set(have)):
        w, h = want.get(name), have.get(name)
        if w is None:
            problems.append("not in the lock: %s %s" % (name, h.get("version") or h.get("uri")))
        elif h is None:
            problems.append("locked but not installed: %s %s" % (name, w.get("version") or w.get("uri")))
        elif w != h:
            problems.append("%s: locked %s, built with %s" % (name, w.get("version") or w.get("uri"),
                                                            h.get("version") or h.get("uri")))
    if not want:
        problems.insert(0, "%s has no entry in %s" % (env, os.path.relpath(LOCK, ROOT)))
    if problems:
        print("%s does not match its library lock:\n  %s" % (env, "\n  ".join(problems)))
        print("If the change is intended, run: python3 tools/lib_lock.py generate %s" % env)
        return 1
    print("%s: %d libraries, exactly as locked" % (env, len(want)))
    return 0


def main(argv):
    if len(argv) < 2 or argv[0] not in ("generate", "install", "check"):
        print(__doc__)
        return 2
    if argv[0] == "generate":
        return generate(argv[1:])
    rc = 0
    for env in argv[1:]:
        rc |= install(env) if argv[0] == "install" else check(env)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
