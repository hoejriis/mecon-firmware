"""How the bench tools open a serial port without resetting the board behind it.

A Heltec V3 (a CP2102 bridge, USB vendor 0x10c4) wires DTR and RTS to its auto-reset pair: EN goes low, and the board
resets, while RTS is high and DTR is low. Linux raises both lines at open, which the pair ignores, so the safe thing is to
leave them as the open leaves them. Setting DTR low first and RTS low second passes through the reset state; that is how
mecon_iface_hw.py reset v3test twice on 2026-11-08, once in the middle of an F2 session. The same rule, measured on
v3test, is in the Reader's bench serial bridge (deimos-mesh, serial_bridge.py, #907).

Every other board keeps the preset: DTR low and RTS low, or DTR high for an nRF52 board (MECON_SERIAL_DTR=1), which stays
silent on USB until the host asserts DTR. BENCH_LINES=untouched or BENCH_LINES=low overrides the choice.
"""
import os

CP2102_VID = 0x10C4


def _usb_vid(port):
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    real = os.path.realpath(port)   # /dev/bench/<board> is a udev link
    for p in list_ports.comports():
        if os.path.realpath(p.device) == real:
            return p.vid
    return None


def leave_lines(port):
    """True: open the port and leave DTR and RTS as the open leaves them."""
    forced = os.environ.get("BENCH_LINES")
    if forced in ("untouched", "low"):
        return forced == "untouched"
    return _usb_vid(port) == CP2102_VID


def open_serial(port, baud=115200, timeout=0.05, dtr=None):
    """An open pyserial port. A pyserial URL (socket://...) is never a board's lines, so it is opened as it is."""
    import serial
    s = serial.serial_for_url(port, baud, timeout=timeout, do_not_open=True)
    if "://" not in port and not leave_lines(port):
        s.dtr = (os.environ.get("MECON_SERIAL_DTR") == "1") if dtr is None else dtr   # set before open: no pulse
        s.rts = False
    s.open()
    return s
