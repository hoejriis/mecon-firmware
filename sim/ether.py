"""The simulated LoRa ether: a broker that moves raw packets between node processes.

Nodes connect over a unix socket (see variants/sim/sim_radio.cpp for the frame format).
The broker decides who hears whom (``set_link``), applies loss, reports RSSI/SNR, holds
the channel busy for the packet's time on air and delivers the packet when the
transmission ends. Overlapping receptions at one node destroy each other, and a node
that is transmitting hears nothing.

Time inside the broker is "virtual microseconds" = wall clock * time_scale, matching the
node's --time-scale option, so airtime and node clocks stay consistent.
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


class Ether:
    def __init__(self, path, time_scale=1.0, seed=1):
        self.path = path
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

    def now_us(self):
        return (time.monotonic() - self._t0) * 1e6 * self.scale

    def wait_for_nodes(self, names, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                if all(n in self._conns and self._conns[n].params for n in names):
                    return True
            time.sleep(0.01)
        return False

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
                if self._pending:
                    timeout = max(0.0, min(timeout, self._pending[0][0] - time.monotonic()))
            for key, _ in sel.select(timeout):
                if key.data is None:
                    try:
                        s, _ = self._srv.accept()
                    except BlockingIOError:
                        continue
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
                    if not data:
                        sel.unregister(c.sock)
                        c.sock.close()
                        with self.lock:
                            if c.name and self._conns.get(c.name) is c:
                                del self._conns[c.name]
                        continue
                    c.buf += data
                    self._parse(c)
            self._deliver_due()

    def _parse(self, c):
        while len(c.buf) >= 5:
            (ln,) = struct.unpack_from("<I", c.buf, 0)
            if len(c.buf) < 4 + ln:
                return
            typ = chr(c.buf[4])
            payload = c.buf[5:4 + ln]
            c.buf = c.buf[4 + ln:]
            with self.lock:
                if typ == "H":
                    c.name = payload.decode()
                    self._conns[c.name] = c
                elif typ == "P" and len(payload) >= 10:
                    f, bw = struct.unpack_from("<ff", payload, 0)
                    c.params = (round(f, 3), round(bw, 3), payload[8], payload[9])
                elif typ == "T" and len(payload) > 14:
                    self._on_tx(c, payload)

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
            r = dict(dst=dst, src=src, tx=tx, end_us=end, link=link, data=data, corrupt=None)
            if dst.tx_until_us > start:
                r["corrupt"] = "dst transmitting"
            for other in dst.receptions:
                if other["end_us"] > start:       # overlap in time at this receiver
                    other["corrupt"] = other["corrupt"] or "collision"
                    r["corrupt"] = r["corrupt"] or "collision"
            dst.receptions.append(r)
            self._seq += 1
            heapq.heappush(self._pending, (time.monotonic() + toa / 1e6 / self.scale, self._seq, id(r), r))

    def _deliver_due(self):
        with self.lock:
            now = time.monotonic()
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
