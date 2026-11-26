"""Recovery from failed broker connects under a low heap (src/helpers/mqtt/MqttRecovery.h, docs/MQTT.md; Issue #651), end to
end on the simulated Companion: the decision rule is the native test_mqtt_recovery, this checks that the controller feeds
it from the real connect path and that the restart goes through the board with its audit entry kept.

The broker is an address nothing listens on, so every attempt fails fast and the profile cools down 15 s, 30 s, 60 s, 120 s
of node time between attempts; the node runs 40 times faster than real time. The simulator's heap is set by the test.
Not verified here: a real allocator, a real TLS handshake, or the restart on a board.
"""
import time

import pytest

from sim import harness
from sim.harness import CompanionClient, WifiEnv, MqttEnv, audit_entries

RTC = 1760000000
HOUR_MS = 3600000
FLOOR = 20480
pytestmark = pytest.mark.timescale(40)


def _client(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def boot(nodes, tmp_path, name, extra=None, low=True):
    """A node whose only broker profile points at an address nothing listens on, rebooted so both controllers run."""
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    menv = MqttEnv(str(tmp_path / (name + ".mqtt")))
    n = nodes("companion_wifi_usb", name, wifi=wenv, mqtt=menv, seed=3, rtc=RTC, extra=extra or []).start()
    c = _client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    for cmd in ("set mqtt.profile 0 type static", "set mqtt.profile 0 host 127.0.0.9", "set mqtt.profile 0 port 18999",
                "set mqtt.profile 0 tls off", "set mqtt.profile 0 priority 90"):
        assert c.cli(cmd).startswith(">"), cmd
    if low:
        n.heap(300000, FLOOR - 1000)   # the largest block under the floor, the total still large
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")
    assert n.wait_exit(10) == 75
    n.start()
    return n, _client(n)


def test_failed_connects_under_a_low_heap_restart_the_device_and_the_audit_entry_survives(nodes, tmp_path):
    n, c = boot(nodes, tmp_path, "rec", extra=["--millis-start", str(HOUR_MS)])
    assert n.wait_exit(40) == 75, "five failed connects under a low heap never restarted the device"
    n.start()
    c = _client(n)
    entries = [e for e in audit_entries(c.cli) if " low-mem " in e and " mqtt-recover " in e]
    assert len(entries) == 1 and " system " in entries[0], audit_entries(c.cli)


def test_failed_connects_with_a_healthy_heap_never_restart_the_device(nodes, tmp_path):
    n, c = boot(nodes, tmp_path, "rech", extra=["--millis-start", str(HOUR_MS)], low=False)
    time.sleep(15)   # 10 minutes of node time: all five attempts and more
    assert n.proc.poll() is None, "a broker that is only down restarted a device with a healthy heap"
    assert not [e for e in audit_entries(c.cli) if " mqtt-recover " in e]


def test_no_restart_in_the_first_hour_after_boot(nodes, tmp_path):
    n, c = boot(nodes, tmp_path, "recf")   # millis() starts at zero: the hour has not passed
    time.sleep(15)
    assert n.proc.poll() is None, "the once-an-hour cap did not hold in the first hour"
    assert not [e for e in audit_entries(c.cli) if " mqtt-recover " in e]

