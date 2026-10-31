"""Issue #202: the reply to a config.apply that changes connectivity.mode must reach the caller before the restart that
the change brings. The case mesh's Reader hit: one apply of a broker profile, mqtt.enabled and connectivity.mode, about
nine keys, sent over USB in several frames, on the standalone Companion."""
import time

from sim.harness import MqttEnv, WifiEnv
from test_device_interface import client
from test_updates import RTC


def test_the_reply_to_a_long_apply_that_restarts_arrives_before_the_restart(nodes, tmp_path):
    wenv = WifiEnv(str(tmp_path / "cm.wifi"))
    wenv.set("sim")
    n = nodes("companion_standalone", "cm", wifi=wenv, mqtt=MqttEnv(str(tmp_path / "cm.mqtt")), seed=81, rtc=RTC).start()
    c, i = client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    args = {"set": {"mqtt.profile.0.type": "static", "mqtt.profile.0.host": "broker.example.test",
                    "mqtt.profile.0.port": 8883, "mqtt.profile.0.tls": True, "mqtt.profile.0.user": "gw-1",
                    "mqtt.profile.0.pwd": "not-a-real-password", "mqtt.profile.0.priority": 1,
                    "mqtt.enabled": True, "connectivity.mode": "wifi"}}
    t0 = time.time()
    r = i.request("config.apply", args, rid="su-2", timeout=10)
    took = time.time() - t0
    assert r is not None and r["status"] == "ok", r
    assert r["result"]["restarting"] is True, r
    assert n.wait_exit(10) == 75, "the restart follows the reply"
    print("reply after %.2f s" % took)


def test_the_reply_survives_a_slow_flash_after_the_mode_line(nodes, tmp_path):
    """On a Heltec V3 that apply took 5.6 s, and the restart fired 1.5 s after the connectivity.mode line however much of
    the apply was still to run. Here the mode comes first and every later write holds the node 300 ms, as a flash save
    does: the restart must still wait for the reply."""
    wenv = WifiEnv(str(tmp_path / "cs.wifi"))
    wenv.set("sim")
    n = nodes("companion_standalone", "cs", wifi=wenv, mqtt=MqttEnv(str(tmp_path / "cs.mqtt")), seed=82, rtc=RTC,
              extra=["--fs-write-ms", "300"]).start()
    c, i = client(n)
    assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
    keys = {"connectivity.mode": "wifi", "mqtt.profile.0.type": "static", "mqtt.profile.0.host": "broker.example.test",
            "mqtt.profile.0.port": 8883, "mqtt.profile.0.tls": True, "mqtt.profile.0.user": "gw-1",
            "mqtt.profile.0.pwd": "not-a-real-password", "mqtt.profile.0.priority": 1, "mqtt.enabled": True}
    t0 = time.time()
    r = i.request("config.apply", {"set": keys}, rid="slow-1", timeout=20)
    t_reply = time.time()
    assert r is not None and r["status"] == "ok" and r["result"]["restarting"] is True, r
    assert n.wait_exit(10) == 75
    t_exit = time.time()
    print("reply after %.2f s, restart %.2f s after the reply" % (t_reply - t0, t_exit - t_reply))
    assert t_exit - t_reply >= 1.2, "the restart's 1.5 s count from the reply"


def test_a_host_polling_faster_than_the_delay_does_not_postpone_the_restart(nodes, tmp_path):
    """Issue #226: mesh's Reader sent a hello every 0.3 s after the switch, waiting for the new epoch. Each reply moved
    the restart 1.5 s further out, and it came only once the Reader went away. Only the switch's own reply may."""
    wenv = WifiEnv(str(tmp_path / "cp.wifi"))
    wenv.set("sim")
    n = nodes("companion_standalone", "cp", wifi=wenv, mqtt=MqttEnv(str(tmp_path / "cp.mqtt")), seed=83, rtc=RTC).start()
    c, i = client(n)
    r = i.request("config.apply", {"set": {"connectivity.mode": "wifi"}}, rid="poll-1", timeout=10)
    t_reply = time.time()
    assert r is not None and r["status"] == "ok" and r["result"]["restarting"] is True, r
    k = 0
    while n.proc.poll() is None and time.time() - t_reply < 8:
        k += 1
        try:
            i.request("hello", {}, rid="h%d" % k, timeout=1)
        except Exception:
            break                                            # the node went away mid-request: the restart
        time.sleep(0.3)
    assert n.wait_exit(10) == 75
    took = time.time() - t_reply
    print("restart %.2f s after the reply, %d hellos in between" % (took, k))
    assert k >= 3, "the host did poll while the restart was pending"
    assert took < 3.5, "the restart came 1.5 s after the reply, however often the host asked"
