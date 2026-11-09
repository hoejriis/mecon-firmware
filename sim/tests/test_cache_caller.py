"""The result cache answers a duplicate id only to the caller that produced it (#476). A hit runs no handler, so a caller
check made inside one (ble.pin.get, identity.key.export, device.reset bonds, deployment.reset) would not see a replayed
id; another caller gets id-in-use, and nothing runs twice."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import Dev, apply_name  # noqa: E402


def test_a_phone_reusing_the_cables_id_gets_id_in_use_and_nothing_runs(nodes):
    from test_jobs import PIN, ble, ble_boot
    n = ble_boot(nodes, "ccc", PIN)
    d = Dev(n)
    a = d.i.request("config.apply", apply_name("ByCable"), rid="w1")
    assert a["status"] == "ok", a
    b, i = ble(n)
    r = i.request("config.apply", apply_name("ByPhone"), rid="w1")
    assert r["status"] == "invalid" and r["error"] == "id-in-use", "another caller's stored result: %s" % r
    assert "ByCable" in d.name() and "ByPhone" not in d.name(), "and it did not run"
    b.close()
    again = d.i.request("config.apply", apply_name("ByCable"), rid="w1")
    assert again.raw == a.raw, "the caller that made it still gets its stored result"
