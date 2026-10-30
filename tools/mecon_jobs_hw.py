#!/usr/bin/env python3
"""Commit-or-revert on a real Companion over its USB frames (docs/contract/OPERATIONS.md section 3.4).

    mecon_jobs_hw.py <port> <state.json> apply      a tentative tx change over the cable, asked with confirm
    mecon_jobs_hw.py <port> <state.json> second     after one reboot: reverted at boot; then a change that is confirmed
    mecon_jobs_hw.py <port> <state.json> third      after another reboot: the confirmed change stayed

Opening the port resets an ESP32, so each step starts a new boot. Prints PASS/FAIL lines; exits non-zero on a failure.
"""
import json
import sys
import time

import mecon_iface
import mecon_iface_hw

failed = False


def check(name, ok, detail=""):
    global failed
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))
    failed = failed or not ok


def connect(port):
    send, read, close = mecon_iface_hw.serial_transport(port)
    end = time.time() + 90
    while time.time() < end:
        send(bytes([1, 3]) + bytes(6) + b"jobtest")
        f = read(2.0)
        while f is not None and f[:1] != b"\x05":
            f = read(0.5)
        if f is not None:
            return mecon_iface.IfaceClient(send, read), close
    sys.exit("the board never answered CMD_APP_START")


def tx(i):
    return i.request("config.get", {"keys": ["tx"]})["result"]["values"]["tx"]


def main():
    port, state_path, step = sys.argv[1:4]
    i, close = connect(port)
    if step == "apply":
        t = tx(i)
        r = i.request("config.apply", {"set": {"tx": t - 1}, "confirm": {"within_s": 300}})
        check("apply with confirm is awaiting", r.get("result", {}).get("state") == "awaiting-confirm", r)
        check("applied at once", tx(i) == t - 1)
        check("status counts the open job", i.request("status.get")["result"].get("jobs") == {"open": 1, "owed": 0})
        json.dump({"tx": t, "job": r.get("result", {}).get("job")}, open(state_path, "w"))
    elif step == "second":
        s = json.load(open(state_path))
        check("the second boot reverted tx", tx(i) == s["tx"], tx(i))
        g = i.request("job.get", {"job": s["job"]})
        check("the job reads reverted and owed", g.get("result", {}).get("state") == "reverted" and g["result"].get("owed"), g)
        r = i.request("config.apply", {"set": {"tx": s["tx"] - 2}, "confirm": {}})
        s["job2"] = r.get("result", {}).get("job")
        c = i.request("job.confirm", {"job": s["job2"]})
        check("job.confirm confirms", c.get("result", {}).get("state") == "confirmed", c)
        json.dump(s, open(state_path, "w"))
    elif step == "third":
        s = json.load(open(state_path))
        check("a confirmed change survives a reboot", tx(i) == s["tx"] - 2, tx(i))
        check("its job reads confirmed", i.request("job.get", {"job": s["job2"]})["result"]["state"] == "confirmed")
        lst = i.request("job.list")["result"]
        check("both jobs are listed, none open", lst["open"] == 0 and len(lst["jobs"]) == 2, lst)
    close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
