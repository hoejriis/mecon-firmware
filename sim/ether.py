"""The simulated LoRa ether: a broker that moves raw packets between node processes.

Nodes connect over a unix socket (see variants/sim/sim_radio.cpp for the frame format).
The broker decides who hears whom (``set_link``), applies loss, reports RSSI/SNR, holds
the channel busy for the packet's time on air and delivers the packet when the
transmission ends. Overlapping receptions at one node destroy each other unless one is at
least ``capture_db`` stronger (the capture effect, below), and a node that is transmitting
hears nothing. Every node that can hear a transmission is told at once ('B', busy), which is
what its radio reports to the firmware's listen-before-talk check (``isReceiving()``).

Capture model (docs/SIMULATION.md, "Channel model"). Strength is the link SNR from ``set_link``.
When two receptions overlap at a node:
  * if neither is ``capture_db`` (default 6 dB) stronger than the other, both are lost;
  * if the earlier one is that much stronger, it survives and the later one is lost;
  * if the later one is that much stronger, it survives only if it starts while the receiver
    can still re-lock: before the last LOCK_SYMBOLS (5) symbols of the earlier packet's
    preamble. Starting later, it finds the receiver locked onto the earlier packet, which it
    then destroys: both are lost.
Sources: the 6 dB power threshold and the 5-symbol critical section at the end of the
preamble are those of Bor, Roedig, Voigt, Alonso, "Do LoRa Low-Power Wide-Area Networks
Scale?", MSWiM 2016 (their LoRaSim simulator, powerCollision/timingCollision). Gu, Jiang, Tan,
"Attack-Aware Data Timestamping in Low-Power Synchronization-Free LoRaWAN" (ICDCS 2020,
arXiv:1905.01679) measured the late-arrival case on an SX1276 with an 8-symbol preamble: a
stronger frame starting before the victim's 5th chirp is received instead, one starting after
it is not, because the receiver locks the victim's preamble from the 6th chirp.

Time inside the broker is "virtual microseconds" = wall clock * time_scale, matching the
node's --time-scale option, so airtime and node clocks stay consistent.

With ``lockstep=True`` time is a counter the broker owns instead (docs/SIMULATION.md, "Lock-step virtual
time"): every node runs with ``--lockstep``, reports after each loop() how far it wants to go, and blocks
until granted. The broker advances to the smallest request only once every connected node has reported,
and sends every reception due by then before the grant, so node clocks and ``now_us()`` agree exactly and a
run with the same seeds is repeatable. A node without ``--lockstep`` cannot join such an ether (nor the
reverse); the attempt is recorded in ``errors`` and the connection closed.
"""
import heapq
import os
import random
import selectors
import socket
import struct
import threading
import time
from dataclasses import dataclass, field


def toa_us(sf, bw_khz, cr, preamble, payload_len):
    """Semtech time-on-air in microseconds (explicit header, CRC on); cr is 5..8 (4/x)."""
    import math
    tsym_ms = (1 << sf) / bw_khz
    de = 1 if tsym_ms > 16.0 else 0
    pre_const = 6.25 if sf in (5, 6) else 4.25
    t_pre = (preamble + pre_const) * tsym_ms
    num = 8.0 * payload_len - 4.0 * sf + 28.0 + 16.0
    blocks = max(math.ceil(num / (4.0 * (sf - 2 * de))), 0)
    return (t_pre + (8.0 + blocks * cr) * tsym_ms) * 1000.0


# The capture effect (module docstring): a reception survives an overlap if it is at least this much stronger (dB),
# and a later, stronger packet takes the receiver over only before the last LOCK_SYMBOLS of the earlier's preamble.
CAPTURE_DB = 6.0
LOCK_SYMBOLS = 5


def preamble_symbols(sf):
    """The preamble the firmware programs (RadioLibWrapper::preambleLengthForSF, variants/sim/target.h)."""
    return 32 if sf <= 8 else 16


def lock_window_us(sf, bw_khz):
    """How long after a packet's start a stronger packet can still take the receiver over (us)."""
    return max(preamble_symbols(sf) - LOCK_SYMBOLS, 0) * (1 << sf) / bw_khz * 1000.0


@dataclass
class Link:
    rssi: float = -80.0
    snr: float = 8.0
    loss: float = 0.0


@dataclass
class Tx:
    """One transmission as the broker saw it (kept in Ether.tx_log for assertions)."""
    src: str
    start_us: float
    toa_us: int
    length: int
    data: bytes
    heard_by: list = field(default_factory=list)
    delivered_to: list = field(default_factory=list)
    lost: list = field(default_factory=list)   # (dst, reason)


class _Conn:
    def __init__(self, sock):
        self.sock = sock
        self.name = None
        self.buf = b""
        self.params = None            # (freq, bw, sf, cr) the node listens on
        self.tx_until_us = 0.0
        self.receptions = []          # active reception dicts
        self.lockstep = False         # joined with 'L' (lock-step clock)
        self.want = None              # lock-step: the broker time this node asked to reach (None: still running)
        self.join_us = None           # lock-step: the broker time granted at the join
        self.kill = False             # refused: close after this read


class Ether:
    def __init__(self, path, time_scale=1.0, seed=1, lockstep=False, start_after=0, capture_db=CAPTURE_DB):
        self.path = path
        self.capture_db = float(capture_db)   # capture threshold in dB of link SNR (module docstring)
        self.scale = float(time_scale)
        self.rng = random.Random(seed)
        self.links = {}               # (src, dst) -> Link
        self.tx_log = []
        self.lock = threading.RLock()
        self._conns = {}              # name -> _Conn
        self._pending = []            # heap of (due_wall, seq, reception)
        self._seq = 0
        self._stop = threading.Event()
        self._thread = None
        self._t0 = time.monotonic()
        self._srv = None
        # lock-step virtual time
        self.lockstep = bool(lockstep)
        self.start_after = int(start_after)   # time stays at 0 until this many lock-step nodes have joined
        self.errors = []                      # protocol violations (e.g. a real-time node on a lock-step ether)
        self.barriers = 0                     # number of times virtual time advanced
        self._vnow = 0                        # broker time in integer microseconds
        self._ls = []                         # connected lock-step nodes
        self._ls_joined = 0
        self.joined_us = {}                   # lock-step: node name -> broker time of its latest join (its millis() 0)
        self._hold_us = None                  # lock-step: time advances no further than this (hold_at), None: free

    # ------------------------------------------------------------------ control API
    def set_link(self, a, b, rssi=-80.0, snr=8.0, loss=0.0, both=True):
        with self.lock:
            self.links[(a, b)] = Link(rssi, snr, loss)
            if both:
                self.links[(b, a)] = Link(rssi, snr, loss)

    def clear_links(self):
        with self.lock:
            self.links.clear()

    def set_loss(self, a, b, loss, both=True):
        with self.lock:
            for k in ((a, b), (b, a)) if both else ((a, b),):
                if k in self.links:
                    self.links[k].loss = loss

    def inject(self, dst, data, rssi=-80.0, snr=8.0):
        """Deliver raw bytes to node `dst` as if they had just been received over the air (no sender, no
        airtime, no collisions). For hand-built or malformed packets."""
        with self.lock:
            c = self._conns[dst]
            self._send(c, "R", struct.pack("<ff", rssi, snr) + bytes(data))

    def now_us(self):
        if self.lockstep:
            return float(self._vnow)
        return (time.monotonic() - self._t0) * 1e6 * self.scale

    def wait_until_us(self, t_us, timeout=60.0):
        """Wait (wall clock, at most `timeout` s) until the broker's time reaches `t_us`. True if it did."""
        end = time.monotonic() + timeout
        while self.now_us() < t_us:
            if time.monotonic() > end:
                return False
            time.sleep(0.005)
        return True

    def wait_quiet(self, seconds=3.0, timeout=60.0):
        """Wait (wall clock, at most `timeout` s) until nothing has been on the air for `seconds` of broker time. For a
        test that is about to send: the previous exchange's follow-ups (a path return, a relay) are over, so the new
        packet cannot collide with them at a node that hears both senders. True if the channel went quiet."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                last = max((t.start_us + t.toa_us for t in self.tx_log), default=None)
                if last is None or self.now_us() - last >= seconds * 1e6:
                    return True
            time.sleep(0.005)
        return False

    def wait_tx(self, src, timeout=60.0):
        """Wait (wall clock) until `src` has started a transmission; True if it did. A node's first packet after boot
        waits for the first 30 s duty-cycle bin (the hour is seeded at the limit), so two nodes booted at the same
        broker time release their first packets in the same quantum and collide: a test waits for a server's boot
        advert before it sends anything that needs an answer."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.tx_of(src):
                return True
            time.sleep(0.005)
        return False

    def hold_at(self, t_us, timeout=60.0):
        """Lock-step only: let broker time advance no further than `t_us` until release(), and wait (wall clock, at most
        `timeout` s) until it is there with every node blocked on its next grant. Input sent to a node while held (a CLI
        line) is then read by its first loop() after the release, at a known broker time. True if the hold was reached."""
        assert self.lockstep, "hold_at needs a lock-step ether"
        with self.lock:
            self._hold_us = int(t_us)
            self._barrier()
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                if self._vnow >= self._hold_us and self._ls and all(c.want is not None for c in self._ls):
                    return True
            time.sleep(0.005)
        return False

    def release(self):
        """End a hold_at(): time advances again."""
        with self.lock:
            self._hold_us = None
            self._barrier()

    def wait_for_nodes(self, names, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                if all(n in self._conns and self._conns[n].params for n in names):
                    return True
            time.sleep(0.01)
        return False

    # ------------------------------------------------------------------ airtime instrument
    # The channel, not the node, is the instrument: every figure below comes from the transmissions the
    # broker saw, stamped with the broker's own (virtual) clock, never from a node's own counters.
    def tx_of(self, node):
        with self.lock:
            return sorted((t for t in self.tx_log if t.src == node), key=lambda t: t.start_us)

    def airtime_us(self, node, since_us=0.0):
        """Total transmitted airtime (us) of `node` that started at or after `since_us`."""
        return sum(t.toa_us for t in self.tx_of(node) if t.start_us >= since_us)

    def max_airtime_in_window_us(self, node, window_us=3600e6, since_us=0.0):
        """Largest airtime (us) of `node` inside any window of `window_us` (a sliding window, positioned
        anywhere in time, partial overlaps counted pro rata). Only transmissions that started at or after
        `since_us` are considered."""
        import bisect
        txs = [t for t in self.tx_of(node) if t.start_us >= since_us]
        if not txs:
            return 0.0
        starts = [t.start_us for t in txs]
        longest = max(t.toa_us for t in txs)
        cands = set(starts) | {t.start_us + t.toa_us - window_us for t in txs}
        best = 0.0
        for a in cands:
            b = a + window_us
            lo = bisect.bisect_left(starts, a - longest)
            hi = bisect.bisect_left(starts, b)
            tot = 0.0
            for t in txs[lo:hi]:
                tot += max(0.0, min(t.start_us + t.toa_us, b) - max(t.start_us, a))
            best = max(best, tot)
        return best

    def max_duty_in_any_hour(self, node, since_us=0.0):
        """Largest fraction of any sliding hour (1.0 = 100 %) that `node` spent on the air."""
        return self.max_airtime_in_window_us(node, 3600e6, since_us) / 3600e6

    # ------------------------------------------------------------------ lifecycle
    def start(self):
        if os.path.exists(self.path):
            os.unlink(self.path)
        self._srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._srv.bind(self.path)
        self._srv.listen(16)
        self._srv.setblocking(False)
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)
        for c in list(self._conns.values()):
            try:
                c.sock.close()
            except OSError:
                pass
        if self._srv:
            self._srv.close()
        if os.path.exists(self.path):
            os.unlink(self.path)

    # ------------------------------------------------------------------ main loop
    def _run(self):
        sel = selectors.DefaultSelector()
        sel.register(self._srv, selectors.EVENT_READ, None)
        socks = {}
        while not self._stop.is_set():
            timeout = 0.005
            with self.lock:
                if self.lockstep:
                    timeout = 0.05            # nothing is due on the wall clock: deliveries happen at barriers
                elif self._pending:
                    timeout = max(0.0, min(timeout, self._pending[0][0] - time.monotonic()))
            for key, _ in sel.select(timeout):
                if key.data is None:
                    try:
                        s, _ = self._srv.accept()
                    except BlockingIOError:
                        continue
                    if self.lockstep:
                        s.settimeout(5.0)   # a grant must never be dropped on a full buffer (sendall waits instead)
                    else:
                        s.setblocking(False)
                    c = _Conn(s)
                    socks[s] = c
                    sel.register(s, selectors.EVENT_READ, c)
                else:
                    c = key.data
                    try:
                        data = c.sock.recv(65536)
                    except (BlockingIOError, InterruptedError):
                        continue
                    except OSError:
                        data = b""
                    if data:
                        c.buf += data
                        self._parse(c)
                    if not data or c.kill:
                        sel.unregister(c.sock)
                        c.sock.close()
                        with self.lock:
                            if c.name and self._conns.get(c.name) is c:
                                del self._conns[c.name]
                            if c in self._ls:
                                self._ls.remove(c)
                                self._barrier()   # the nodes left may all be waiting already
                        continue
            if not self.lockstep:
                self._deliver_due(None)

    def _parse(self, c):
        while len(c.buf) >= 5 and not c.kill:
            (ln,) = struct.unpack_from("<I", c.buf, 0)
            if len(c.buf) < 4 + ln:
                return
            typ = chr(c.buf[4])
            payload = c.buf[5:4 + ln]
            c.buf = c.buf[4 + ln:]
            with self.lock:
                if typ == "L":
                    if not self.lockstep:
                        self._refuse(c, "a --lockstep node joined a real-time ether")
                        return
                    c.lockstep = True
                    c.join_us = self._vnow
                    self._ls.append(c)
                    self._ls_joined += 1
                    self._send(c, "G", struct.pack("<Q", self._vnow))
                elif typ == "A" and len(payload) >= 8:
                    (want,) = struct.unpack_from("<Q", payload, 0)
                    if not c.lockstep or want < self._vnow:
                        self._refuse(c, "bad advance request %d at %d from %s" % (want, self._vnow, c.name))
                        return
                    c.want = want
                    self._barrier()
                elif typ == "H":
                    c.name = payload.decode()
                    if self.lockstep and not c.lockstep:
                        self._refuse(c, "a real-time node (%s) joined a lock-step ether" % c.name)
                        return
                    self._conns[c.name] = c
                    if c.lockstep:
                        # 'L' and 'H' are sent together before setup(), and time cannot advance between them (this
                        # node has not asked for any), so this is the instant the node's clock reads 0
                        self.joined_us[c.name] = c.join_us
                elif typ == "P" and len(payload) >= 10:
                    f, bw = struct.unpack_from("<ff", payload, 0)
                    c.params = (round(f, 3), round(bw, 3), payload[8], payload[9])
                elif typ == "T" and len(payload) > 14:
                    self._on_tx(c, payload)

    def _refuse(self, c, why):
        self.errors.append(why)
        c.kill = True

    # ------------------------------------------------------------------ lock-step barrier
    def _barrier(self):
        """Advance virtual time once every lock-step node has reported: to the smallest requested time, after
        sending every reception due by then. Called with the lock held."""
        if not self._ls or self._ls_joined < self.start_after:
            return
        if any(c.want is None for c in self._ls):
            return
        t = min(c.want for c in self._ls)
        if self._hold_us is not None:
            t = min(t, self._hold_us)
            if t <= self._vnow:
                return                   # held (hold_at): the nodes wait for their grants until release()
        self._deliver_due(t)
        self._vnow = t
        self.barriers += 1
        grant = struct.pack("<Q", t)
        for c in self._ls:
            self._send(c, "G", grant)    # also to a node still asleep: its clock follows the broker's
            if c.want <= t:
                c.want = None

    @staticmethod
    def _send(c, typ, payload):
        try:
            c.sock.sendall(struct.pack("<IB", 1 + len(payload), ord(typ)) + payload)
        except OSError:
            pass

    # ------------------------------------------------------------------ physics
    def _on_tx(self, src, payload):
        freq, bw = struct.unpack_from("<ff", payload, 0)
        sf, cr = payload[8], payload[9]
        (toa,) = struct.unpack_from("<I", payload, 10)
        data = bytes(payload[14:])
        start = self.now_us()
        end = start + toa
        tx = Tx(src.name, start, toa, len(data), data)
        self.tx_log.append(tx)
        src.tx_until_us = end
        # a node that starts transmitting loses whatever it was receiving
        for r in src.receptions:
            if not r["corrupt"]:
                r["corrupt"] = "dst transmitting"
        for dst in self._conns.values():
            if dst is src or dst.name is None:
                continue
            link = self.links.get((src.name, dst.name))
            if link is None:
                continue
            if dst.params is None or dst.params[0] != round(freq, 3) or dst.params[1] != round(bw, 3) or dst.params[2] != sf:
                continue   # not tuned to this transmission
            tx.heard_by.append(dst.name)
            self._send(dst, "B", struct.pack("<I", int(toa)))
            r = dict(dst=dst, src=src, tx=tx, end_us=end, link=link, data=data, corrupt=None,
                     lock_us=start + lock_window_us(sf, bw))
            if dst.tx_until_us > start:
                r["corrupt"] = "dst transmitting"
            for other in dst.receptions:
                # A node's own transmissions cannot overlap on air (the radio sends one at a time); an overlap with
                # its previous one is clock jitter between the node and the broker, not a collision.
                if other["src"] is src:
                    continue
                if other["end_us"] > start:       # overlap in time at this receiver
                    lead = link.snr - other["link"].snr   # how much stronger the new packet is here
                    if lead >= self.capture_db and start < other["lock_us"]:
                        other["corrupt"] = other["corrupt"] or "captured"   # the receiver re-locks onto the new one
                    elif -lead >= self.capture_db:
                        r["corrupt"] = r["corrupt"] or "captured"           # the earlier one holds the receiver
                    else:
                        other["corrupt"] = other["corrupt"] or "collision"
                        r["corrupt"] = r["corrupt"] or "collision"
            dst.receptions.append(r)
            self._seq += 1
            if self.lockstep:
                # due in virtual time; ties broken by names, not by the wall-clock order frames happened to arrive in
                heapq.heappush(self._pending, (end, (src.name, dst.name), id(r), r))
            else:
                heapq.heappush(self._pending, (time.monotonic() + toa / 1e6 / self.scale, self._seq, id(r), r))

    def _deliver_due(self, until=None):
        """Deliver receptions due by `until` (virtual us under lock-step; None = now on the wall clock)."""
        with self.lock:
            now = time.monotonic() if until is None else until
            while self._pending and self._pending[0][0] <= now:
                _, _, _, r = heapq.heappop(self._pending)
                dst = r["dst"]
                if r in dst.receptions:
                    dst.receptions.remove(r)
                tx = r["tx"]
                reason = r["corrupt"]
                if not reason and self.rng.random() < r["link"].loss:
                    reason = "loss"
                if reason:
                    tx.lost.append((dst.name, reason))
                    continue
                tx.delivered_to.append(dst.name)
                self._send(dst, "R", struct.pack("<ff", r["link"].rssi, r["link"].snr) + r["data"])
