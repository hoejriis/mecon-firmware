#!/usr/bin/env python3
"""The simulated-board suite in shards, so no job meets its time limit.

    sim/shard.py <index> <count>   prints the test files of one shard

Files go to the shard with the least so far, slowest first, so the split is the same on every runner. SLOW holds what
one full run measured, in seconds. It need not be exact: a file not listed counts as 30."""
import glob
import os
import sys

SLOW = {
    "test_duty_cycle.py": 427, "test_message_ops.py": 278, "test_inbox.py": 220, "test_telemetry_streams.py": 217,
    "test_contacts.py": 213, "test_identity_rotate.py": 153, "test_jobs.py": 139, "test_companion_mqtt.py": 122,
    "test_device_interface_server.py": 105, "test_mqtt_nonblocking.py": 92, "test_deployment_authority.py": 90,
    "test_mqtt_interleave.py": 87, "test_postures_admin.py": 84, "test_heap_floor.py": 75, "test_device_interface.py": 72,
    "test_connectivity_mode.py": 61,
}
DEFAULT = 30


def shards(count, root=None):
    root = root or os.path.join(os.path.dirname(os.path.abspath(__file__)), "tests")
    files = sorted(os.path.basename(p) for p in glob.glob(os.path.join(root, "test_*.py")))
    out = [[] for _ in range(count)]
    load = [0] * count
    for f in sorted(files, key=lambda f: (-SLOW.get(f, DEFAULT), f)):
        i = load.index(min(load))
        out[i].append(f)
        load[i] += SLOW.get(f, DEFAULT)
    return out, load


if __name__ == "__main__":
    index, count = int(sys.argv[1]), int(sys.argv[2])
    print(" ".join("sim/tests/" + f for f in sorted(shards(count)[0][index])))
