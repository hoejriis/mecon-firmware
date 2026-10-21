"""Measure lock-step virtual time: virtual seconds per wall second for N idle Repeaters at a given quantum.

usage (inside the dev container): python3 sim/lockstep_speed.py [--nodes 3] [--quantum-us 1000 10000] [--seconds 20]
"""
import argparse
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from sim import harness  # noqa: E402
from sim.ether import Ether  # noqa: E402


def measure(n, q, seconds):
    with tempfile.TemporaryDirectory() as d:
        e = Ether(os.path.join(d, "ether.sock"), seed=1, lockstep=True, start_after=n).start()
        nodes = []
        try:
            for i in range(n):
                nd = harness.Node("repeater", "r%d" % i, d, ether_path=e.path, seed=i + 1, lockstep=True, quantum_us=q)
                nodes.append(nd.start())
            for i in range(n):
                for j in range(n):
                    if i != j:
                        e.set_link("r%d" % i, "r%d" % j, both=False)
            e.wait_until_us(1e6, timeout=60)        # past boot
            v0, b0, w0 = e.now_us(), e.barriers, time.monotonic()
            time.sleep(seconds)
            v1, b1, w1 = e.now_us(), e.barriers, time.monotonic()
            return (v1 - v0) / 1e6 / (w1 - w0), (b1 - b0) / (w1 - w0), len(e.tx_log)
        finally:
            for nd in nodes:
                nd.stop()
            e.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nodes", type=int, default=3)
    ap.add_argument("--quantum-us", type=int, nargs="+", default=[1000, 10000])
    ap.add_argument("--seconds", type=float, default=20.0)
    a = ap.parse_args()
    for q in a.quantum_us:
        speed, bps, ntx = measure(a.nodes, q, a.seconds)
        print("LOCKSTEP nodes=%d quantum_us=%d: %.1f virtual s per wall s, %.0f barriers per wall s (%d transmissions)"
              % (a.nodes, q, speed, bps, ntx), flush=True)


if __name__ == "__main__":
    main()
