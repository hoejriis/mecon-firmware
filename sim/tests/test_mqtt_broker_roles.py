"""Issue #218: the Deployment's broker roles order the brokers a device tries, ahead of the profiles' own priorities.

A standalone Companion in Wi-Fi mode with two brokers: profile 0 has the higher priority, so with no roles it is the one
used. A provisioning names profile 1 the primary; from the next boot the device uses profile 1, and an unsigned priority
edit cannot lift profile 0 back above the signed role."""
from sim.harness import MqttEnv, WifiEnv
from test_companion_mqtt import PLAIN, _client, static, wait_status
from test_companion_mqtt import brokers, _tools  # noqa: F401  (fixtures)
from test_deployment_authority import Dev, provision_args
from test_updates import RTC


def boot_wifi(nodes, tmp_path, name, commands):
    wenv = WifiEnv(str(tmp_path / (name + ".wifi")))
    wenv.set("sim")
    n = nodes("companion_standalone", name, wifi=wenv, mqtt=MqttEnv(str(tmp_path / (name + ".mqtt"))), seed=31, rtc=RTC).start()
    c = _client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    for cmd in commands + ["set mqtt.enabled 1"]:
        assert c.cli(cmd).startswith(">"), cmd
    c.cli("set connectivity.mode wifi")   # restarts into Wi-Fi mode
    assert n.wait_exit(15) == 75
    n.start()
    return n


def test_a_provisioned_primary_is_the_broker_the_device_uses(nodes, tmp_path, brokers):
    a = brokers("a", "127.0.0.2")
    b = brokers("b", "127.0.0.3")
    n = boot_wifi(nodes, tmp_path, "roles", static(0, "127.0.0.2", PLAIN, 90) + static(1, "127.0.0.3", PLAIN, 10))
    d = Dev(n)
    s = wait_status(d.c, "profile=0")
    assert "broker=127.0.0.2:%d" % PLAIN in s, "with no roles the higher priority wins: " + s

    args = provision_args(d.node_key)
    args["deployment"]["broker_roles"] = [{"profile": 1, "role": "primary"}]
    r = d.i.request("deployment.provision", args)
    assert r["status"] == "ok" and r["result"]["broker_roles"] == [{"profile": 1, "role": "primary"}], r

    d.reboot()
    before = a.client_connects()
    s = wait_status(d.c, "profile=1")
    assert "broker=127.0.0.3:%d" % PLAIN in s, "the signed primary is used: " + s
    assert a.client_connects() == before, "the unroled broker was not tried first"

    # An unsigned priority edit on the device (an enrolled device may refuse it outright): it cannot outrank the role.
    d.c.cli("set mqtt.profile 0 priority 255")
    d.reboot()
    s = wait_status(d.c, "profile=1")
    assert "broker=127.0.0.3:%d" % PLAIN in s, s
