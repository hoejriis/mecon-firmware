"""Roles that have no simulated board here (sensor, secure chat) must still report their channel to the Dispatcher,
or the regulatory duty-cycle floor silently does not apply to them (Dispatcher::getRadioChannel() defaults to
"unknown", which means only the user's airtime factor limits transmission).

This is a STATIC check of the sources, not a behavioural one: no simulated sensor or secure-chat board exists, so
what it guards is that the override is declared and that every place the role retunes the radio also updates the
channel it reports.  The behaviour of the floor itself is measured on the simulated repeater (test_duty_cycle.py)
and on the KISS modem (pio test -e native_kiss_modem)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def src(rel):
    return (ROOT / rel).read_text()


def test_sensor_reports_the_channel_it_applied():
    h, c = src("examples/simple_sensor/SensorMesh.h"), src("examples/simple_sensor/SensorMesh.cpp")
    assert re.search(r"bool getRadioChannel\(float& freq_mhz, float& bw_khz\) const override", h)
    assert "return radio_freq > 0;" in h
    sets = re.findall(r"radio_driver\.setParams\(([^;]*)\);\s*\n\s*(radio_freq = [^;]*;[^\n]*)", c)
    calls = len(re.findall(r"radio_driver\.setParams\(", c))
    assert calls == 3 and len(sets) == 3, (calls, sets)      # boot, temporary params, revert: each updates the channel
    for args, upd in sets:
        freq, bw = args.split(",")[0].strip(), args.split(",")[1].strip()
        assert "radio_freq = %s" % freq in upd and "radio_bw = %s" % bw in upd, (args, upd)


def test_secure_chat_reports_the_channel_it_applied():
    m = src("examples/simple_secure_chat/main.cpp")
    assert re.search(r"bool getRadioChannel\(float& freq_mhz, float& bw_khz\) const override", m)
    assert "return _radio_freq > 0;" in m
    assert re.search(r"radio_driver\.setParams\(the_mesh\.getFreqPref\(\), LORA_BW[^;]*;\s*\n\s*"
                     r"the_mesh\.noteRadioApplied\(the_mesh\.getFreqPref\(\), LORA_BW\);", m)
    assert len(re.findall(r"radio_driver\.setParams\(", m)) == 1


def test_every_dispatcher_role_overrides_the_channel():
    for rel in ("examples/simple_repeater/MyMesh.h", "examples/simple_room_server/MyMesh.h",
                "examples/companion_radio/MyMesh.h", "examples/simple_sensor/SensorMesh.h",
                "examples/simple_secure_chat/main.cpp"):
        assert "getRadioChannel(float& freq_mhz, float& bw_khz) const override" in src(rel), rel


def test_the_kiss_modem_fails_closed_on_an_unknown_channel():
    c = src("examples/kiss_modem/KissModem.cpp")
    assert "if (!(_chan_freq_mhz > 0)) { _duty_refuse = true; return false; }" in c
    assert "if (!dutyAllowsSend(_tx_est_ms)) {" in c
