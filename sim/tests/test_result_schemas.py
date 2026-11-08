"""The result schemas of standing.*, report.now, job.ack, job.cancel, job.confirm and posture.set
(docs/contract/schema/<op>.result.json) against what the firmware answers. Each operation once, on a path its own suite
already covers; this file only checks the shapes."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mecon_schema  # noqa: E402
from test_jobs import PIN, ble, ble_boot, tx_of  # noqa: E402
from test_lora_management import target  # noqa: E402


def ok(answer, op):
    assert answer["status"] == "ok", answer
    problems = mecon_schema.check(answer["result"], mecon_schema.load(op + ".result.json"))
    assert not problems, (op, problems, answer["result"])
    return answer["result"]


@pytest.mark.parametrize("kind", ["repeater", "companion_standalone"])
def test_standing_and_report_now_answer_in_their_schemas(ether, nodes, kind):
    d = target(nodes, ether, kind, "rs" + kind[:2])
    to = "ab" * 32
    ok(d.i.request("standing.put", {"id": "h1", "fields": ["health", "radio"], "every_s": 900, "to": to}), "standing.put")
    ok(d.i.request("standing.put", {"id": "c.2", "fields": ["health"], "every_s": 3600, "runs": 3, "durable": False,
                                    "slot": 0}), "standing.put")
    listed = ok(d.i.request("standing.list"), "standing.list")
    assert {s["id"] for s in listed["instructions"]} == {"h1", "c.2"}, listed
    assert [s for s in listed["instructions"] if s["id"] == "c.2"][0]["runs_left"] == 3
    ok(d.i.request("standing.remove", {"id": "h1"}), "standing.remove")
    ok(d.i.request("report.now", {"fields": ["health"], "to": to}), "report.now")


@pytest.mark.timescale(10)
def test_job_confirm_cancel_and_ack_answer_in_their_schemas(nodes):
    n = ble_boot(nodes, "rj", PIN)
    b, i = ble(n)
    before = tx_of(i)
    job = i.request("config.apply", {"set": {"tx": before - 1}, "confirm": {"within_s": 60}})["result"]["job"]
    assert ok(i.request("job.confirm", {"job": job}), "job.confirm")["state"] == "confirmed"
    ok(i.request("job.ack", {"job": job}), "job.ack")
    job = i.request("config.apply", {"set": {"tx": before - 2}, "confirm": {"within_s": 60}})["result"]["job"]
    assert ok(i.request("job.cancel", {"job": job}), "job.cancel")["state"] == "reverted"


def test_posture_set_answers_in_its_schema(ether, nodes):
    d = target(nodes, ether, "repeater", "rpo")
    assert ok(d.i.request("posture.set", {"posture": "hardened"}), "posture.set") == {"posture": "hardened"}
    assert d.n.wait_exit(10) == 75


def test_the_checker_enforces_every_keyword_the_schemas_use():
    s = {"type": "object", "minProperties": 1,
         "properties": {"a": {"type": "string", "minLength": 2, "maxLength": 3},
                        "n": {"type": "integer", "minimum": 1, "maximum": 5},
                        "l": {"type": "array", "maxItems": 2}},
         "additionalProperties": {"type": "integer"}}
    assert mecon_schema.check({"a": "ab", "n": 3, "l": [1, 2], "x": 7}, s) == []
    for bad in ({"a": "a"}, {"a": "abcd"}, {"n": 0}, {"n": 6}, {"l": [1, 2, 3]}, {"x": "seven"}, {}):
        assert mecon_schema.check(bad, s), bad
    assert mecon_schema.check(1, {"type": "integer", "exclusiveMinimum": 0}), "an unknown keyword is never silently passed"


def test_status_reports_the_deployment_in_its_schema_healthy_and_corrupt(nodes):
    from test_deployment_authority import boot as dep_boot, wreck_record
    d = dep_boot(nodes, "rsd")
    assert "deployment" not in ok(d.i.request("status.get"), "status.get"), "a standalone device reports none"
    assert d.provision()["status"] == "ok"
    dep = ok(d.i.request("status.get"), "status.get")["deployment"]
    assert dep["contract_version"] == 1 and len(dep["deployment_id"]) == 32 and "state" not in dep, dep
    d.n.stop()
    wreck_record(d)
    d.n.start()
    d.attach()
    dep = ok(d.i.request("status.get"), "status.get")["deployment"]
    assert dep["state"] == "corrupt" and "deployment_id" not in dep, dep
