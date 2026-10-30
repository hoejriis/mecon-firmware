#!/usr/bin/env python3
"""#158 on a real nRF52 Repeater console: a first provisioning, signed requests (verify and sign on the worker), and the
loop and worker stack margins. Public test keys only (test/fixtures/deployment_authority_public_v1.json).

    mecon_worker_hw.py <port>        one session: status, provision, a signed config.apply, a signed deployment.reset,
                                     status again; PASS/FAIL lines, exit non-zero on a failure
"""
import base64
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "sim"))
import ed25519_ref  # noqa: E402
import mecon_marker_hw as m  # noqa: E402

V = m.V
SEED_A = bytes.fromhex(V["keys"]["instance a"]["seed_hex"])
INST_A = V["instance_ids"]["a"]
failed = False


def check(name, ok, detail=""):
    global failed
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + json.dumps(detail)[:300]))
    failed = failed or not ok


def signed(c, node_key, op, args, job):
    body = {"v": 1, "id": "w%x-%d" % (int(time.time()), job), "op": op}
    if args is not None:
        body["args"] = args
    body.update({"issuer": INST_A, "deployment_id": V["deployment_id"], "authority_revision": 1, "issued_at": 1790001100,
                 "target_public_key": node_key, "job_id": job})
    text = json.dumps(body, separators=(",", ":"))
    sig = ed25519_ref.sign(SEED_A, b"MECON-DEVMSG-v1\x00iface\x00" + text.encode())
    c.send_raw(json.dumps({"mecon_auth": 1, "issuer": INST_A, "sig": base64.b64encode(sig).decode(), "body": text},
                          separators=(",", ":")))
    return c.wait_result(body["id"], timeout=15)


def stacks(c, tag):
    h = c.request("status.get", timeout=10)["result"].get("heap") or {}
    print("%s loop_stack_free=%s worker_stack_free=%s heap_free=%s" % (tag, h.get("loop_stack_free"),
                                                                        h.get("worker_stack_free"), h.get("free")))
    return h


def main():
    c, h, close = m.console(sys.argv[1])
    key = h["result"]["node_key"]
    g = c.request("deployment.get")["result"]
    if g.get("state") == "provisioned":
        # An unsigned reset is refused on a healthy record, and a signed one needs that record's issuer and job ids.
        close()
        sys.exit("the board already holds a record: clear it with a signed deployment.reset first")
    if g.get("state") != "none":
        r = c.request("deployment.reset")
        check("start from no record", r.get("status") == "ok", r)
    stacks(c, "before:")
    r = c.request("deployment.provision", m.provision_args(key), timeout=15)
    check("a first provisioning answers (no reset)", r.get("status") == "ok", r)
    check("its result is signed", "usig" in r, sorted(r.keys()))
    r = signed(c, key, "config.apply", {"set": {"name": "worker test"}}, 1)
    check("a signed config.apply verifies and runs", r.get("status") == "ok" and r["result"].get("complete") is True, r)
    r = signed(c, key, "deployment.reset", None, 2)
    check("a signed deployment.reset unenrols", r.get("status") == "ok", r)
    check("then no record", c.request("deployment.get")["result"] == {"state": "none"})
    hp = stacks(c, "after: ")
    lf, wf = hp.get("loop_stack_free"), hp.get("worker_stack_free")
    check("loop stack margin >= 1 KB", isinstance(lf, int) and lf >= 1024, lf)
    check("worker stack reported, margin >= 1 KB", isinstance(wf, int) and wf >= 1024, wf)
    close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
