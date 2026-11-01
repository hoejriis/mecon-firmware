"""PlatformIO extra script: pass the build's identity to the compiler as -D MECON_VERSION="<git describe>" and
-D MECON_VARIANT="<environment name>", and pin the build's clock to the commit's.

The describe string is `git describe --tags --match 'v[0-9]*' --dirty` (a clean tag build is exactly the tag;
a development build is v0.1.2-5-gabc1234, with -dirty when tracked files differ). Without git, or without a
tag, it is "unknown". src/helpers/MeconVersion.h turns it into the firmware version string.

SOURCE_DATE_EPOCH is set to the commit's own time (unless the caller set it), so __DATE__ and __TIME__ (the Arduino
core prints them in its build info) are the same in every build of one commit, and two builds of it are
byte-identical (Issue #220). Without git it is left unset.
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


def commit_time(project_dir):
    try:
        out = subprocess.run(["git", "-c", "safe.directory=*", "show", "-s", "--format=%ct", "HEAD"],
                             cwd=project_dir, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    s = out.stdout.strip()
    return s if out.returncode == 0 and s.isdigit() else None


def apply(env):
    when = commit_time(env.subst("$PROJECT_DIR"))
    if when and "SOURCE_DATE_EPOCH" not in env["ENV"]:
        env["ENV"]["SOURCE_DATE_EPOCH"] = when   # GCC takes __DATE__ and __TIME__ from it
    env.Append(CPPDEFINES=[("MECON_VERSION", env.StringifyMacro(describe(env.subst("$PROJECT_DIR"))))])
    # The build environment's name: a release manifest names the variant it was built for (UPDATES.md section 4).
    env.Append(CPPDEFINES=[("MECON_VARIANT", env.StringifyMacro(env.subst("$PIOENV")))])


try:
    Import("env")  # noqa: F821 - injected by SCons
except NameError:
    pass
else:
    apply(env)  # noqa: F821
