"""Lock-step virtual time (docs/SIMULATION.md, "Lock-step virtual time").

The ether broker owns the clock: it advances only when every node has finished its quantum, and it sends every
reception due by then before the grant. So a run whose only inputs are the seeds is repeatable to the microsecond,
which is what these tests check on the channel (the broker's tx_log), not on any node counter.
"""
import os

import pytest

from sim import harness
from sim.ether import Ether

RTC = 1760000000
RUN_US = 600e6            # ten virtual minutes: two boot adverts, their repeats and the 2-minute local adverts


def _scenario(tmp, node_seeds, quantum_us=1000):
    """Two Repeaters in range of each other, booted together at virtual time 0, run for RUN_US with no input but
    their seeds and their stored prefs. Returns the broker's transmissions as (src, start_us, length, data), in
    channel order, and how many receptions were delivered.

    rb is first given a 2 % user duty cycle (on its own, before the run, with no ether): its boot advert then waits
    for two 30 s bins of the seeded hour instead of one, so the two nodes do not always transmit together and
    collide, and each hears the other: the reception path is part of what must repeat."""
    os.makedirs(tmp, exist_ok=True)
    cfg = harness.Node("repeater", "rb", tmp, seed=node_seeds[1], rtc=RTC).start()
    try:
        cli = harness.CliClient(cfg)
        assert cli.command("set dutycycle 2").startswith("OK")
        cli.close()
    finally:
        cfg.stop()
    e = Ether(os.path.join(tmp, "ether.sock"), seed=7, lockstep=True, start_after=2).start()
    made = []
    try:
        e.set_link("ra", "rb")
        for name, seed in zip(("ra", "rb"), node_seeds):
            n = harness.Node("repeater", name, tmp, ether_path=e.path, seed=seed, rtc=RTC, lockstep=True,
                             quantum_us=quantum_us)
            made.append(n.start())
        assert e.wait_until_us(RUN_US, timeout=120), "virtual time stalled at %.3f s" % (e.now_us() / 1e6)
        with e.lock:
            done = [t for t in e.tx_log if t.start_us + t.toa_us < RUN_US]
            log = sorted((t.src, t.start_us, t.length, t.data) for t in done)
            delivered = sum(len(t.delivered_to) for t in done)
        assert not e.errors, e.errors
        return log, delivered
    finally:
        for n in made:
            n.stop()
        e.stop()


def test_the_same_seeds_give_the_same_transmissions(tmp_path):
    first, rx1 = _scenario(str(tmp_path / "run1"), (1, 2))
    second, rx2 = _scenario(str(tmp_path / "run2"), (1, 2))
    other, _ = _scenario(str(tmp_path / "run3"), (3, 4))
    print("MEASURED lock-step determinism: %d transmissions and %d receptions in %d virtual s; starts (s): %s"
          % (len(first), rx1, RUN_US / 1e6, " ".join("%s@%.3f" % (t[0], t[1] / 1e6) for t in first)))
    # the scenario has something to compare: both nodes' boot and 2-minute adverts, heard by the other node
    assert {t[0] for t in first} == {"ra", "rb"} and len(first) >= 6, first
    assert rx1 >= 4, "the nodes hear nothing of each other: the reception path is not exercised"
    assert [t[:3] for t in second] == [t[:3] for t in first], "same seeds, different (src, start_us, length)"
    assert second == first and rx2 == rx1, "same seeds, same times, different packets or receptions"
    # and the comparison has power: other seeds (other identities) give other packets
    assert [t[3] for t in other] != [t[3] for t in first]


@pytest.mark.lockstep()
def test_node_clocks_and_broker_time_agree(ether, nodes):
    """A transmission is stamped with the broker time of the barrier the node started it in, so every start lies on
    the quantum grid and a node never starts before its previous transmission has ended on the channel."""
    ether.set_link("ca", "cb")
    nodes("repeater", "ca", ether_path=ether.path, seed=1, rtc=RTC).start()
    nodes("repeater", "cb", ether_path=ether.path, seed=2, rtc=RTC).start()
    assert ether.wait_until_us(ether.now_us() + 300e6, timeout=120)
    txs = ether.tx_of("ca") + ether.tx_of("cb")
    assert len(txs) >= 4, txs
    for t in txs:
        assert t.start_us % 1000 == 0, "start %r is off the 1 ms grid" % t.start_us
    for name in ("ca", "cb"):
        own = ether.tx_of(name)
        for a, b in zip(own, own[1:]):
            assert b.start_us >= a.start_us + a.toa_us, "%s started a packet before the previous one ended" % name


def test_a_real_time_node_cannot_join_a_lock_step_ether(tmp_path):
    e = Ether(str(tmp_path / "ether.sock"), lockstep=True).start()
    n = harness.Node("repeater", "rt", tmp_path, ether_path=e.path, seed=1)   # no --lockstep
    try:
        n.start()
        end = e.now_us()
        import time
        deadline = time.time() + 5
        while not e.errors and time.time() < deadline:
            time.sleep(0.02)
        assert e.errors and "real-time node" in e.errors[0], e.errors
        assert "rt" not in e._conns
        assert e.now_us() == end == 0.0
    finally:
        n.stop()
        e.stop()


def test_a_lock_step_node_cannot_join_a_real_time_ether(tmp_path):
    e = Ether(str(tmp_path / "ether.sock")).start()
    n = harness.Node("repeater", "ls", tmp_path, ether_path=e.path, seed=1, lockstep=True)
    try:
        try:
            n.start()
        except RuntimeError:
            pass                      # it may exit before publishing its port: the ether closed it
        assert n.proc.wait(10) == 3, "a refused lock-step node must exit, not run on without a clock"
        assert e.errors and "lockstep node joined a real-time ether" in e.errors[0], e.errors
    finally:
        n.stop()
        e.stop()


ADVERT_DELAY_US = 1500e3      # CommonCLI: `advert.zerohop` queues the advert 1500 ms ahead
OFFSET_US = 200e3             # db's advert is due this long after da's has started: inside da's airtime
CAD_RETRY_US = 200e3          # Dispatcher::getCADFailRetryDelay()


@pytest.mark.lockstep()
def test_a_node_defers_its_transmission_while_a_neighbour_transmits(ether, nodes):
    """Listen-before-talk on the channel: db's advert falls due while da's is on the air, db's radio reports the
    channel busy (the broker's busy notice), the Dispatcher retries every 200 ms, and the packet starts after da's
    has ended instead of overlapping it. Both are heard. Broker time is held (Ether.hold_at) while each CLI line is
    sent, so each advert is queued at a known broker time."""
    ether.set_link("da", "db")
    da = nodes("repeater", "da", ether_path=ether.path, seed=1, rtc=RTC).start()
    db = nodes("repeater", "db", ether_path=ether.path, seed=2, rtc=RTC).start()
    assert ether.wait_tx("da", timeout=120) and ether.wait_tx("db", timeout=120), "no boot adverts"
    assert ether.wait_quiet(3.0, timeout=120)
    cli_a, cli_b = harness.CliClient(da), harness.CliClient(db)
    try:
        t0 = ether.now_us() + 10e3
        assert ether.hold_at(t0)
        cli_a.sock.sendall(b"advert.zerohop\r")
        assert ether.hold_at(t0 + OFFSET_US)
        cli_b.sock.sendall(b"advert.zerohop\r")
        ether.release()
        assert ether.wait_until_us(t0 + OFFSET_US + ADVERT_DELAY_US + 3e6, timeout=120)
    finally:
        ether.release()
        cli_a.close()
        cli_b.close()
    ta = [t for t in ether.tx_of("da") if t.start_us >= t0]
    tb = [t for t in ether.tx_of("db") if t.start_us >= t0 + OFFSET_US]
    assert len(ta) == 1 and len(tb) == 1, (ta, tb)
    a, b = ta[0], tb[0]
    a_end = a.start_us + a.toa_us
    b_due = t0 + OFFSET_US + ADVERT_DELAY_US
    print("MEASURED deferral: da line sent at %.3f s, on air %.3f-%.3f s; db line sent at %.3f s, due %.3f s, started %.3f s"
          " (deferred %.0f ms)" % (t0 / 1e6, a.start_us / 1e6, a_end / 1e6, (t0 + OFFSET_US) / 1e6, b_due / 1e6,
                                   b.start_us / 1e6, (b.start_us - b_due) / 1e3))
    # the scenario has power: db's packet fell due while da's was on the air (it would have overlapped)
    assert a.start_us < b_due < a_end - 50e3, (a.start_us, b_due, a_end)
    # and db waited: its packet starts after da's ends, within one 200 ms CAD retry period of it (plus a 100 ms margin:
    # the restart was measured 45 ms after da's end at a 400 ms offset and 207 ms after it at this one), and both are
    # heard
    assert a_end <= b.start_us <= a_end + CAD_RETRY_US + 100e3, (a_end, b.start_us)
    assert a.delivered_to == ["db"] and b.delivered_to == ["da"], (a.lost, b.lost)


def _first_transmissions(tmp, seeds, run_us=75_000_000):
    """Two Repeaters with default prefs, joined to a lock-step ether at the same broker instant (a power restore).
    Returns each node's first transmission as (start_us, end_us), or None if it sent nothing in run_us."""
    os.makedirs(tmp, exist_ok=True)
    e = Ether(os.path.join(tmp, "ether.sock"), seed=7, lockstep=True, start_after=2).start()
    made = []
    try:
        e.set_link("pa", "pb")
        for name, seed in zip(("pa", "pb"), seeds):
            made.append(harness.Node("repeater", name, tmp, ether_path=e.path, seed=seed, rtc=RTC, lockstep=True,
                                     quantum_us=10000).start())
        assert e.wait_until_us(run_us, timeout=120), "virtual time stalled at %.3f s" % (e.now_us() / 1e6)
        with e.lock:
            first = {}
            for t in e.tx_log:
                first.setdefault(t.src, (t.start_us, t.start_us + t.toa_us))
        assert not e.errors, e.errors
        return first.get("pa"), first.get("pb")
    finally:
        for n in made:
            n.stop()
        e.stop()


def test_nodes_booted_together_do_not_release_on_the_same_bin_edge(tmp_path):
    # A packet held for duty is released when a 30 s bin ages out. Every node's window is seeded full at boot, so two
    # nodes restarted together (a power restore) used to release their first packets on the same edge and collide
    # every time. Each node now draws its own bin phase. Ten seed pairs (deterministic under lock-step): a few may still
    # overlap by chance (a ~1 s advert in a 30 s phase range, 2 of 10 when written); the shared grid made all ten
    # collide. The bound leaves room for a change in random draws without hiding a return of the shared grid.
    collisions = []
    for i in range(10):
        a, b = _first_transmissions(str(tmp_path / ("pair%d" % i)), (100 + 2 * i, 101 + 2 * i))
        assert a and b, "a node sent nothing: %r %r" % (a, b)
        if a[0] < b[1] and b[0] < a[1]:
            collisions.append((i, a, b))
    print("MEASURED first-packet collisions of nodes booted together: %d of 10" % len(collisions))
    assert len(collisions) <= 4, collisions
