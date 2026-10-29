"""The job operations of the device interface (docs/contract/OPERATIONS.md section 3) on the Companion and the Repeater
and Room server console: job.get, job.list, job.ack, job.cancel and job.confirm, and the `jobs` member of the status
snapshot. Nothing in this build creates a job yet, so these check the empty table, the answers for an unknown job, the
reboot, and who may call what once a device is enrolled. The table itself is test/test_jobs and test/test_job_service.
Simulated boards."""
import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_iface  # noqa: E402
import mecon_schema  # noqa: E402
from test_deployment_authority import boot as dep_boot, wrap  # noqa: E402
from test_device_interface import PIN, ble_boot, start as companion_start, client as companion_client  # noqa: E402
from test_device_interface_server import start as server_start, client as server_client  # noqa: E402

JOB_OPS = ["job.get", "job.list", "job.ack", "job.cancel", "job.confirm"]


def ok(answer, schema):
    assert answer["status"] == "ok", answer
    problems = mecon_schema.check(answer["result"], mecon_schema.load(schema))
    assert not problems, problems
    return answer["result"]


def check_empty(i):
    assert set(ok(i.request("hello"), "hello.result.json")["capabilities"]) >= set(JOB_OPS)
    assert ok(i.request("status.get"), "status.get.result.json")["jobs"] == {"open": 0, "owed": 0}
    assert ok(i.request("job.list"), "job.list.result.json") == {"jobs": [], "open": 0, "owed": 0}
    for op in JOB_OPS[:1] + JOB_OPS[2:]:
        r = i.request(op, {"job": "nobody"})
        assert r["status"] == "invalid" and r["error"] == "unknown-job", (op, r)
        r = i.request(op, {})
        assert r["status"] == "invalid" and r["error"] == "bad-args", (op, r)


def test_the_companion_answers_an_empty_table_and_keeps_it_over_a_reboot(nodes):
    n, (c, i) = companion_start(nodes, "jobs")
    check_empty(i)
    n.stop()
    n.start()
    c, i = companion_client(n)
    check_empty(i)


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_the_console_answers_an_empty_table_and_keeps_it_over_a_reboot(nodes, kind):
    n, (cli, i) = server_start(nodes, kind, "jobs" + kind)
    check_empty(i)
    n.stop()
    n.start()
    cli, i = server_client(n)
    check_empty(i)


def test_once_enrolled_an_unsigned_caller_may_read_jobs_but_not_release_or_change_them(nodes):
    d = dep_boot(nodes, "jobauth")
    assert d.provision()["status"] == "ok"
    assert d.i.request("job.list")["status"] == "ok"
    assert d.i.request("job.get", {"job": "x"})["error"] == "unknown-job", "a read reaches the table"
    for op in ["job.ack", "job.cancel", "job.confirm"]:
        r = d.i.request(op, {"job": "x"})
        assert r["status"] == "refused" and r["error"] == "unauthenticated", (op, r)
    # signed: the reads are plain (no job id), the rest are jobs like any write
    r = d.send(wrap(d.node_key, "job.list", job=None))
    assert r["status"] == "ok" and r["result"]["open"] == 0, r
    r = d.send(wrap(d.node_key, "job.ack", {"job": "x"}, job=None))
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "a release without a job id is not replay-safe"
    r = d.send(wrap(d.node_key, "job.ack", {"job": "x"}, job=1))
    assert r["status"] == "invalid" and r["error"] == "unknown-job", r


# ------------------------------------------------------------------ commit-or-revert (OPERATIONS.md section 3.4)

def ble(n):
    b = n.connect_ble(PIN)
    b.app_start()
    return b, mecon_iface.IfaceClient(b._send, b._read_frame)


def tx_of(i):
    return ok(i.request("config.get", {"keys": ["tx"]}), "config.get.result.json")["values"]["tx"]


@pytest.mark.timescale(10)
def test_a_paired_phone_must_ask_to_confirm_and_a_confirmed_change_stays(nodes):
    n = ble_boot(nodes, "cf", PIN)
    b, i = ble(n)
    before = tx_of(i)
    r = i.request("config.apply", {"set": {"tx": before - 1}})
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    assert tx_of(i) == before, "nothing written"
    r = i.request("config.apply", {"set": {"tx": before - 1}, "confirm": {"within_s": 60}})
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    job = r["result"]["job"]
    assert tx_of(i) == before - 1, "applied at once, tentatively"
    assert ok(i.request("status.get"), "status.get.result.json")["jobs"] == {"open": 1, "owed": 0}
    g = ok(i.request("job.get", {"job": job}), "job.get.result.json")
    assert g["state"] == "awaiting-confirm" and 0 < g["confirm_left_s"] <= 60, g
    r = i.request("job.confirm", {"job": job})
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r
    assert tx_of(i) == before - 1
    assert ok(i.request("status.get"), "status.get.result.json")["jobs"] == {"open": 0, "owed": 1}


@pytest.mark.timescale(10)
def test_an_unconfirmed_change_reverts_at_its_deadline(nodes):
    n = ble_boot(nodes, "dl", PIN)
    b, i = ble(n)
    before = tx_of(i)
    r = i.request("config.apply", {"set": {"tx": before - 2}, "confirm": {"within_s": 30}})
    job = r["result"]["job"]
    deadline = time.time() + 30
    state = None
    while time.time() < deadline:   # 30 device seconds at timescale 10
        state = i.request("job.get", {"job": job})["result"]["state"]
        if state != "awaiting-confirm":
            break
        time.sleep(0.5)
    assert state == "reverted", state
    assert tx_of(i) == before


@pytest.mark.timescale(10)
def test_a_cancel_reverts_and_a_reboot_while_pending_reverts_at_boot(nodes):
    n = ble_boot(nodes, "cx", PIN)
    b, i = ble(n)
    before = tx_of(i)
    job = i.request("config.apply", {"set": {"tx": before - 3}, "confirm": {}})["result"]["job"]
    r = i.request("job.cancel", {"job": job})
    assert r["status"] == "ok" and r["result"]["state"] == "reverted", r
    assert tx_of(i) == before
    job = i.request("config.apply", {"set": {"tx": before - 4}, "confirm": {}})["result"]["job"]
    b.close()
    n.restart()   # the change took effect in the boot before: this is the second boot while it is pending
    b, i = ble(n)
    assert tx_of(i) == before, "reverted at boot, before anyone could use the device"
    g = ok(i.request("job.get", {"job": job}), "job.get.result.json")
    assert g["state"] == "reverted" and g["owed"] is True, g


def test_a_signed_caller_on_the_cable_is_not_the_cable(nodes):
    d = dep_boot(nodes, "sgn")
    assert d.provision()["status"] == "ok"
    r = d.send(wrap(d.node_key, "config.apply", {"set": {"tx": 10}}, job=1))
    assert r["status"] == "invalid" and r["error"] == "confirm-required", r
    r = d.send(wrap(d.node_key, "config.apply", {"set": {"tx": 10}, "confirm": {}}, job=2))
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    job = r["result"]["job"]
    assert job.startswith("#") and job.endswith(":2"), "a signed job is named by its issuer and job id"
    r = d.send(wrap(d.node_key, "job.confirm", {"job": job}, job=3))
    assert r["status"] == "ok" and r["result"]["state"] == "confirmed", r
    r = d.i.request("config.apply", {"set": {"tx": 11}})
    assert r["status"] == "refused" and r["error"] == "unauthenticated", "an enrolled device takes no unsigned write"


@pytest.mark.parametrize("kind", ["repeater", "room_server"])
def test_the_console_reverts_an_unconfirmed_tx_at_boot(nodes, kind):
    n, (cli, i) = server_start(nodes, kind, "cr" + kind)
    before = tx_of(i)
    r = i.request("config.apply", {"set": {"tx": before - 1}})
    assert r["status"] == "ok" and r["result"]["complete"] is True, "the console is the cable"
    r = i.request("config.apply", {"set": {"tx": before - 2}, "confirm": {}})
    assert r["status"] == "ok" and r["result"]["state"] == "awaiting-confirm", r
    job = r["result"]["job"]
    n.stop()
    n.start()
    cli, i = server_client(n)
    assert tx_of(i) == before - 1
    assert ok(i.request("job.get", {"job": job}), "job.get.result.json")["state"] == "reverted"
