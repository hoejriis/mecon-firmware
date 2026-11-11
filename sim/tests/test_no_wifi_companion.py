"""A Companion on a board without Wi-Fi (the Wio Tracker L1 and T1000-E USB builds; sim_companion_iface_usb) has no
Wi-Fi setting and no connectivity mode. Its settings profile does not list them, and a request for one is `invalid`,
as for any key the device lacks (DEVICE_INTERFACE.md section 5). A hardware limit is stated, not offered (#485)."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from test_device_interface import RTC, assert_case, client, expand, fixture  # noqa: E402

WIFI = ("connectivity.mode", "wifi.enabled", "wifi.profile.{n}.ssid", "wifi.profile.{n}.pwd", "wifi.profile.{n}.priority")


def schema_keys(i):
    keys, cursor = [], 0
    while cursor is not None:
        r = i.request("config.schema", {"cursor": cursor})
        assert r["status"] == "ok", r
        keys += [k["key"] for k in r["result"]["keys"]]
        cursor = r["result"]["next"]
    return keys


def test_a_board_without_wifi_lists_no_wifi_key_and_refuses_one(nodes):
    n = nodes("companion_iface_usb", "nowifi", seed=11, rtc=RTC).start()
    c, i = client(n)
    keys = schema_keys(i)
    assert "name" in keys and "radio" in keys, keys
    assert not set(WIFI) & set(keys), "a board without Wi-Fi offers %s" % sorted(set(WIFI) & set(keys))
    r = i.request("config.apply", {"set": {"wifi.profile.3.priority": 5}})
    assert r["status"] == "invalid", r
    r = i.request("config.apply", {"set": {"connectivity.mode": "wifi"}})
    assert r["status"] == "invalid", r
    r = i.request("config.get", {"keys": ["wifi.profile.3.priority"]})
    assert r["status"] == "ok" and r["result"]["errors"] == {"wifi.profile.3.priority": "unknown-key"}, r


def test_every_applicable_request_fixture_and_the_wifi_less_twin(nodes):
    # #418: on this build `config apply, a key this board lacks` runs and gets `invalid`, the cases that need a Wi-Fi
    # key are skipped by the rule the board runner uses, and every other Companion case passes.
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
    import mecon_fixture
    n = nodes("companion_iface_usb", "nowifif", seed=11, rtc=RTC).start()
    c, i = client(n)
    keys = mecon_fixture.schema_keys(i.request)
    ran, skipped = [], []
    for case in fixture("requests.json")["cases"]:
        if "companion" not in case.get("roles", ["companion"]):
            continue
        if not mecon_fixture.applicable(case, keys)[0]:
            skipped.append(case["name"])
            continue
        if "raw" in case:
            i.send_raw(case["raw"].encode())
            answer = i.wait_result(case["expect"].get("id", ""))
        else:
            i.send_raw(expand(case["request"]))
            answer = i.wait_result(case["request"]["id"])
        assert_case(i, case, answer)
        ran.append(case["name"])
    assert "config apply, a key this board lacks" in ran, ran
    assert {"config apply", "config get"} <= set(skipped), skipped
