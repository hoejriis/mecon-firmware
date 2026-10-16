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


@pytest.fixture
def ether(tmp_path):
    e = Ether(str(tmp_path / "ether.sock"), seed=7).start()
    yield e
    e.stop()


@pytest.fixture
def nodes(tmp_path):
    """Factory for nodes; every node is stopped at test end."""
    made = []

    def make(kind, name, **kw):
        n = harness.Node(kind, name, tmp_path, **kw)
        made.append(n)
        return n

    yield make
    for n in made:
        n.stop()
