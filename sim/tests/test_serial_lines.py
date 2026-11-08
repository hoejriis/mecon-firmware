"""tools/serial_lines.py: which boards the bench tools open with their lines left alone. A Heltec V3's CP2102 resets when
DTR drops while RTS is high, so its lines are left as the open leaves them; other boards keep the preset."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import serial_lines  # noqa: E402


def test_a_cp2102_is_left_alone_and_other_boards_keep_the_preset(monkeypatch):
    monkeypatch.delenv("BENCH_LINES", raising=False)
    monkeypatch.setattr(serial_lines, "_usb_vid", lambda port: 0x10C4)
    assert serial_lines.leave_lines("/dev/bench/v3test")
    for vid in (0x303A, 0x239A, 0x2886, None):   # ESP32-S3 native USB, Adafruit nRF52, Seeed, not found
        monkeypatch.setattr(serial_lines, "_usb_vid", lambda port, v=vid: v)
        assert not serial_lines.leave_lines("/dev/ttyACM0"), vid


def test_bench_lines_overrides_either_way(monkeypatch):
    monkeypatch.setattr(serial_lines, "_usb_vid", lambda port: 0x10C4)
    monkeypatch.setenv("BENCH_LINES", "low")
    assert not serial_lines.leave_lines("/dev/bench/v3test")
    monkeypatch.setattr(serial_lines, "_usb_vid", lambda port: 0x303A)
    monkeypatch.setenv("BENCH_LINES", "untouched")
    assert serial_lines.leave_lines("/dev/ttyACM0")
