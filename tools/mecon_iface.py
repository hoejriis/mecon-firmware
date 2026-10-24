"""Reference client for the MECON device interface (docs/contract/DEVICE_INTERFACE.md).

Transport-neutral: give it a function that sends one Companion frame and one that returns the next frame the device
sends (or None on timeout). It splits requests into CMD_MECON (0x7F) frames, skips native Companion frames while it
waits, reassembles the result and returns the decoded envelope.

    client = IfaceClient(send_frame, read_frame)
    hello = client.request("hello")            # {"v":1,"id":...,"status":"ok","result":{...}}
"""
import itertools
import json
import time

FRAME_CODE = 0x7F
FRAME_PAYLOAD = 168
MAX_MSG = 4096


def fragment(msg: bytes, xid: int):
    """The CMD_MECON frames of one message, in order."""
    if not msg or len(msg) > MAX_MSG:
        raise ValueError("message must be 1 to %d bytes" % MAX_MSG)
    count = (len(msg) + FRAME_PAYLOAD - 1) // FRAME_PAYLOAD
    return [bytes([FRAME_CODE, xid & 0xFF, i, count]) + msg[i * FRAME_PAYLOAD:(i + 1) * FRAME_PAYLOAD]
            for i in range(count)]


class Reassembler:
    """One inbound message at a time, as the device does: frames in any order, duplicates ignored."""

    def __init__(self):
        self.xid = None
        self.parts = {}
        self.count = 0

    def feed(self, frame: bytes):
        """Returns the complete message, or None while frames are missing."""
        if len(frame) < 5 or frame[0] != FRAME_CODE:
            raise ValueError("not a CMD_MECON frame")
        xid, index, count = frame[1], frame[2], frame[3]
        if not 1 <= count <= 25 or index >= count:
            raise ValueError("bad frame header")
        if xid != self.xid:
            self.xid, self.parts, self.count = xid, {}, count
        self.parts.setdefault(index, frame[4:])
        if len(self.parts) == self.count:
            msg = b"".join(self.parts[i] for i in range(self.count))
            self.xid, self.parts = None, {}
            return msg
        return None


class IfaceClient:
    def __init__(self, send_frame, read_frame, id_prefix="r"):
        self.send_frame = send_frame
        self.read_frame = read_frame
        self.rx = Reassembler()
        self.xids = itertools.count(1)
        self.ids = itertools.count(1)
        self.id_prefix = id_prefix
        self.native = []          # native frames seen while waiting, for the caller
        self.events = []          # event envelopes seen while waiting for a result

    def send_raw(self, msg: bytes):
        for f in fragment(msg, next(self.xids)):
            self.send_frame(f)

    def envelope(self, op, args=None, rid=None, v=1, **extra):
        env = {"v": v, "id": rid or "%s-%d" % (self.id_prefix, next(self.ids)), "op": op}
        if args is not None:
            env["args"] = args
        env.update(extra)
        return env

    def read_message(self, timeout=5.0):
        """The next complete interface message (decoded JSON), skipping native frames; None on timeout."""
        end = time.time() + timeout
        while time.time() < end:
            f = self.read_frame(min(0.5, max(0.05, end - time.time())))
            if f is None:
                continue
            if not f or f[0] != FRAME_CODE:
                self.native.append(f)
                continue
            msg = self.rx.feed(f)
            if msg is not None:
                return json.loads(msg.decode("utf-8"))
        return None

    def request(self, op, args=None, rid=None, timeout=5.0, **extra):
        """Sends one request and returns its result envelope (events that arrive first are kept in self.events)."""
        env = self.envelope(op, args, rid, **extra)
        self.send_raw(json.dumps(env, separators=(",", ":")).encode("utf-8"))
        return self.wait_result(env["id"], timeout)

    def wait_result(self, rid, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            m = self.read_message(max(0.05, end - time.time()))
            if m is None:
                continue
            if "ev" in m:
                self.events.append(m)
                continue
            if m.get("id") in (rid, ""):   # "" = a result the device could not tie to an id (busy, incomplete)
                return m
        raise TimeoutError("no result for %s" % rid)
