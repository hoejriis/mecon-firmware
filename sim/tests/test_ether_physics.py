"""The broker's collision model: overlapping transmissions of two nodes collide at a receiver; one node's own
back-to-back transmissions never do, even when clock jitter makes them overlap by a few milliseconds.

The capture effect (sim/ether.py, module docstring): of two overlapping receptions the one at least the capture
threshold stronger (link SNR) survives; a later, stronger one only if it starts before the last 5 symbols of the
earlier one's preamble. And channel activity: every node that can hear a transmission is told at its start, with its
airtime, which is what its radio reports to the firmware's listen-before-talk check."""
import struct
import time

from sim.ether import CAPTURE_DB, Ether, _Conn, lock_window_us

FREQ, BW, SF, CR = 869.525, 250.0, 11, 5


def _ether(*names, lockstep=False, sent=None):
    # lockstep: the broker's clock is _vnow, set by the test, so start times are exact
    e = Ether("/unused", time_scale=1.0, lockstep=lockstep)
    # no sockets: only the physics is under test (`sent` collects what would have gone to each node)
    e._send = (lambda conn, typ, payload: None) if sent is None else (lambda conn, typ, payload: sent.append((conn.name, typ, payload)))
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


# ---------------------------------------------------------------- capture effect (lock-step clock: exact start times)
SYM_US = (1 << SF) / BW * 1000.0          # one symbol at SF11 BW250: 8.192 ms
LOCK_US = lock_window_us(SF, BW)           # 16-symbol preamble less the last 5: 11 symbols


def _capture_run(snr_a, snr_b, b_after_us, threshold=None):
    """a, then b `b_after_us` later, both 400 ms long, heard by r with the given link SNRs. Returns (tx_a, tx_b)."""
    e = _ether("a", "b", "r", lockstep=True)
    if threshold is not None:
        e.capture_db = threshold
    e.clear_links()                        # only r hears: a and b cannot hear each other
    e.set_link("a", "r", snr=snr_a, both=False)
    e.set_link("b", "r", snr=snr_b, both=False)
    t1 = _tx(e, "a", 400_000)
    e._vnow = int(b_after_us)
    t2 = _tx(e, "b", 400_000)
    e._deliver_due(10_000_000)
    return t1, t2


def test_lock_window_is_the_preamble_less_its_last_five_symbols():
    assert LOCK_US == 11 * SYM_US and CAPTURE_DB == 6.0
    assert lock_window_us(8, 125.0) == 27 * (1 << 8) / 125.0 * 1000.0   # SF <= 8: 32-symbol preamble


def test_equal_strength_collides():
    t1, t2 = _capture_run(8.0, 8.0, 0)
    assert t1.lost == [("r", "collision")] and t2.lost == [("r", "collision")]


def test_earlier_packet_at_the_threshold_captures():
    t1, t2 = _capture_run(14.0, 8.0, 50_000)
    assert t1.delivered_to == ["r"], t1.lost
    assert t2.lost == [("r", "captured")]


def test_later_packet_at_the_threshold_captures_during_the_preamble():
    t1, t2 = _capture_run(2.0, 8.0, 0)        # same instant
    assert t2.delivered_to == ["r"] and t1.lost == [("r", "captured")]
    t1, t2 = _capture_run(2.0, 8.0, LOCK_US - SYM_US)   # one symbol before the receiver locks onto a
    assert t2.delivered_to == ["r"] and t1.lost == [("r", "captured")]


def test_just_below_the_threshold_both_are_lost():
    for snr_a, snr_b in ((13.9, 8.0), (8.0, 13.9)):
        t1, t2 = _capture_run(snr_a, snr_b, 0)
        assert t1.lost == [("r", "collision")] and t2.lost == [("r", "collision")], (snr_a, snr_b)


def test_the_threshold_is_configurable():
    t1, t2 = _capture_run(18.0, 8.0, 0, threshold=10.0)
    assert t1.delivered_to == ["r"] and t2.lost == [("r", "captured")]
    t1, t2 = _capture_run(17.0, 8.0, 0, threshold=10.0)
    assert t1.lost == [("r", "collision")] and t2.lost == [("r", "collision")]


def test_later_stronger_packet_after_the_lock_destroys_both():
    """Once the receiver has locked onto a's preamble, a stronger b cannot take it over: a is destroyed by b, and b
    is never acquired."""
    t1, t2 = _capture_run(2.0, 20.0, LOCK_US)
    assert t1.lost == [("r", "collision")] and t2.lost == [("r", "collision")]
    t1, t2 = _capture_run(2.0, 20.0, 200_000)    # well into a's payload
    assert t1.lost == [("r", "collision")] and t2.lost == [("r", "collision")]


def test_no_overlap_no_capture_question():
    t1, t2 = _capture_run(2.0, 20.0, 400_000)    # b starts as a ends
    assert t1.delivered_to == ["r"] and t2.delivered_to == ["r"]


# ---------------------------------------------------------------- channel activity (what listen-before-talk sees)
def test_every_node_that_can_hear_a_transmission_is_told_it_is_busy():
    """The busy notice ('B', with the airtime) goes at the start of the transmission to every tuned node with a link,
    lossy or not (activity is detected whether or not the packet would be decoded), and to nobody else: not the
    sender, not a node out of range, not one tuned to another SF."""
    sent = []
    e = _ether("a", "n", "lossy", "far", "other_sf", lockstep=True, sent=sent)
    e.clear_links()
    for d in ("n", "lossy", "other_sf"):
        e.set_link("a", d)
    e.set_loss("a", "lossy", 1.0)
    e._conns["other_sf"].params = (round(FREQ, 3), round(BW, 3), SF - 1, CR)
    _tx(e, "a", 250_000)
    busy = sorted((n, struct.unpack("<I", p)[0]) for n, typ, p in sent if typ == "B")
    assert busy == [("lossy", 250_000), ("n", 250_000)], sent
    assert not [x for x in sent if x[1] == "R"], "nothing is delivered before the airtime is over"
    sent.clear()
    e._deliver_due(250_000)
    assert [(n, typ) for n, typ, _ in sent] == [("n", "R")], sent
