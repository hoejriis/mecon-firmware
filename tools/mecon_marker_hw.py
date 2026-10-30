#!/usr/bin/env python3
"""The provisioned marker on a real nRF52 Repeater console (docs/contract/AUTHORITY.md section 4), with the public test
keys (test/fixtures/deployment_authority_public_v1.json). Never on a real fleet.

    mecon_marker_hw.py <port> state                 hello and deployment.get
    mecon_marker_hw.py <port> reset                 an unsigned deployment.reset over the cable
    mecon_marker_hw.py <port> provision             a first provisioning over the cable; expects ok
    mecon_marker_hw.py <port> expect-corrupt        files gone, marker kept: corrupt, and a provisioning is refused

Prints PASS/FAIL lines and exits non-zero on a failure. The nRF52 console needs DTR asserted.
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mecon_iface  # noqa: E402

V = json.load(open(os.path.join(HERE, "..", "test", "fixtures", "deployment_authority_public_v1.json")))
failed = False


def check(name, ok, detail=""):
    global failed
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + json.dumps(detail)[:300]))
    failed = failed or not ok


def console(port):
    import serial
    s = serial.Serial(port, 115200, timeout=0.05)
    s.dtr = True
    buf = bytearray()

    def write(data):
        s.write(data)
        s.flush()

    def read_line(timeout):
        nonlocal buf
        end = time.time() + timeout
        while time.time() < end:
            buf += s.read(4096)
            if b"\n" in buf:
                line, _, rest = bytes(buf).partition(b"\n")
                buf = bytearray(rest)
                return line.decode("utf-8", "replace").rstrip("\r")
        return None

    # A fresh id prefix per session: the device keeps the results of mutating requests by id, so a second session
    # reusing "c-1" would get the first session's answer back instead of a run.
    c = mecon_iface.ConsoleClient(write, read_line, id_prefix="m%x" % int(time.time()))
    end = time.time() + 60
    while time.time() < end:
        try:
            h = c.request("hello", timeout=3)
            return c, h, s.close
        except TimeoutError:
            pass
    sys.exit("the console never answered hello")


def provision_args(node_key):
    s1 = V["authority_set_rev1"]
    return {"deployment": {"contract_version": 1, "deployment_id": V["deployment_id"], "deployment_name": "Marker test",
                           "node_public_key": node_key, "provision_revision": 1,
                           "dak": V["keys"]["dak epoch 1"]["public_key"], "authority_epoch": 1,
                           "authority_set": s1["authority_set"], "authority_set_sig": s1["sig"]}}


def main():
    port, step = sys.argv[1:3]
    c, h, close = console(port)
    key = h["result"]["node_key"]
    g = c.request("deployment.get")
    print("hello: role=%s firmware=%s; deployment.get: %s" % (h["result"].get("role"), h["result"].get("firmware"),
                                                              json.dumps(g.get("result"))[:120]))
    if step == "reset":
        r = c.request("deployment.reset")
        check("unsigned reset over the cable", r.get("status") == "ok", r)
        check("then no record", c.request("deployment.get")["result"] == {"state": "none"})
    elif step == "provision":
        r = c.request("deployment.provision", provision_args(key))
        check("first provisioning over the cable", r.get("status") == "ok", r)
        check("the record reads back", c.request("deployment.get")["result"].get("state") == "provisioned")
    elif step == "expect-corrupt":
        check("files gone, marker kept: corrupt", g.get("result") == {"state": "corrupt"}, g)
        r = c.request("deployment.provision", provision_args(key))
        check("a provisioning is refused", r.get("status") == "refused" and r.get("error") == "deployment_record_corrupt", r)
    close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
