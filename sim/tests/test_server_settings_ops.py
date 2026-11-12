"""Repeater and Room server settings and board reads over the device interface (#493; docs/contract/OPERATIONS.md 5.1):
the flood.max.unscoped and flood.max.advert keys, board.get, board.adc.set and power.saving.set, each with the class of its
CLI line. The simulated board has no bootloader version, no power management and no adjustable battery divider, so those
answer null or `unsupported` here, as a board without them does; it has power saving, as an ESP32 does."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from test_deployment_authority import wrap  # noqa: E402
from test_device_ops import next_job, shaped  # noqa: E402
from test_lora_management import target  # noqa: E402

KINDS = ["repeater", "room_server"]


def values(d, *keys):
    r = d.i.request("config.get", {"keys": list(keys)})
    assert r["status"] == "ok", r
    return r["result"]["values"]


@pytest.mark.parametrize("kind", KINDS)
def test_the_flood_limits_are_keys_of_the_cli_range(ether, nodes, kind):
    d = target(nodes, ether, kind, "fm" + kind[:2])
    r = d.i.request("config.apply", {"set": {"flood.max.unscoped": 10, "flood.max.advert": 5}})
    assert r["status"] == "ok", r
    assert values(d, "flood.max.unscoped", "flood.max.advert") == {"flood.max.unscoped": 10, "flood.max.advert": 5}
    assert d.cli.command("get flood.max.unscoped") == "> 10" and d.cli.command("get flood.max.advert") == "> 5"
    for bad in ({"flood.max.unscoped": 65}, {"flood.max.advert": -1}, {"flood.max.advert": "5"}):
        assert d.i.request("config.apply", {"set": bad})["status"] == "invalid", bad
    assert d.provision()["status"] == "ok"
    r = d.i.request("config.apply", {"set": {"flood.max.advert": 7}})
    assert r["status"] == "refused", "a write, so the unsigned console keeps reads only once enrolled: %s" % r
    assert values(d, "flood.max.advert") == {"flood.max.advert": 5}


@pytest.mark.parametrize("kind", KINDS)
def test_board_get_is_a_read_of_what_the_board_can_tell(ether, nodes, kind):
    d = target(nodes, ether, kind, "bg" + kind[:2])
    res = shaped("board.get", d.i.request("board.get")["result"])
    assert res["bootloader"] is None and res["adc_multiplier"] is None, res
    assert res["power_management"] is False and res["power_source"] is None and res["boot_mv"] is None, res
    assert res["boot_reason"] == d.cli.command("get pwrmgt.bootreason")[2:], res
    assert res["powersaving"] is (d.cli.command("powersaving") == "on"), res
    assert d.provision()["status"] == "ok"
    assert d.i.request("board.get")["status"] == "ok", "a read: the unsigned console keeps it once enrolled"


@pytest.mark.parametrize("kind", KINDS)
def test_the_board_writes_take_the_cli_answer(ether, nodes, kind):
    d = target(nodes, ether, kind, "bw" + kind[:2])
    r = d.i.request("board.adc.set", {"multiplier": "1.5"})
    assert r["status"] == "failed" and r["error"] == "unsupported", r
    for bad in ("11", "1.2345", "-1", "x", ""):
        assert d.i.request("board.adc.set", {"multiplier": bad})["status"] == "invalid", bad
    assert d.i.request("board.adc.set", {"multiplier": 1.5})["status"] == "invalid", "a string, as the CLI prints it"
    r = d.i.request("power.saving.set", {"on": True})
    assert r["status"] == "ok" and shaped("power.saving.set", r["result"]) == {"on": True}, r
    assert d.i.request("board.get")["result"]["powersaving"] is True and d.cli.command("powersaving") == "on"
    r = d.i.request("power.saving.set", {"on": False})
    assert r["status"] == "ok" and r["result"] == {"on": False}, r
    assert d.i.request("board.get")["result"]["powersaving"] is False
    assert d.i.request("power.saving.set", {"on": "yes"})["status"] == "invalid"
    assert d.provision()["status"] == "ok"
    r = d.i.request("power.saving.set", {"on": False})
    assert r["status"] == "refused", "a write, refused to the unsigned console once enrolled: %s" % r
    r = d.send(wrap(d.node_key, "power.saving.set", {"on": False}, job=next_job(d)))
    assert r["status"] == "ok", r
