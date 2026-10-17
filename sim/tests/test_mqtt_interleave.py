"""The connect job and the main loop, interleaved on purpose (docs/MQTT.md, "The connect runs on a worker").

The cancellation rules of the worker are about *when* the main loop acts relative to the job: the job owns the transport
while it runs; a cancel is only a flag the job reads between its steps; the result is taken only once the job has ended.
Tests that rely on the timing of a real connect (a slow proxy, a black hole, a sleep) pass or fail with the speed of the
machine: on a slow x86 runner two mutants of these rules survived that died on the developer's machine.

These tests do not race. The simulated network can stop the job at a named step and hold it there until told to go on
(`MqttEnv.pause(step)` / `release()`, variants/sim/sim_mqtt_net.cpp). The test arms a pause, waits for the node's own
"paused <step>" line, acts on the main loop while the job is provably stopped, releases, and asserts. A mutant of any
of these rules therefore breaks the test whatever the speed of the machine.

Steps: dns (after the name was resolved), tcp (after the TCP connect), tls (after the handshake and pin check), connack
(before the first byte of the MQTT CONNECT is written).
"""
import time

import pytest

from sim import mqtt_broker
from sim.harness import CompanionClient, MqttEnv, WifiEnv, mqtt_events
from sim.mqtt_broker import Broker, make_cert
from sim.tests.test_companion_mqtt import PLAIN, TLS, RTC, static, wait_status, wait_until

STEPS = ["dns", "tcp", "tls", "connack"]
A_IP, B_IP, T_IP = "127.0.0.2", "127.0.0.3", "127.0.0.4"
KIND = "companion_wifi_usb_asan"   # also proves that no cancel path frees anything under the job or leaks it


@pytest.fixture(autouse=True)
def _tools():
    mqtt_broker.require_tools()


@pytest.fixture
def brokers(tmp_path):
    made = []

    def make(name, ip, tls):
        cert = key = None
        if tls:
            cert, key, _ = make_cert(str(tmp_path), name, ip)
        b = Broker(tmp_path, name, ip, TLS if tls else PLAIN, cert, key).start()
        made.append(b)
        return b

    yield make
    for b in made:
        b.stop()


class Rig:
    """A node whose profile 0 is the name mq-a.test (broker A); mq-b.test is broker B. The first attempt is paused at
    `step` before the node is rebooted into running the controller."""

    def __init__(self, nodes, tmp_path, step, tls, extra_cmds=()):
        self.step = step
        self.menv = MqttEnv(str(tmp_path / "il.mqtt"))
        self.menv.host("mq-a.test", A_IP)
        self.menv.host("mq-b.test", B_IP)
        wenv = WifiEnv(str(tmp_path / "il.wifi"))
        wenv.set("sim")
        self.n = nodes(KIND, "il", wifi=wenv, mqtt=self.menv, seed=3, rtc=RTC).start()
        c = self.c = CompanionClient(self.n)
        c.device_query()
        c.app_start()
        assert "ssid is now" in c.cli("set wifi.profile 0 ssid sim")
        port = TLS if tls else PLAIN
        for cmd in static(0, "mq-a.test", port, 90, tls=tls) + list(extra_cmds):
            assert c.cli(cmd).startswith(">"), cmd
        self.menv.pause(step)
        from sim.tests.test_mqtt_nonblocking import _reboot
        self.c = _reboot(self.n, c)
        wait_until(lambda: self.events().count("paused " + step) >= 1, "the job to stop at " + step, 40)

    def events(self):
        return mqtt_events(self.n)

    def settle(self):
        """The main loop has run, with the job stopped: the console is answered by it, so a few round trips and a short
        wait are enough for any reaction the code under test would have. Nothing is waited *for*: the assertion is that
        nothing happened."""
        for _ in range(3):
            self.c.cli("get mqtt.status")
        time.sleep(0.3)
        self.c.cli("get mqtt.status")

    def release(self):
        self.menv.release(self.step)
        wait_until(lambda: self.events().count("released " + self.step) >= 1, "the job to carry on", 20)


def opens_to(events, ip):
    return sum(1 for e in events if e.startswith("open addr=%s:" % ip))


def closes(events):
    return sum(1 for e in events if e == "close")


def opens(events):
    return sum(1 for e in events if e.startswith("open "))


@pytest.mark.parametrize("step", STEPS)
def test_the_result_is_not_taken_while_the_job_runs(nodes, tmp_path, brokers, step):
    tls = step == "tls"
    a = brokers("a", A_IP, tls)
    r = Rig(nodes, tmp_path, step, tls)
    before = r.events()
    r.settle()
    # the job is stopped mid-attempt: the controller is still connecting, and has not read a result the job did not write
    s = r.c.cli("get mqtt.status")
    assert "connecting" in s, s
    assert r.events() == before, r.events()
    assert a.client_connects() == 0
    r.release()
    s = wait_status(r.c, "connected", timeout=40)
    assert "broker=%s:" % A_IP in s, s
    assert a.client_connects() == 1
    assert opens(r.events()) - closes(r.events()) == 1, r.events()
    assert r.n.sanitizer_report() == "", r.n.sanitizer_report()


@pytest.mark.parametrize("step", STEPS)
def test_an_edit_while_the_job_owns_the_transport_does_not_touch_it(nodes, tmp_path, brokers, step):
    tls = step == "tls"
    a = brokers("a", A_IP, tls)
    b = brokers("b", B_IP, tls)
    r = Rig(nodes, tmp_path, step, tls)
    before = r.events()
    assert r.c.cli("set mqtt.profile 0 host mq-b.test").startswith(">")      # the profile being connected is edited
    r.settle()
    # the main loop has seen the edit; the job still owns the transport, so nothing was closed, opened or resolved
    assert r.events() == before, r.events()
    r.release()
    s = wait_status(r.c, "connected", timeout=40)
    assert "broker=%s:" % B_IP in s, s
    ev = r.events()
    assert opens_to(ev, A_IP) == (0 if step == "dns" else 1), ev
    assert a.client_connects() == (1 if step == "connack" else 0)           # a CONNECT already on its way is closed late
    assert b.client_connects() == 1
    assert opens(ev) - closes(ev) == 1, ev
    assert r.n.sanitizer_report() == "", r.n.sanitizer_report()


def _cause_edit(r):
    assert r.c.cli("set mqtt.profile 0 host mq-b.test").startswith(">")


def _cause_posture(r):
    assert "hardened" in r.c.cli("set posture hardened").lower()


def _cause_disable(r):
    assert r.c.cli("set mqtt.enabled 0").startswith(">")


CAUSES = {"edit": _cause_edit, "posture": _cause_posture, "disable": _cause_disable}
CASES = [(s, c) for c in CAUSES for s in STEPS if not (c == "posture" and s == "tls")]   # Hardened allows TLS


@pytest.mark.parametrize("step,cause", CASES, ids=["%s-%s" % (s, c) for s, c in CASES])
def test_a_cancel_between_the_steps_stops_the_job_at_the_next_check(nodes, tmp_path, brokers, step, cause):
    tls = step == "tls"
    a = brokers("a", A_IP, tls)
    b = brokers("b", B_IP, tls)
    t = None
    extra = ()
    if cause == "posture":   # profile 1: TLS without an anchor, the only one Hardened permits
        cert, key, _ = make_cert(str(tmp_path), "t", T_IP)
        t = Broker(tmp_path, "t", T_IP, TLS, cert, key).start()
        extra = static(1, T_IP, TLS, 50, tls=True)
    try:
        r = Rig(nodes, tmp_path, step, tls, extra_cmds=extra)
        before = r.events()
        CAUSES[cause](r)
        r.settle()
        assert r.events() == before, r.events()   # the job still owns the transport: the cause only raised the flag
        r.release()
        if cause == "disable":   # the job ends, the cancelled result is dropped, then the controller can start again
            wait_until(lambda: opens(r.events()) - closes(r.events()) == 0, "the cancelled attempt's transport to be closed", 20)
            assert r.c.cli("set mqtt.profile 0 host mq-b.test").startswith(">")
            wait_until(lambda: r.c.cli("get mqtt.status").strip() == "> off", "the controller to be off", 20)
            assert r.c.cli("set mqtt.enabled 1").startswith(">")
        s = wait_status(r.c, "connected", timeout=40)
        ev = r.events()
        if cause == "posture":
            assert "profile=1" in s, s
            assert t.client_connects() == 1
        else:
            assert "broker=%s:" % B_IP in s, s
            assert b.client_connects() == 1
        # the checks between the steps: cancelled after the name was resolved, the job opens nothing; cancelled after
        # the transport opened, it sends no CONNECT; at the last step the CONNECT is on its way and is closed late
        assert opens_to(ev, A_IP) == (0 if step == "dns" else 1), ev
        assert a.client_connects() == (1 if step == "connack" else 0), a.log()
        assert a.retained_status(2) is None
        assert opens(ev) - closes(ev) == 1, ev
        assert r.n.sanitizer_report() == "", r.n.sanitizer_report()
    finally:
        if t:
            t.stop()
