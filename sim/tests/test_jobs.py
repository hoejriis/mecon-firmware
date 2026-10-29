"""The job operations of the device interface (docs/contract/OPERATIONS.md section 3) on the Companion and the Repeater
and Room server console: job.get, job.list, job.ack, job.cancel and job.confirm, and the `jobs` member of the status
snapshot. Nothing in this build creates a job yet, so these check the empty table, the answers for an unknown job, the
reboot, and who may call what once a device is enrolled. The table itself is test/test_jobs and test/test_job_service.
Simulated boards."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_schema  # noqa: E402
from test_deployment_authority import boot as dep_boot, wrap  # noqa: E402
from test_device_interface import start as companion_start, client as companion_client  # noqa: E402
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
