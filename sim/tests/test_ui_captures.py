"""Captures of the standalone Companion's screens, for docs/UI.md.

Each state is driven on the simulated board the way a finger would (button gestures, CLI over the simulated USB
port), the frame the real ui-new code drew is read from the recording display and rendered as a character grid
(sim/ui_render.py), and the grid is compared with the committed file in docs/ui/.

    sim/run.sh python3 -m pytest sim/tests/test_ui_captures.py -q            # fails if a screen drifted from docs/ui
    UI_CAPTURE_UPDATE=1 sim/run.sh python3 -m pytest sim/tests/test_ui_captures.py -q   # rewrites docs/ui/*.txt

These are renderings of simulator output, not photographs; BLE is a stub (see docs/SIMULATION.md).
"""
import os
import time

import pytest

from sim import mqtt_broker
from sim.harness import MqttEnv, WifiEnv
from sim.mqtt_broker import Broker, make_cert
from sim.ui_render import render
import test_connectivity_mode as t

pytestmark = pytest.mark.timescale(10)

DOCS_UI = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "docs", "ui")
UPDATE = os.environ.get("UI_CAPTURE_UPDATE") == "1"
WIFI_PW = "ExamplePassword-1"   # made-up
MQTT_PW = "ExampleMqttPass-2"
MQTT_USER = "exampleuser"


def emit(name, title, frame, alert=None):
    """Write (UPDATE) or check the capture docs/ui/<name>.txt."""
    text = "\n".join(render(frame, alert=alert, title=title)) + "\n"
    path = os.path.join(DOCS_UI, name + ".txt")
    if UPDATE:
        os.makedirs(DOCS_UI, exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        return
    assert os.path.exists(path), "missing capture %s; run with UI_CAPTURE_UPDATE=1" % path
    assert open(path).read() == text, "%s drifted from the firmware's output; rerun with UI_CAPTURE_UPDATE=1" % name


def start(nodes, name, **kw):
    """A standalone Companion with a fixed, obviously made-up node name (the default is derived from its key)."""
    n, c = t.start(nodes, name, **kw)
    assert c.cli("set name ExampleNode") == "OK"
    return n, c


def emit_text(name, title, line):
    text = "# " + title + "\n" + line + "\n"
    path = os.path.join(DOCS_UI, name + ".txt")
    if UPDATE:
        os.makedirs(DOCS_UI, exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        return
    assert os.path.exists(path) and open(path).read() == text, "%s drifted; rerun with UI_CAPTURE_UPDATE=1" % name


def last_frame(n):
    return n.ui.frames()[-1]


def settle(n):
    time.sleep(0.4)


def frame_with(n, needle, timeout=15.0):
    """The newest frame (since boot) in which some string contains ``needle``; waits for one to appear."""
    end = time.time() + timeout
    while time.time() < end:
        for fr in reversed(n.ui.frames()):
            if any(needle in s[3] for s in fr):
                return fr
        time.sleep(0.05)
    raise AssertionError("no frame showed %r; last: %s" % (needle, n.ui.last_text()))


def page_name(text):
    for key, name in (("Unread Msg", "home"), ("FQ:", "radio"), ("toggle:", "bluetooth"), ("Mode:", "connectivity"),
                      ("advert:", "advert"), ("hibernate", "hibernate"), ("discover:", "discovery")):
        if key in text:
            return name
    return "recent"


def tour(n):
    """Click through every home page once; returns [(page name, frame)] starting at the home page."""
    t.home_settled(n)
    pages = [("home", last_frame(n))]
    for _ in range(14):
        before = n.ui.last_text()
        n.ui.press("click")
        end = time.time() + 15
        while time.time() < end and n.ui.last_text() == before:
            time.sleep(0.05)
        settle(n)    # let the page's own alert ("Recent adverts") expire
        txt = n.ui.last_text()
        nm = page_name(txt)
        if nm == "home":
            break
        if len(pages) == 1:
            nm = "recent"    # the first page after home; empty until an advert is heard
        pages.append((nm, last_frame(n)))
    return pages


def order_line(pages):
    return " -> ".join(nm for nm, _ in pages) + " -> (back to home)"


def go_connectivity(n):
    t.goto_connectivity(n)
    settle(n)
    return last_frame(n)


def test_ble_mode_screens(nodes):
    n, c = start(nodes, "cap_ble")
    pages = tour(n)
    d = dict(pages)
    emit("home_ble", "Home page, BLE mode, per-device PIN (the simulated USB host counts as connected)", d["home"])
    emit("page_radio", "Radio page", d["radio"])
    emit("page_bluetooth", "Bluetooth page (the 32x32 icon is a graphic and is not recorded)", d["bluetooth"])
    emit("page_advert", "Advert page (icon not recorded)", d["advert"])
    emit("page_discovery", "Repeater discovery page (nothing sent yet)", d["discovery"])
    emit("page_hibernate", "Hibernate page (icon not recorded)", d["hibernate"])
    emit("page_recent_empty", "Recent adverts page with nothing heard yet", d["recent"])
    emit("connectivity_ble", "Connectivity page, BLE mode, Normal, per-device PIN, no phone", d["connectivity"])
    ble_order = order_line(pages)
    # a phone connects
    go_connectivity(n)
    n.ui.phone(True)
    n.ui.wait_text("Phone: connected")
    settle(n)
    emit("connectivity_ble_phone", "Connectivity page, BLE mode, phone connected", last_frame(n))
    n.ui.phone(False)
    n.ui.wait_text("Phone: not connected")
    # arm the switch, then cancel it
    n.ui.press("long")
    fr = frame_with(n, "Switch to Wi-Fi?")
    emit("confirm_switch", "Connectivity page, switch armed (first long press)", fr)
    n.ui.press("click")
    fr = frame_with(n, "Cancelled")
    emit("alert_cancelled", "Alert after a click while armed", fr, alert="Cancelled")
    settle(n)
    # arm and confirm
    n.ui.press("long")
    frame_with(n, "Switch to Wi-Fi?")
    n.ui.press("long")
    fr = frame_with(n, "Restarting...")
    emit("alert_restarting", "Alert after the confirming long press", fr, alert="Restarting...")
    assert n.wait_exit(10) == 75
    emit_text("page_order_ble", "Home page order, BLE mode, as clicked on the simulated board", ble_order)


def test_ble_default_pin_and_hardened(nodes):
    n, c = start(nodes, "cap_dp")
    assert c.cli("set pin 123456").startswith("> pin is now")
    n.restart()
    c = t._client(n)
    t.home_settled(n)
    settle(n)
    emit("home_ble_default_pin", "Home page, BLE mode, default PIN (drawn as a warning)", last_frame(n))
    emit("connectivity_ble_default_pin", "Connectivity page, BLE mode, default PIN", go_connectivity(n))
    assert c.cli("set pin 482916").startswith("> pin is now")   # a made-up non-default PIN
    n.restart()
    c = t._client(n)
    t.home_settled(n)
    assert c.cli("set posture hardened") == "OK - posture hardened"
    fr = go_connectivity(n)
    assert n.ui.wait_text("Mode: BLE, Hardened", 8)
    settle(n)
    emit("connectivity_ble_hardened", "Connectivity page, BLE mode, Hardened, per-device PIN", last_frame(n))


def test_ble_low_heap(nodes):
    n, c = start(nodes, "cap_low")
    t.home_settled(n)
    settle(n)
    n.heap(300000, 1000)                       # the largest block falls below the 20 KB floor
    assert n.ui.wait_text("LOW MEM", 20), n.ui.last_text()
    settle(n)
    emit("home_ble_low_mem", "Home page, BLE mode, largest free heap block below the floor", last_frame(n))


@pytest.fixture
def wenv(tmp_path):
    return WifiEnv(str(tmp_path / "w.wifi"))


def test_wifi_mode_screens(nodes, wenv):
    wenv.set("ExNet", WIFI_PW, rssi=-61)
    n, c = start(nodes, "cap_wifi", wifi=wenv)
    t.wifi_profile(c, 0, "ExNet", WIFI_PW)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = t.restart_after_switch(n)
    t.home_settled(n)
    t.wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link")
    assert n.ui.wait_text("Wi-Fi 192.0.2.10")
    pages = tour(n)
    d = dict(pages)
    emit("home_wifi", "Home page, Wi-Fi mode, connected (no pairing PIN is drawn)", d["home"])
    emit("connectivity_wifi_nomqtt", "Connectivity page, Wi-Fi mode, connected, no MQTT profile", d["connectivity"])
    assert "bluetooth" not in d, "the Bluetooth page must be skipped in Wi-Fi mode"
    wifi_order = order_line(pages)
    emit_text("page_order_wifi", "Home page order, Wi-Fi mode, as clicked on the simulated board", wifi_order)
    # the page armed to switch back to BLE
    go_connectivity(n)
    n.ui.press("long")
    emit("confirm_switch_to_ble", "Connectivity page in Wi-Fi mode, switch armed", frame_with(n, "Switch to BLE?"))
    n.ui.press("click")
    n.ui.wait_text("Mode:")
    settle(n)
    # the link goes away
    wenv.down("ExNet")
    t.wait_until(lambda: "IP: -" in n.ui.last_text(), "the screen to notice the lost link", 40)
    settle(n)
    emit("connectivity_wifi_link_lost", "Connectivity page, Wi-Fi mode, link lost", last_frame(n))


def test_long_ssid_pushes_the_signal_strength_off_the_line(nodes, wenv):
    wenv.set("ExampleNetwork-5G", WIFI_PW, rssi=-61)
    n, c = start(nodes, "cap_long", wifi=wenv)
    t.wifi_profile(c, 0, "ExampleNetwork-5G", WIFI_PW)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = t.restart_after_switch(n)
    t.home_settled(n)
    t.wait_until(lambda: "connected" in c.cli("get wifi.status"), "the Wi-Fi link")
    go_connectivity(n)
    assert n.ui.wait_text("Wi-Fi p0 ExampleNe")
    settle(n)
    emit("connectivity_wifi_long_ssid", "Connectivity page, Wi-Fi mode, a network name too long for the line",
         last_frame(n))


def test_wifi_no_profile_and_hardened(nodes, wenv):
    n, c = start(nodes, "cap_np")
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = t.restart_after_switch(n)
    t.home_settled(n)
    assert n.ui.wait_text("Wi-Fi: no profile")
    settle(n)
    emit("home_wifi_no_profile", "Home page, Wi-Fi mode, no profile", last_frame(n))
    emit("connectivity_wifi_no_profile", "Connectivity page, Wi-Fi mode, no profile", go_connectivity(n))

    wenv.set("ExNet", WIFI_PW)
    n2, c2 = start(nodes, "cap_hw", wifi=wenv)
    t.wifi_profile(c2, 0, "ExNet", WIFI_PW)
    assert c2.cli("set posture hardened").startswith("OK")
    assert c2.cli("set connectivity.mode wifi").startswith("OK")
    c2 = t.restart_after_switch(n2)
    t.home_settled(n2)
    fr = go_connectivity(n2)
    emit("connectivity_wifi_hardened", "Connectivity page, Wi-Fi mode, Hardened, no MQTT profile Hardened permits",
         last_frame(n2))


@pytest.fixture
def brokers(tmp_path):
    mqtt_broker.require_tools()
    made = []

    def make(name, ip, port, tls=False):
        cert = key = fp = None
        if tls:
            cert, key, fp = make_cert(str(tmp_path), name, ip)
        b = Broker(tmp_path, name, ip, port, cert, key, None).start()
        b.fingerprint = fp
        made.append(b)
        return b

    yield make
    for b in made:
        b.stop()


def boot_wifi_mode_with_mqtt(nodes, tmp_path, name, profile_fn, seed=9):
    w = WifiEnv(str(tmp_path / (name + ".wifi")))
    w.set("ExNet", WIFI_PW, rssi=-61)
    m = MqttEnv(str(tmp_path / (name + ".mqtt")))
    n, c = start(nodes, name, seed=seed, wifi=w, mqtt=m)
    t.wifi_profile(c, 0, "ExNet", WIFI_PW)
    profile_fn(c)
    assert c.cli("set connectivity.mode wifi").startswith("OK")
    c = t.restart_after_switch(n)
    t.home_settled(n)
    go_connectivity(n)
    return n, c


def test_mqtt_screens(nodes, tmp_path, brokers):
    brokers("a", "127.0.0.2", 18830)
    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "cap_mp", lambda c: t.mqtt_profile(c, 0, "127.0.0.2", 18830))
    assert n.ui.wait_text("MQTT0 plain", 40)
    settle(n)
    emit("connectivity_wifi_mqtt_plain", "Connectivity page, Wi-Fi mode, MQTT profile 0, plain TCP", last_frame(n))

    b = brokers("s", "127.0.0.2", 18883, tls=True)
    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "cap_mv",
                                      lambda c: t.mqtt_profile(c, 0, "127.0.0.2", 18883, tls=True, anchor=b.fingerprint),
                                      seed=10)
    assert n.ui.wait_text("MQTT0 TLS verified", 40)
    settle(n)
    emit("connectivity_wifi_mqtt_tls_verified", "Connectivity page, Wi-Fi mode, MQTT over TLS, certificate verified",
         last_frame(n))

    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "cap_mu",
                                      lambda c: t.mqtt_profile(c, 0, "127.0.0.2", 18883, tls=True), seed=11)
    assert n.ui.wait_text("MQTT0 TLS UNVERIFIED", 40)
    settle(n)
    emit("connectivity_wifi_mqtt_tls_unverified", "Connectivity page, Wi-Fi mode, MQTT over TLS, not verified (warning)",
         last_frame(n))

    n, c = boot_wifi_mode_with_mqtt(nodes, tmp_path, "cap_mn", lambda c: t.mqtt_profile(c, 0, "127.0.0.9", 18899),
                                      seed=12)
    t.wait_until(lambda: "MQTT: backoff" in n.ui.last_text() or "MQTT: connecting" in n.ui.last_text(), "an MQTT state", 40)
    # the state word alternates; keep the capture stable by recording the first state seen
    emit("connectivity_wifi_mqtt_no_session", "Connectivity page, Wi-Fi mode, broker unreachable (state shown)",
         last_frame(n))


def test_every_capture_is_shown_in_docs_ui_md():
    """docs/UI.md embeds each docs/ui/*.txt (without its title line) as a code block."""
    doc = open(os.path.join(os.path.dirname(DOCS_UI), "UI.md")).read()
    names = sorted(f for f in os.listdir(DOCS_UI) if f.endswith(".txt"))
    assert names
    for f in names:
        body = "".join(open(os.path.join(DOCS_UI, f)).read().splitlines(True)[1:]).rstrip("\n")
        assert body in doc, "%s is not shown in docs/UI.md" % f
