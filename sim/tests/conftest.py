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


def _lockstep(request):
    """@pytest.mark.lockstep(quantum_us=1000, start_after=0) runs the `ether` fixture and every node from `nodes` on
    lock-step virtual time (docs/SIMULATION.md): the broker owns the clock and advances it only when every node has
    finished its quantum. For LoRa-only tests: the serial port, Wi-Fi, MQTT and BLE stay on the wall clock. Returns
    None without the marker, else (quantum_us, start_after)."""
    m = request.node.get_closest_marker("lockstep")
    if not m:
        return None
    return int(m.kwargs.get("quantum_us", 1000)), int(m.kwargs.get("start_after", 0))


def pytest_configure(config):
    config.addinivalue_line("markers", "timescale(n): run the broker and nodes n times faster than real time")
    config.addinivalue_line("markers", "lockstep(quantum_us=1000, start_after=0): lock-step virtual time, broker-driven")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if rep.when == "call":
        item.sim_call_failed = rep.failed


def _ether_log(e, last=40):
    """The broker's last transmissions, one per line: who sent what when, and who heard, lost or never got it."""
    with e.lock:
        txs = list(e.tx_log)[-last:]
    return "\n".join("  %-8s t=%.6fs toa=%dus len=%d type=%d route=%d heard=%s delivered=%s lost=%s"
                     % (t.src, t.start_us / 1e6, t.toa_us, t.length, (t.data[0] >> 2) & 0x0F, t.data[0] & 0x03,
                        t.heard_by, t.delivered_to, t.lost) for t in txs if t.data)


@pytest.fixture
def ether(tmp_path, request):
    ls = _lockstep(request)
    if ls:
        e = Ether(str(tmp_path / "ether.sock"), seed=7, lockstep=True, start_after=ls[1]).start()
    else:
        e = Ether(str(tmp_path / "ether.sock"), seed=7, time_scale=_scale(request)).start()
    yield e
    if getattr(request.node, "sim_call_failed", False):
        # the evidence for a failed radio test: what was on the air (shown under "Captured stdout teardown")
        print("ETHER LOG at broker time %.6f s (%s), last %d of %d transmissions:\n%s"
              % (e.now_us() / 1e6, "lock-step" if e.lockstep else "real time", min(40, len(e.tx_log)), len(e.tx_log),
                 _ether_log(e)))
    e.stop()
    assert not e.errors, e.errors


@pytest.fixture
def nodes(tmp_path, request):
    """Factory for nodes; every node is stopped at test end."""
    made = []

    def make(kind, name, **kw):
        kw.setdefault("time_scale", _scale(request))
        ls = _lockstep(request)
        if ls:
            kw.setdefault("lockstep", True)
            kw.setdefault("quantum_us", ls[0])
        n = harness.Node(kind, name, tmp_path, **kw)
        made.append(n)
        return n

    yield make
    for n in made:
        n.stop()
