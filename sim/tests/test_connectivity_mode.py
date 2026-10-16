"""Connectivity-mode switching and the connectivity status screen on the standalone Companion (docs/CONNECTIVITY_MODE.md).

The node is `companion_standalone`: the real Companion application and the real ui-new screens, BLE or Wi-Fi (with
MQTT) one at a time, USB always. What is simulated and what is not:

* The display records every distinct frame as text and the button replays gestures from a file, so the tests assert
  what the firmware drew and drive the device the way a finger would.
* BLE is a stub: it logs when its stack would be brought up (`SIMBLE begin`), classifies its transport like the
  real class and can be told a phone is connected. There is no radio, GATT or pairing: nothing here verifies BLE
  itself, only the mode logic and the screens around it. The TCP side of the Wi-Fi mode is inert in this build
  (as in `sim_companion_wifi_usb`); the Wi-Fi controller and the MQTT controller are the real ones.
* Time runs 10 times faster than real time. The splash lasts 3 s, a long press in the first 8 s of the UI goes
  to the rescue console (stock), an armed switch times out after 10 s and the restart follows a switch by 1.5 s.
"""
import os
import re
import time

import pytest

from sim import harness, mqtt_broker
from sim.harness import CompanionClient, WifiEnv, MqttEnv, audit_entries, files_containing, node_log, wifi_events
from sim.mqtt_broker import Broker, make_cert

RTC = 1760000000
pytestmark = pytest.mark.timescale(10)

WIFI_PW = "Wp-s3cret-AAAA"
WIFI_PW2 = "Wp-s3cret-A2A2"
MQTT_PW = "Mq-s3cret-BBBB"
MQTT_USER = "mqUser-CCCC"


# ------------------------------------------------------------------ helpers

def _client(node):
    c = CompanionClient(node)
    c.device_query()
    c.app_start()
    return c


def start(nodes, name, seed=7, wifi=None, mqtt=None, **kw):
    n = nodes("companion_standalone", name, seed=seed, rtc=RTC, wifi=wifi, mqtt=mqtt, **kw).start()
    return n, _client(n)


def restart_after_switch(node):
    """The device restarts by itself shortly after a switch (exit status 75); bring it back."""
    assert node.wait_exit(10) == 75
    node.start()
    return _client(node)


def home_settled(node):
    """The home screen is up and the first 8 s of the UI (where a long press goes to the rescue console) are over."""
    assert node.ui.wait_text("Unread Msg", 20), node.ui.last_text()
    time.sleep(1.0)


def goto_connectivity(node):
    """Click through the home pages to the connectivity status screen; returns the number of clicks."""
    for clicks in range(12):
        text = node.ui.last_text()
        if "Mode:" in text:
            return clicks
        node.ui.press("click")
        end = time.time() + 15   # wait for the page to change, not merely for a redraw of the same page
        while time.time() < end and node.ui.last_text() == text:
            time.sleep(0.05)
    try:
        inp = open(node.ui.input_path).read()
    except OSError as e:
        inp = repr(e)
    try:
        log = open(node.log_path, errors="replace").read()[-3000:]
    except OSError as e:
        log = repr(e)
    raise AssertionError("never reached the connectivity screen: " + node.ui.last_text()
                         + "\n--- frames this boot: %d\n--- input file:\n%s\n--- node log tail:\n%s" % (len(node.ui.frames()), inp, log))


def log_count(node, text):
    return node_log(node).count(text)


def audit(c):
    return audit_entries(c.cli)


def wifi_profile(c, i, ssid, pwd=None):
    assert "ssid is now" in c.cli("set wifi.profile %d ssid %s" % (i, ssid))
    if pwd:
        assert "pwd" in c.cli("set wifi.profile %d pwd %s" % (i, pwd))


def wait_until(fn, what, timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + what)


@pytest.fixture
def wenv(tmp_path):
    e = WifiEnv(str(tmp_path / "w.wifi"))
    return e


# ------------------------------------------------------------------ the mode is persisted and starts the right stack

def test_the_default_mode_is_ble_and_only_the_ble_stack_starts(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "d1", wifi=wenv)
    assert c.cli("get connectivity.mode") == "> ble"
    wifi_profile(c, 0, "Home", WIFI_PW)
    c.cli("set wifi.enabled 1")
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"reboot")           # a Wi-Fi profile exists, the mode is still BLE
    assert n.wait_exit(10) == 75
    n.start()
    c = _client(n)
    time.sleep(1.0)
    assert c.cli("get connectivity.mode") == "> ble"
    assert log_count(n, "SIMBLE begin") == 2                              # one per boot
    assert wifi_events(n) == []                                           # the Wi-Fi stack never ran
    assert c.cli("get wifi.status") == "> off"


def test_wifi_mode_starts_only_the_wifi_stack_and_persists(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "w1", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    assert c.cli("set connectivity.mode wifi") == "OK - connectivity.mode wifi, restarting"
    c = restart_after_switch(n)
    assert c.cli("get connectivity.mode") == "> wifi"
    wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link")
    assert ("up", "Home") in wifi_events(n)
    assert log_count(n, "SIMBLE begin") == 1                              # only the first boot (BLE mode) started BLE
    # a power cycle keeps the mode
    n.restart()
    c = _client(n)
    assert c.cli("get connectivity.mode") == "> wifi"
    wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link after a power cycle")
    assert log_count(n, "SIMBLE begin") == 1
    assert "conn_mode:1" in open(os.path.join(n.dir, "prefs.json")).read().replace(" ", "")


def test_switching_back_and_forth_never_brings_up_both_stacks(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "bf", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    for want, bles, joined in (("wifi", 1, True), ("ble", 2, True), ("wifi", 2, True)):
        base_joins = len([e for e in wifi_events(n) if e[0] == "join"])
        assert c.cli("set connectivity.mode " + want).startswith("OK - connectivity.mode " + want)
        c = restart_after_switch(n)
        assert c.cli("get connectivity.mode") == "> " + want
        time.sleep(1.0)
        assert log_count(n, "SIMBLE begin") == bles, (want, node_log(n))
        joins = len([e for e in wifi_events(n) if e[0] == "join"])
        assert (joins > base_joins) == (want == "wifi"), (want, wifi_events(n))   # the radio ran only in Wi-Fi mode


def test_a_damaged_mode_byte_starts_ble(nodes):
    n = nodes("companion_standalone", "dm", seed=7, rtc=RTC)
    with open(os.path.join(n.dir, "prefs.json"), "w") as f:
        f.write("{comp:{conn_mode:7}}")        # neither 0 nor 1 (the prefs format has unquoted keys)
    n.start()
    c = _client(n)
    assert c.cli("get connectivity.mode") == "> ble"
    assert log_count(n, "SIMBLE begin") == 1


@pytest.mark.parametrize("cut", ["tmp1", "tmp40", "before_rename"])
def test_a_power_cut_while_switching_keeps_the_previous_complete_prefs(nodes, cut):
    n, c = start(nodes, "cut" + cut)
    assert c.cli("set name Keeper") == "OK"
    n.stop()
    if cut.startswith("tmp"):
        n.fault = "/prefs.json.tmp:%s" % cut[3:]
    else:
        n.fault_before = "rename:/prefs.json.tmp"
    n.start()
    c = _client(n)
    c._send(bytes([harness.CMD_RUN_CLI_COMMAND]) + b"set connectivity.mode wifi")
    assert n.wait_exit(10) == 99               # the power went during the save
    n.fault = n.fault_before = None
    n.start()
    c = _client(n)
    assert c.cli("get connectivity.mode") == "> ble"       # the old mode, not a half-written new one
    assert c.cli("get name") == "> Keeper"                  # and the rest of the prefs survived
    assert c.cli("set connectivity.mode wifi").startswith("OK")   # a clean switch afterwards works
    c = restart_after_switch(n)
    assert c.cli("get connectivity.mode") == "> wifi"


# ------------------------------------------------------------------ USB management in both modes and across a switch

def test_usb_management_works_in_both_modes_and_across_a_switch(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "usb", wifi=wenv)
    key = c.export_private_key()
    assert c.cli("set name Alpha") == "OK"
    assert c.cli("get name") == "> Alpha"
    assert c.cli("get posture") == "> normal"
    wifi_profile(c, 0, "Home", WIFI_PW)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)                                   # the same cable, the other mode
    assert c.cli("get name") == "> Alpha"                         # state survived the switch
    assert c.cli("get connectivity.mode") == "> wifi"
    assert c.cli("set name Beta") == "OK"
    assert c.cli("get name") == "> Beta"
    wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link")
    assert c.cli("get wifi.status").startswith("> connected")     # the Wi-Fi CLI answers over the cable too
    assert c.export_private_key() == key                          # a cable command only a trusted transport may run
    assert c.app_start()[4:36] == c.pub_key
    assert c.cli("set connectivity.mode ble").startswith("OK")
    c = restart_after_switch(n)
    assert c.cli("get name") == "> Beta"
    assert c.cli("set name Gamma") == "OK"


# ------------------------------------------------------------------ who may switch

def _pair(ether, nodes):
    victim = nodes("companion_standalone", "rv", seed=31, rtc=RTC, ether_path=ether.path).start()
    remote = nodes("companion", "rr", seed=32, rtc=RTC, ether_path=ether.path).start()
    assert ether.wait_for_nodes(["rv", "rr"])
    ether.set_link("rv", "rr")
    v, r = _client(victim), _client(remote)
    assert v.add_contact(r.pub_key, "remote", flags=0x10)[0] == 0      # the victim allows remote CLI from it
    assert r.add_contact(v.pub_key, "victim")[0] == 0
    return victim, v, r


def _remote(r, v, text):
    assert r.send_cli_command(v.pub_key, text)[0] == 6
    msgs = r.sync_messages(timeout=60, want=1)
    assert msgs, "no reply to remote command %r" % text
    return msgs[0]["text"]


def test_a_remote_switch_is_refused_and_audited(ether, nodes):
    victim, v, r = _pair(ether, nodes)
    assert _remote(r, v, "get connectivity.mode") == "> ble"            # a read is fine
    assert "not permitted" in _remote(r, v, "set connectivity.mode wifi")
    assert "not permitted" in _remote(r, v, "set connectivity.mode wifi")   # repeats fold into one audit entry
    time.sleep(2.0)                                                     # long past the 1.5 s restart delay
    assert victim.proc.poll() is None                                   # the device did not restart
    assert v.cli("get connectivity.mode") == "> ble"
    prefs = os.path.join(victim.dir, "prefs.json")
    assert not os.path.exists(prefs) or "conn_mode:1" not in open(prefs).read().replace(" ", "")   # nothing was stored
    log = audit(v)
    assert any(ln.endswith("refused remote-cli connectivity-mode x2") for ln in log), log
    assert not any(" mode " in ln for ln in log), log                  # and no mode change was recorded
    assert v.cli("set connectivity.mode wifi").startswith("OK")       # positive control: the cable may


def test_the_cable_switch_is_audited(nodes):
    n, c = start(nodes, "au")
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    log = audit(c)
    assert any(ln.endswith("mode cable wifi x1") for ln in log), log


def test_bad_input_changes_nothing(nodes):
    n, c = start(nodes, "bi")
    for cmd in ("set connectivity.mode", "set connectivity.mode both", "set connectivity.mode WIFI"):
        assert c.cli(cmd) == "ERR: set connectivity.mode ble | wifi", cmd
    assert c.cli("set connectivity.mode ble") == "OK - already ble"
    time.sleep(1.0)
    assert n.proc.poll() is None
    assert audit(c) == [] or all("mode" not in ln for ln in audit(c)[1:])


# ------------------------------------------------------------------ the button gesture

def test_the_button_switches_only_after_a_confirming_press(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "bt", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    home_settled(n)
    assert goto_connectivity(n) == 4                                      # click through the BLE-mode home pages
    t = n.ui.last_text()
    assert "to Wi-Fi: long press" in t, t
    n.ui.press("long")                                                    # arms; nothing has changed yet
    t = n.ui.wait_text("Switch to Wi-Fi?")
    assert t and "yes: long press" in t and "no: click" in t, t
    time.sleep(0.4)                                                       # 4 s of device time: still armed
    assert c.cli("get connectivity.mode") == "> ble" and n.proc.poll() is None
    assert audit(c) == [] or all(" mode " not in ln for ln in audit(c))
    n.ui.press("long")                                                    # the confirming press
    assert n.ui.wait_text("Restarting..."), n.ui.last_text()
    c = restart_after_switch(n)
    assert c.cli("get connectivity.mode") == "> wifi"
    assert any(ln.endswith("mode button wifi x1") for ln in audit(c)), audit(c)
    assert log_count(n, "SIMBLE begin") == 1
    wait_until(lambda: ("up", "Home") in wifi_events(n), "the Wi-Fi link in the new mode")


@pytest.mark.parametrize("cancel", ["click", "double"])
def test_a_cancelled_confirmation_does_nothing(nodes, cancel):
    n, c = start(nodes, "ca" + cancel)
    home_settled(n)
    goto_connectivity(n)
    n.ui.press("long")
    assert n.ui.wait_text("Switch to Wi-Fi?")
    n.ui.press(cancel)
    t = n.ui.wait_gone("Switch to Wi-Fi?")
    assert t is not None and "Mode: BLE" in n.ui.wait_text("Mode: BLE"), n.ui.last_text()
    time.sleep(2.5)                                                       # well past the 1.5 s restart delay
    assert n.proc.poll() is None
    assert c.cli("get connectivity.mode") == "> ble"
    assert all(" mode " not in ln for ln in audit(c)), audit(c)
    # a press after the cancel arms afresh: one long press is not a confirmation
    n.ui.press("long")
    assert n.ui.wait_text("Switch to Wi-Fi?")
    time.sleep(1.0)
    assert n.proc.poll() is None and c.cli("get connectivity.mode") == "> ble"


def test_an_unconfirmed_switch_times_out_and_changes_nothing(nodes):
    n, c = start(nodes, "to")
    home_settled(n)
    goto_connectivity(n)
    n.ui.press("long")
    assert n.ui.wait_text("Switch to Wi-Fi?")
    assert n.ui.wait_gone("Switch to Wi-Fi?", timeout=10)                 # 10 s of device time, 1 s of test time
    time.sleep(2.5)
    assert n.proc.poll() is None
    assert c.cli("get connectivity.mode") == "> ble"
    # a long press now arms again instead of confirming the stale prompt
    n.ui.press("long")
    assert n.ui.wait_text("Switch to Wi-Fi?")
    time.sleep(1.0)
    assert n.proc.poll() is None


def test_the_button_switches_back_and_works_in_hardened(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "hb", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    assert c.cli("set posture hardened") == "OK - posture hardened"
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    home_settled(n)
    assert goto_connectivity(n) == 3                                      # Wi-Fi mode: the Bluetooth page is skipped
    t = n.ui.last_text()
    assert "Mode: Wi-Fi, Hardened" in t and "to BLE: long press" in t, t
    n.ui.press("long")
    assert n.ui.wait_text("Switch to BLE?")
    n.ui.press("long")
    c = restart_after_switch(n)
    assert c.cli("get connectivity.mode") == "> ble"
    assert c.cli("get posture") == "> hardened"                           # the button did not touch the posture
    assert any(ln.endswith("mode button ble x1") for ln in audit(c)), audit(c)


# ------------------------------------------------------------------ the status screen and the home status line

def test_ble_mode_screen_and_home_line(nodes):
    # (the stock home screen shows the pairing PIN while no host is connected; the simulated USB port always
    # reports a host, so that line is not reachable here and is not asserted)
    n, c = start(nodes, "sb")
    home_settled(n)
    home = n.ui.last_text()
    lines = home.splitlines()
    assert "BLE" in lines and not any("Wi-Fi" in ln for ln in lines), home      # the status line
    goto_connectivity(n)
    t = n.ui.last_text()
    assert "Mode: BLE, Normal" in t and "Pin: per-device" in t and "Phone: not connected" in t, t
    assert "Wi-Fi, MQTT: off" in t and "DEFAULT" not in t, t
    assert not re.search(r"\d{6}", t), t                                        # the status screen never draws PIN digits
    n.ui.phone(True)
    t = n.ui.wait_text("Phone: connected")
    assert t, n.ui.last_text()
    n.ui.phone(False)
    assert n.ui.wait_text("Phone: not connected")


def test_a_default_pin_is_a_warning_not_a_credential(nodes):
    n, c = start(nodes, "dp")
    assert c.cli("set pin 123456").startswith("> pin is now")
    n.restart()
    c = _client(n)
    home_settled(n)
    home = n.ui.last_text()
    assert "BLE (default PIN!)" in home, home
    goto_connectivity(n)
    t = n.ui.last_text()
    assert "Pin: DEFAULT (unsafe)" in t and "per-device" not in t, t
    assert "Mode: BLE, Normal" in t, t


def test_the_screen_follows_the_posture(nodes):
    n, c = start(nodes, "po")
    home_settled(n)
    goto_connectivity(n)
    assert "Mode: BLE, Normal" in n.ui.last_text()
    assert c.cli("set posture hardened") == "OK - posture hardened"
    assert n.ui.wait_text("Mode: BLE, Hardened", 8), n.ui.last_text()
    assert c.cli("set posture normal confirm") == "OK - posture normal"
    assert n.ui.wait_text("Mode: BLE, Normal", 8), n.ui.last_text()


def test_wifi_mode_screen_shows_profile_state_rssi_and_ip(nodes, wenv):
    wenv.set("Home", WIFI_PW, rssi=-61)
    n, c = start(nodes, "sw", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    home_settled(n)
    wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link")
    t = n.ui.wait_text("Wi-Fi 192.0.2.10")                                       # the home status line
    assert t and "Pin:" not in t and "BLE" not in t, t                           # no BLE, so no pairing PIN to show
    goto_connectivity(n)
    t = n.ui.wait_text("Wi-Fi p0 Home -61dBm")
    assert t, n.ui.last_text()
    assert "Mode: Wi-Fi, Normal" in t and "IP: 192.0.2.10" in t and "MQTT: no profile" in t, t
    # the link goes away: the screen says so
    wenv.down("Home")
    t = wait_until(lambda: (lambda x: x if "-61dBm" not in x and "Wi-Fi" in x and "IP: -" in x else None)(n.ui.last_text()),
                   "the screen to notice the lost link", 40)
    assert "IP: 192.0.2.10" not in t, t


def test_wifi_mode_shows_no_pairing_pin(nodes):
    n = nodes("companion_standalone", "np2", seed=8, rtc=RTC)
    with open(os.path.join(n.dir, "prefs.json"), "w") as f:
        f.write("{comp:{conn_mode:1}}")
    n.start()
    home_settled(n)
    assert "Wi-Fi" in n.ui.last_text() and "Pin:" not in n.ui.last_text(), n.ui.last_text()


def test_wifi_mode_without_a_profile_says_so(nodes):
    n, c = start(nodes, "np")
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    home_settled(n)
    assert "Wi-Fi: no profile" in n.ui.last_text(), n.ui.last_text()
    goto_connectivity(n)
    t = n.ui.last_text()
    assert "Wi-Fi: no profile" in t and "IP: -" in t, t
    assert c.cli("get connectivity.mode") == "> wifi"          # USB works with no Wi-Fi at all


def test_hardened_wifi_mode_without_an_mqtt_profile_shows_wifi_off(nodes, wenv):
    wenv.set("Home", WIFI_PW)
    n, c = start(nodes, "hw", wifi=wenv)
    wifi_profile(c, 0, "Home", WIFI_PW)
    assert c.cli("set posture hardened").startswith("OK")
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    home_settled(n)
    goto_connectivity(n)
    t = n.ui.last_text()
    assert "Mode: Wi-Fi, Hardened" in t and "Wi-Fi: off" in t, t
    assert wifi_events(n) == []                                # Hardened starts Wi-Fi only for MQTT over TLS


# ------------------------------------------------------------------ MQTT on the screen (real Mosquitto)

@pytest.fixture
def brokers(tmp_path):
    mqtt_broker.require_tools()
    made = []

    def make(name, ip, port=18830, tls=False, users=None):
        cert = key = fp = None
        if tls:
            cert, key, fp = make_cert(str(tmp_path), name, ip)
        b = Broker(tmp_path, name, ip, port, cert, key, users).start()
        b.fingerprint = fp
        made.append(b)
        return b

    yield make
    for b in made:
        b.stop()


def mqtt_profile(c, i, host, port, tls=False, anchor=None, user=None, pwd=None):
    cmds = ["set mqtt.profile %d type static" % i, "set mqtt.profile %d host %s" % (i, host),
            "set mqtt.profile %d port %d" % (i, port), "set mqtt.profile %d tls %s" % (i, "on" if tls else "off")]
    if anchor:
        cmds.append("set mqtt.profile %d anchor %s" % (i, anchor))
    if user:
        cmds.append("set mqtt.profile %d user %s" % (i, user))
    if pwd:
        cmds.append("set mqtt.profile %d pwd %s" % (i, pwd))
    for cmd in cmds:
        assert c.cli(cmd).startswith(">"), cmd


def boot_wifi_mode_with_mqtt(nodes, tmp_path, name, profile_fn, seed=9):
    w = WifiEnv(str(tmp_path / (name + ".wifi")))
    w.set("Home", WIFI_PW)
    m = MqttEnv(str(tmp_path / (name + ".mqtt")))
    n, c = start(nodes, name, seed=seed, wifi=w, mqtt=m)
    wifi_profile(c, 0, "Home", WIFI_PW)
    profile_fn(c)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    home_settled(n)
    goto_connectivity(n)
    return n, c


def test_mqtt_line_shows_a_plain_session(nodes, tmp_path, brokers):
    brokers("a", "127.0.0.2")
    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "mp", lambda c: mqtt_profile(c, 0, "127.0.0.2", 18830))
    t = n.ui.wait_text("MQTT0 plain", 40)
    assert t, n.ui.last_text()
    assert "Wi-Fi p0 Home" in t, t


def test_mqtt_line_says_whether_tls_is_verified(nodes, tmp_path, brokers):
    b = brokers("s", "127.0.0.2", port=18883, tls=True)
    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "mv",
                                    lambda c: mqtt_profile(c, 0, "127.0.0.2", 18883, tls=True, anchor=b.fingerprint))
    t = n.ui.wait_text("MQTT0 TLS verified", 40)
    assert t, n.ui.last_text()
    # the same broker without a pinned anchor: encrypted, and the screen must not call it verified
    n2, c2 = boot_wifi_mode_with_mqtt(nodes, tmp_path, "mu",
                                      lambda c: mqtt_profile(c, 0, "127.0.0.2", 18883, tls=True), seed=10)
    t = n2.ui.wait_text("MQTT0 TLS UNVERIFIED", 40)
    assert t, n2.ui.last_text()
    assert "verified" not in t.replace("UNVERIFIED", ""), t


def test_mqtt_line_shows_the_state_when_there_is_no_session(nodes, tmp_path):
    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "mn", lambda c: mqtt_profile(c, 0, "127.0.0.9", 18899))
    t = wait_until(lambda: (lambda x: x if re.search(r"MQTT: (connecting|backoff|idle)", x) else None)(n.ui.last_text()),
                   "an MQTT state", 40)
    assert "MQTT0" not in t, t


# ------------------------------------------------------------------ no secret on any screen

def test_no_secret_is_ever_drawn(nodes, tmp_path, brokers):
    """Wi-Fi passwords and MQTT credentials are set, both modes are run, every home page is visited and the
    confirmation is armed; none of them appears in any frame of any boot. The pairing PIN is not asserted on: the
    stock home screen shows it on the device's own display, which is how the user pairs."""
    b = brokers("bsec", "127.0.0.2", users={MQTT_USER: MQTT_PW})
    w = WifiEnv(str(tmp_path / "sec.wifi"))
    w.set("Home", WIFI_PW)
    w.set("Other", WIFI_PW2)
    m = MqttEnv(str(tmp_path / "sec.mqtt"))
    n, c = start(nodes, "sec", wifi=w, mqtt=m)
    wifi_profile(c, 0, "Home", WIFI_PW)
    wifi_profile(c, 1, "Other", WIFI_PW2)
    mqtt_profile(c, 0, "127.0.0.2", 18830, user=MQTT_USER, pwd=MQTT_PW)

    def tour():
        home_settled(n)
        for _ in range(12):                                   # every home page, wrapping
            before = len(n.ui.frames())
            n.ui.press("click")
            time.sleep(0.15)
        goto_connectivity(n)
        n.ui.press("long")                                    # the confirmation screen
        assert n.ui.wait_text("Switch to")
        n.ui.press("click")                                   # cancelled
        time.sleep(0.3)

    tour()                                                    # BLE mode
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = restart_after_switch(n)
    tour()                                                    # Wi-Fi mode, MQTT joining and then up
    assert n.ui.wait_text("MQTT0 plain", 40) or "MQTT" in n.ui.last_text()
    assert c.cli("set posture hardened").startswith("OK")     # a posture change redraws; still no secret
    time.sleep(0.5)
    shown = n.ui.all_text()
    assert shown.count("Mode:") > 4, "the tour drew too little to prove anything"
    assert "s3cret" not in shown       # a long line is cut with "...": a leaked password may show only as a prefix
    for secret in (WIFI_PW, WIFI_PW2, MQTT_PW, MQTT_USER):
        assert secret not in shown, secret
        assert files_containing(n, secret) == [os.path.join(n.dir, "prefs.json")], secret
        assert secret not in open(n.ui.frames_path, errors="replace").read(), secret
