"""Remote Companion CLI (TXT_TYPE_CLI_COMMAND from a contact flagged for remote CLI): reads and
`reboot` only. Settings changes, poweroff and secrets are refused with an explicit reply. The local
CLI (CMD_RUN_CLI_COMMAND) is unchanged."""
import pytest

from sim.harness import CompanionClient

RTC = 1760000000
REMOTE_CLI_FLAG = 0x10
REFUSED = "not permitted remotely"


def _pair(ether, nodes, prefix):
    victim = nodes("companion_wifi_usb", prefix + "v", seed=31, rtc=RTC, ether_path=ether.path).start()
    remote = nodes("companion", prefix + "r", seed=32, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes([prefix + "v", prefix + "r"])
    ether.set_link(prefix + "v", prefix + "r")
    v, r = CompanionClient(victim), CompanionClient(remote)
    for c in (v, r):
        c.device_query()
        c.app_start()
    assert v.add_contact(r.pub_key, "remote", flags=REMOTE_CLI_FLAG)[0] == 0   # the victim allows remote CLI from it
    assert r.add_contact(v.pub_key, "victim")[0] == 0
    return victim, remote, v, r


def remote(r, v, text):
    assert r.send_cli_command(v.pub_key, text)[0] == 6      # RESP_SENT
    msgs = r.sync_messages(timeout=60, want=1)
    assert msgs, "no reply to remote command %r" % text
    return msgs[0]["text"]


def test_remote_reads_work_and_writes_are_refused(ether, nodes):
    victim, _, v, r = _pair(ether, nodes, "a")
    assert "v1." in remote(r, v, "ver")                       # allowed: read
    name = remote(r, v, "get name")
    assert name.startswith("> ") and "wifi" not in name

    for cmd in ("set name Hacked", "set pin 654321", "set wifi.pwd stolen", "set radio 433.0,125,7,5",
                "set tx 1", "set wifi.clear"):
        assert REFUSED in remote(r, v, cmd), cmd

    # nothing was applied: read the state back locally over the cable class
    assert v.cli("get name") == name
    assert v.cli("get radio").startswith("> 869.6")
    assert v.cli("get wifi.ssid").endswith("(not set)")
    # local behaviour unchanged: the same settings commands work locally
    assert v.cli("set name Local") == "OK"
    assert v.cli("get name") == "> Local"
    assert victim.proc.poll() is None


def test_remote_poweroff_refused(ether, nodes):
    victim, _, v, r = _pair(ether, nodes, "b")
    assert REFUSED in remote(r, v, "poweroff")
    assert REFUSED in remote(r, v, "shutdown")
    assert victim.proc.poll() is None
    assert v.cli("get name").startswith("> ")                 # still serving


@pytest.mark.timescale(10)
def test_remote_reboot_allowed(ether, nodes):
    victim, _, v, r = _pair(ether, nodes, "c")
    assert r.send_cli_command(v.pub_key, "reboot")[0] == 6
    assert victim.wait_exit(30) == 75
