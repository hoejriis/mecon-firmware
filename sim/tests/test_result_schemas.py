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
                        "l": {"type": "array", "minItems": 1, "maxItems": 2}},
         "additionalProperties": {"type": "integer"}}
    assert mecon_schema.check({"a": "ab", "n": 3, "l": [1, 2], "x": 7}, s) == []
    for bad in ({"a": "a"}, {"a": "abcd"}, {"n": 0}, {"n": 6}, {"l": []}, {"l": [1, 2, 3]}, {"x": "seven"}, {}):
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


@pytest.mark.timescale(10)
def test_the_management_channel_changes_answer_in_their_schemas(nodes):
    from test_mgmt_channel import KEY, NEW, ble, companion_client, put
    n = ble_boot(nodes, "rmg", PIN)
    u, ui = companion_client(n)
    assert put(ui, 2, "ops", KEY)["status"] == "ok"
    ok(ui.request("mgmt.channel.bind", {"slot": 2}), "mgmt.channel.bind")
    ok(ui.request("mgmt.channel.rotate", {"secret": NEW.hex()}), "mgmt.channel.rotate")
    assert ok(ui.request("mgmt.channel.unbind", {"remove": False}), "mgmt.channel.unbind")["removed"] is False
    u.close()
    b, i = ble(n)   # a paired phone's change carries its job and its deadline
    r = ok(i.request("mgmt.channel.bind", {"slot": 2, "confirm": {"within_s": 60}}), "mgmt.channel.bind")
    assert r["job_state"] == "awaiting-confirm" and "job" in r, r


def test_identity_rotate_and_rotation_answer_in_their_schemas(nodes):
    from test_identity_rotate import enrolled_companion, rotate
    d = enrolled_companion(nodes, "rir", 43)
    assert ok(d.i.request("identity.rotation"), "identity.rotation") == {}, "none yet"
    old = d.node_key
    r = ok(rotate(d, 1, old), "identity.rotate")
    assert r["old_public_key"] == old and "node_key" in r["removed"], r
    rot = ok(d.i.request("identity.rotation"), "identity.rotation")
    assert rot["rotation"]["statement"], rot


def test_the_update_operations_answer_in_their_schemas(nodes):
    import base64, hashlib
    from test_update_install import CHUNK, NEW, image, offer, start
    n, d = start(nodes, "rup", 61)
    assert ok(d.i.request("update.status"), "update.status")["job"] is None
    img = image()
    text = d.manifest(version=NEW, image={"size": len(img), "sha256": hashlib.sha256(img).hexdigest()})
    m = ok(d.stage(text), "update.manifest")
    assert m["preflight"] == "ok", m
    o = ok(offer(d, m["sha256"]), "update.offer")
    assert len(o["transfer_token"]) == 32 and o["next"] == 0, "a pushed image gets its token here, and only here"
    st = ok(d.i.request("update.status"), "update.status")
    assert st["job"]["job"] == o["job"] and len(st["staged"]) == 1, st
    data = base64.b64encode(img[:CHUNK]).decode()
    c = ok(d.i.request("update.chunk", {"job": o["job"], "token": o["transfer_token"], "offset": 0, "data": data}), "update.chunk")
    assert c["next"] == CHUNK, c


def test_the_deployment_operations_answer_in_their_schemas(nodes):
    from test_deployment_authority import SECRET, boot as dep_boot, provision_args, try_unlock, update_args, wrap
    d = dep_boot(nodes, "rdp")
    assert ok(d.i.request("deployment.get"), "deployment.get") == {"state": "none"}
    p = ok(d.i.request("deployment.provision", provision_args(d.node_key)), "deployment.provision")
    assert p["state"] == "provisioned" and p["node_key"] == d.node_key, p
    g = ok(d.i.request("deployment.get"), "deployment.get")
    assert g["members"] and "device_id" not in g, "the pair is read back at enrolment only"
    assert ok(d.i.request("deployment.authority_update", update_args()), "deployment.authority_update")["authority_revision"] == 2
    ok(d.send(wrap(d.node_key, "deployment.set_unlock", {"unlock_secret": SECRET}, job=3, nonce=d.nonce())), "deployment.set_unlock")
    ch = ok(d.i.request("deployment.unlock_challenge"), "deployment.unlock_challenge")
    assert len(ch["challenge"]) == 64, ch
    ok(try_unlock(d.i.request), "deployment.unlock")
    r = ok(d.send(wrap(d.node_key, "deployment.reset", job=4)), "deployment.reset")
    assert "record" in r["removed"], r


def test_the_region_operations_answer_in_their_schemas(ether, nodes):
    d = target(nodes, ether, "repeater", "rrg")
    first = ok(d.i.request("region.list"), "region.list")
    assert first["capacity"] >= 1 and first["next"] is None, first
    assert ok(d.i.request("region.put", {"name": "city"}), "region.put")["parent"] == "*"
    assert ok(d.i.request("region.put", {"name": "old-town", "parent": "city"}), "region.put")["parent"] == "city"
    ok(d.i.request("region.flood", {"name": "old-town", "flood": "deny"}), "region.flood")
    ok(d.i.request("region.home", {"name": "city"}), "region.home")
    ok(d.i.request("region.default", {"name": "old-town"}), "region.default")
    page = ok(d.i.request("region.list"), "region.list")
    assert page["home"] == "city" and page["default"] == "old-town" and len(page["regions"]) == 2, page
    ok(d.i.request("region.default", {"name": None}), "region.default")
    ok(d.i.request("region.remove", {"name": "old-town"}), "region.remove")


def test_every_capability_has_a_result_schema():
    # The contract's schema set covers every operation a build offers; a new operation without one fails here (#449)
    import re
    root = os.path.join(os.path.dirname(__file__), "..", "..")
    caps = set()
    for f in ("src/helpers/iface/ConsoleIface.cpp", "examples/companion_radio/MeconIface.cpp"):
        src = open(os.path.join(root, f)).read()
        for m in re.finditer(r"const char\* const (\w*CAPABILITIES)\[\] = \{(.*?)\};", src, re.S):
            caps |= set(re.findall(r'"([a-z][a-z0-9_.]*)"', m.group(2)))
    assert len(caps) > 40, caps   # the lists were found
    have = {f[:-len(".result.json")] for f in os.listdir(os.path.join(root, "docs", "contract", "schema"))
            if f.endswith(".result.json")}
    assert not sorted(caps - have), "no result schema for: %s" % sorted(caps - have)
