"""PlatformIO extra script: pass the build's identity to the compiler as -D MECON_VERSION="<git describe>" and
-D MECON_VARIANT="<environment name>".

The describe string is `git describe --tags --match 'v[0-9]*' --dirty` (a clean tag build is exactly the tag;
a development build is v0.1.2-5-gabc1234, with -dirty when tracked files differ). Without git, or without a
tag, it is "unknown". src/helpers/MeconVersion.h turns it into the firmware version string.
"""
import subprocess


def describe(project_dir):
    try:
        out = subprocess.run(
            ["git", "-c", "safe.directory=*", "describe", "--tags", "--match", "v[0-9]*", "--dirty"],
            cwd=project_dir, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    s = out.stdout.strip()
    if out.returncode != 0 or not s:
        return "unknown"
    return s


def apply(env):
    env.Append(CPPDEFINES=[("MECON_VERSION", env.StringifyMacro(describe(env.subst("$PROJECT_DIR"))))])
    # The build environment's name: a release manifest names the variant it was built for (UPDATES.md section 4).
    env.Append(CPPDEFINES=[("MECON_VARIANT", env.StringifyMacro(env.subst("$PIOENV")))])


try:
    Import("env")  # noqa: F821 - injected by SCons
except NameError:
    pass
else:
    apply(env)  # noqa: F821
