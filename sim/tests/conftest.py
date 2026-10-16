import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from sim import harness  # noqa: E402
from sim.ether import Ether  # noqa: E402


@pytest.fixture(scope="session", autouse=True)
def _binaries_present():
    for kind, path in harness.BIN.items():
        if not os.path.exists(path):
            pytest.fail("missing %s; build it first: pio run -e sim_%s" % (path, kind))


def _scale(request):
    """@pytest.mark.timescale(N) runs a test's broker and nodes N times faster than real time. Used by tests that
    wait on a node's first transmission after a boot: the duty-cycle window is seeded at the limit then, so the
    first packet waits for airtime to age out (up to one 30 s bin)."""
    m = request.node.get_closest_marker("timescale")
    return float(m.args[0]) if m else 1.0


def pytest_configure(config):
    config.addinivalue_line("markers", "timescale(n): run the broker and nodes n times faster than real time")


@pytest.fixture
def ether(tmp_path, request):
    e = Ether(str(tmp_path / "ether.sock"), seed=7, time_scale=_scale(request)).start()
    yield e
    e.stop()


@pytest.fixture
def nodes(tmp_path, request):
    """Factory for nodes; every node is stopped at test end."""
    made = []

    def make(kind, name, **kw):
        kw.setdefault("time_scale", _scale(request))
        n = harness.Node(kind, name, tmp_path, **kw)
        made.append(n)
        return n

    yield make
    for n in made:
        n.stop()
