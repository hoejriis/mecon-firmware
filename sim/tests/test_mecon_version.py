"""The firmware identifies itself as mecon-firmware: version string on every role, the Companion device-info frame,
the build script that supplies it, and the protocol level staying where it is."""
import importlib.util
import os
import re
import subprocess

from sim import harness
from sim.harness import CliClient, CompanionClient

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASE = "v1.18.0-mecon."
RTC = 1760000000


def test_companion_device_info_frame_carries_the_mecon_version_and_the_unchanged_protocol_level(nodes):
    n = nodes("companion", "id1", rtc=RTC, seed=3).start()
    c = CompanionClient(n)
    f = c.device_query()
    assert f[0] == 13   # RESP_CODE_DEVICE_INFO
    assert f[1] == 14, "FIRMWARE_VER_CODE is the protocol level and must not change (got %d)" % f[1]
    ver = bytes(f[60:80]).split(b"\0")[0].decode()
    assert ver.startswith(BASE), ver
    assert len(ver) <= 19 and re.fullmatch(r"v1\.18\.0-mecon\.[0-9A-Za-z.+-]+", ver), ver
    assert len(f) == 82   # the frame did not grow: the string fits the existing 20-byte field


def test_companion_ver_and_mecon_version_over_the_cli_frame(nodes):
    n = nodes("companion", "id2", rtc=RTC, seed=3).start()
    c = CompanionClient(n)
    c.device_query()
    c.app_start()
    assert c.cli("ver").startswith(BASE)
    mv = c.cli("get mecon.version")
    assert re.fullmatch(r"> v1\.18\.0-mecon\.\S+ \(describe \S+, upstream v1\.18\.0\)", mv), mv


def test_repeater_and_room_server_ver_and_mecon_version(nodes):
    for kind in ("repeater", "room_server"):
        node = nodes(kind, kind + "1").start()
        cli = CliClient(node)
        assert re.match(r"v1\.18\.0-mecon\.\S+ \(Build: .+\)", cli.command("ver")), kind
        mv = cli.command("get mecon.version")
        assert re.fullmatch(r"> v1\.18\.0-mecon\.\S+ \(describe \S+, upstream v1\.18\.0\)", mv), (kind, mv)
        cli.close()


def _script():
    spec = importlib.util.spec_from_file_location("mecon_version", os.path.join(REPO, "bin", "mecon_version.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _git(d, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=d, check=True, capture_output=True)


class _FakeEnv:
    def __init__(self, d):
        self.d = d
        self.defs = []

    def subst(self, s):
        return self.d

    def StringifyMacro(self, s):
        return '"%s"' % s

    def Append(self, CPPDEFINES):
        self.defs += CPPDEFINES


def test_build_script_defines_the_describe_string_for_tag_dev_dirty_and_no_git(tmp_path):
    m = _script()
    d = str(tmp_path / "r")
    os.mkdir(d)
    assert m.describe(d) == "unknown"                       # not a git checkout (a source tarball)
    _git(d, "init", "-q")
    (tmp_path / "r" / "f").write_text("1")
    _git(d, "add", "f")
    _git(d, "commit", "-q", "-m", "one")
    assert m.describe(d) == "unknown"                       # git, but no v* tag yet
    _git(d, "tag", "v0.1.2")
    _git(d, "tag", "mecon-v9.9.9")                          # a tag that does not match the pattern is ignored
    assert m.describe(d) == "v0.1.2"                        # a clean tag build is exactly the tag
    (tmp_path / "r" / "f").write_text("2")
    assert m.describe(d) == "v0.1.2-dirty"
    _git(d, "commit", "-q", "-am", "two")
    got = m.describe(d)
    assert re.fullmatch(r"v0\.1\.2-1-g[0-9a-f]+", got), got  # a development build
    (tmp_path / "r" / "f").write_text("3")
    assert re.fullmatch(r"v0\.1\.2-1-g[0-9a-f]+-dirty", m.describe(d))
    env = _FakeEnv(d)
    m.apply(env)
    assert env.defs[0][0] == "MECON_VERSION" and env.defs[0][1].startswith('"v0.1.2-1-g'), env.defs
