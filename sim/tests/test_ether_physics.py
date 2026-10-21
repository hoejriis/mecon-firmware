"""The broker's collision model: overlapping transmissions of two nodes collide at a receiver; one node's own
back-to-back transmissions never do, even when clock jitter makes them overlap by a few milliseconds."""
import struct
import time

from sim.ether import Ether, _Conn

FREQ, BW, SF, CR = 869.525, 250.0, 11, 5


def _ether(*names):
    e = Ether("/unused", time_scale=1.0)
    e._send = lambda conn, typ, payload: None        # no sockets: only the physics is under test
    for n in names:
        c = _Conn(None)
        c.name, c.params = n, (round(FREQ, 3), round(BW, 3), SF, CR)
        e._conns[n] = c
    for a in names:
        for b in names:
            if a != b:
                e.set_link(a, b, both=False)
    return e


def _tx(e, src, toa_us, data=b"x"):
    e._on_tx(e._conns[src], struct.pack("<ffBBI", FREQ, BW, SF, CR, toa_us) + data)
    return e.tx_log[-1]


def _deliver_all(e, wait_s):
    time.sleep(wait_s)
    e._deliver_due()


def test_two_nodes_overlapping_collide():
    e = _ether("a", "b", "r")
    t1 = _tx(e, "a", 200_000)
    t2 = _tx(e, "b", 200_000)           # starts while a's packet is still in the air at r
    _deliver_all(e, 0.3)
    assert ("r", "collision") in t1.lost and ("r", "collision") in t2.lost


def test_one_node_back_to_back_does_not_collide():
    e = _ether("a", "r")
    t1 = _tx(e, "a", 200_000)
    e._conns["a"].tx_until_us = 0.0     # the node's radio finished on its own clock, a little before the broker's
    t2 = _tx(e, "a", 200_000)           # overlaps the broker's end time for t1
    _deliver_all(e, 0.5)
    assert t1.delivered_to == ["r"] and t2.delivered_to == ["r"], (t1.lost, t2.lost)
