"""The Wi-Fi password is write-only: it can be set, but no CLI command reads it back."""
import os

from sim.harness import CompanionClient

PWD = "hunter2-sim-wifi-secret"


def _stored(node):
    for root, _, files in os.walk(node.dir):
        for f in files:
            if PWD.encode() in open(os.path.join(root, f), "rb").read():
                return True
    return False


def test_wifi_password_is_write_only(nodes):
    node = nodes("companion_wifi_usb", "pw1", seed=9, rtc=1760000000).start()
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    assert "updated" in c.cli("set wifi.pwd " + PWD)
    assert "wifi.ssid" in c.cli("set wifi.ssid homenet")     # the other settings still work
    reply = c.cli("get wifi.pwd")
    assert PWD not in reply and "hunter2" not in reply, reply
    assert _stored(node)                                      # set still stored it
    # no other read path leaks it either
    for q in ("get wifi.ssid", "get wifi.enabled", "get wifi.status", "get wifi.ip"):
        assert PWD not in c.cli(q)
