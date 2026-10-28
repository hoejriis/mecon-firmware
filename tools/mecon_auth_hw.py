#!/usr/bin/env python3
"""Authority layer on a real Companion over USB (docs/contract/AUTHORITY.md), with the public test keys.

    python3 tools/mecon_auth_hw.py /dev/ttyACM0

Provisions the board, runs signed requests (duplicate, replay, a reboot in between), checks that an unsigned write is
refused once enrolled, sets a local unlock secret and unlocks, then unenrols with a signed deployment.reset. The board
ends with no record. Test keys only (test/fixtures/deployment_authority_public_v1.json); never use on a real fleet.
"""
import base64
import hashlib
import hmac
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "sim"))
import ed25519_ref        # noqa: E402  (pure Python RFC 8032; checked against the vectors by the sim suite)
import mecon_iface        # noqa: E402
from mecon_iface_hw import serial_transport   # noqa: E402

V = json.load(open(os.path.join(ROOT, "test", "fixtures", "deployment_authority_public_v1.json")))
DEP = V["deployment_id"]
INST_A = V["instance_ids"]["a"]
SEED_A = bytes.fromhex(V["keys"]["instance a"]["seed_hex"])
SET1 = V["authority_set_rev1"]
UNLOCK = bytes(range(32))   # a test secret

results = []


def check(name, ok, detail=""):
    results.append(ok)
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail if not ok else ""))


def wrap(node_key, op, args, job, nonce=None):
    body = {"v": 1, "id": "hw-%s-%s" % (job, op), "op": op, "args": args, "issuer": INST_A, "deployment_id": DEP,
            "authority_revision": 1, "issued_at": 1790001100, "target_public_key": node_key, "job_id": job}
    if nonce:
        body["channel_nonce"] = nonce
    text = json.dumps(body, separators=(",", ":"))
    sig = ed25519_ref.sign(SEED_A, b"MECON-DEVMSG-v1\x00iface\x00" + text.encode())
    return {"mecon_auth": 1, "issuer": INST_A, "sig": base64.b64encode(sig).decode(), "body": text}


def connect(port):
    send, read, close = serial_transport(port)
    end = time.time() + 90
    while time.time() < end:
        send(bytes([1, 3]) + bytes(6) + b"hwtest")
        f = read(2.0)
        while f is not None and f[:1] != b"\x05":
            f = read(0.5)
        if f is not None:
            return mecon_iface.IfaceClient(send, read), close
    sys.exit("the board never answered CMD_APP_START")


def send(i, w):
    i.send_raw(json.dumps(w, separators=(",", ":")).encode())
    return i.wait_result(json.loads(w["body"])["id"], 10)


def main():
    port = sys.argv[1]
    i, close = connect(port)
    hello = i.request("hello")["result"]
    nk = hello["node_key"]
    st = i.request("status.get")["result"]
    check("starts with no record", "deployment" not in st, st.get("deployment"))

    r = i.request("deployment.provision", {"deployment": {
        "contract_version": 1, "deployment_id": DEP, "deployment_name": "HW test", "node_public_key": nk,
        "provision_revision": 1, "dak": V["keys"]["dak epoch 1"]["public_key"], "authority_epoch": 1,
        "authority_set": SET1["authority_set"], "authority_set_sig": SET1["sig"]}})
    check("unsigned first provisioning over USB", r["status"] == "ok", r)

    def name(n):
        return {"set": {"name": n}}

    w1 = wrap(nk, "config.apply", name("hw.signed1"), 1)
    r1 = send(i, w1)
    check("signed config.apply runs", r1["status"] == "ok", r1)
    check("the same bytes get the stored result", send(i, w1) == r1)
    r2 = send(i, wrap(nk, "config.apply", name("hw.signed2"), 2))
    check("a newer job runs", r2["status"] == "ok", r2)
    rr = send(i, w1)
    check("the older job is refused as replay", rr.get("error") == "replay", rr)
    ru = i.request("config.apply", name("hw.unsigned"))
    check("an unsigned write is refused once enrolled", ru["status"] == "refused", ru)

    close()                    # reopening the port resets the ESP32: the mark must survive the reboot
    time.sleep(3)
    i, close = connect(port)
    g = i.request("deployment.get")["result"]
    mark = [m for m in g["members"] if m.get("instance_id") == INST_A]
    check("the mark survived the reboot", bool(mark) and mark[0].get("mark") == 2, g.get("members"))
    d = send(i, wrap(nk, "config.apply", name("hw.signed2"), 2))
    check("the job at the mark is a duplicate after the reboot", d.get("error") == "duplicate", d)
    check("status.deployment is reported", "deployment" in i.request("status.get")["result"])

    nonce = i.request("hello")["result"].get("channel_nonce")
    check("hello reports a channel nonce", bool(nonce))
    su = send(i, wrap(nk, "deployment.set_unlock", {"unlock_secret": UNLOCK.hex()}, 3, nonce))
    check("signed set_unlock over USB", su["status"] == "ok", su)
    ch = i.request("deployment.unlock_challenge")
    chal = bytes.fromhex(ch["result"]["challenge"]) if ch["status"] == "ok" else b""
    bad = i.request("deployment.unlock", {"response": "00" * 32})
    check("a wrong unlock response is refused", bad["status"] != "ok", bad)
    ch = i.request("deployment.unlock_challenge")
    chal = bytes.fromhex(ch["result"]["challenge"])
    good = i.request("deployment.unlock", {"response": hmac.new(UNLOCK, chal, hashlib.sha256).hexdigest()})
    check("the HMAC answer unlocks", good["status"] == "ok", good)
    dump = json.dumps([i.request("deployment.get"), i.request("status.get")])
    check("the secret appears in no reply", UNLOCK.hex() not in dump)

    rs = send(i, wrap(nk, "deployment.reset", {}, 4))
    check("signed deployment.reset over USB", rs["status"] == "ok", rs)
    check("no record afterwards", "deployment" not in i.request("status.get")["result"])
    close()
    print("%d of %d passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
