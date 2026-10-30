#!/usr/bin/env python3
"""Cost of the inventory stream on a real Companion over USB (docs/contract/TELEMETRY.md).

    python3 tools/mecon_stream_hw.py /dev/ttyUSB0 [contacts]

Adds test contacts over the Companion protocol, then measures through the device interface:
- the heap before (status.get);
- one inventory subscription to the end of its snapshot (pages, bytes, time);
- the heap after;
- the longest main-loop pass meanwhile (runtime stream, loop_gap_ms.max).
A build whose inventory is counts only (before the private streams) sends one event, which counts as a one-page snapshot.
Prints one JSON line. The test contacts are made-up keys; the board should be erased or restored afterwards.
"""
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mecon_iface                      # noqa: E402
from mecon_iface_hw import serial_transport   # noqa: E402

CMD_APP_START, CMD_ADD_UPDATE_CONTACT = 1, 9
RESP_OK, RESP_SELF_INFO = 0, 5


def app_start(send, read):
    end = time.time() + 90
    while time.time() < end:
        send(bytes([CMD_APP_START, 3]) + bytes(6) + b"hwtest")
        f = read(2.0)
        while f is not None and f[:1] != bytes([RESP_SELF_INFO]):
            f = read(0.5)
        if f is not None:
            return
    sys.exit("the board never answered CMD_APP_START")


def add_contacts(send, read, n):
    ok = 0
    for k in range(n):
        key = bytes([0xA0, k & 0xFF, k >> 8]) + bytes(range(3, 32))
        name = ("hw-test-%03d" % k).encode()
        send(bytes([CMD_ADD_UPDATE_CONTACT]) + key + bytes([1, 0, 0xFF]) + b"\0" * 64 + name.ljust(32, b"\0") +
             struct.pack("<I", 0))
        f = read(3.0)
        while f is not None and f[:1] not in (bytes([RESP_OK]), b"\x01"):
            f = read(1.0)
        ok += f is not None and f[:1] == bytes([RESP_OK])
    return ok


def heap(i):
    return i.request("status.get", timeout=10)["result"]["heap"]


def next_event(i, stream, timeout):
    end = time.time() + timeout
    while time.time() < end:
        for e in list(i.events):
            if e.get("ev") == stream and not e["data"].get("boot"):
                i.events.remove(e)
                return e
        m = i.read_message(0.5)
        if m and m.get("ev") == stream and not m["data"].get("boot"):   # the runtime boot event is not a snapshot
            return m
        if m and "ev" in m:
            i.events.append(m)
    return None


def main():
    port = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    send, read, close = serial_transport(port)
    app_start(send, read)
    added = add_contacts(send, read, n)
    i = mecon_iface.IfaceClient(send, read)
    time.sleep(2.0)
    h0 = heap(i)
    r = i.request("sub.start", {"stream": "runtime", "period_s": 60, "lifetime_s": 600})
    assert r["status"] == "ok", r
    first = next_event(i, "runtime", 15)   # resets the loop-gap meter; its own value predates the snapshot
    t0 = time.time()
    r = i.request("sub.start", {"stream": "inventory", "lifetime_s": 600})
    assert r["status"] == "ok", r
    pages, sizes, entries = 0, [], 0
    while True:
        e = next_event(i, "inventory", 60)
        if e is None:
            break
        pages += 1
        sizes.append(len(e.raw))
        d = e["data"]
        entries += len(d.get("entries", []))
        if d.get("last", True):   # a counts-only build has no "last": one event is the whole answer
            break
    elapsed = time.time() - t0
    h1 = heap(i)
    gap = next_event(i, "runtime", 75)   # the next period's event covers the snapshot
    print(json.dumps({
        "contacts_added": added, "pages": pages, "entries": entries, "max_event_bytes": max(sizes) if sizes else 0,
        "snapshot_s": round(elapsed, 2), "heap_before": h0, "heap_after": h1,
        "loop_gap_ms_max_during": gap["data"]["loop_gap_ms"]["max"] if gap else None,
        "loop_gap_ms_max_before": first["data"]["loop_gap_ms"]["max"] if first else None,
    }))
    close()


if __name__ == "__main__":
    main()
