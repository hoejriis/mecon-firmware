#!/usr/bin/env python3
"""Decode a compact report (docs/contract/TELEMETRY.md, "Compact reports over LoRa"). decode(bytes) returns the fields, the bytes
the signature covers and the signature. A field the device did not send is absent; the reserved values read as None."""
import struct

DOMAIN = b"MECON-UPLINK-v1\x00report\x00"


def decode(raw):
    if len(raw) < 15 + 64 or raw[0] != 0x1E or raw[1] != 1:
        raise ValueError("not a version 1 report")
    fields = raw[2]
    boot, seq, ts = struct.unpack_from("<III", raw, 3)
    out = {"boot": boot, "seq": seq, "time": ts or None}
    p = 15
    if fields & 1:
        up, heap, mv = struct.unpack_from("<IHH", raw, p)
        out["health"] = {"uptime_s": up, "heap_free_kb": None if heap == 0xFFFF else heap, "battery_mv": None if mv == 0xFFFF else mv}
        p += 8
    if fields & 2:
        rx, tx, err, nf, rssi, snr = struct.unpack_from("<IIBbbb", raw, p)
        none = lambda v: None if v == 0x7F else v  # noqa: E731
        out["radio"] = {"rx": rx, "tx": tx, "err_flags": err, "noise_floor_dbm": none(nf), "rssi_dbm": none(rssi),
                        "snr_db": None if snr == 0x7F else snr / 4}
        p += 12
    if fields & 4:
        fix, lat, lon, alt, sats, age = struct.unpack_from("<BiihBH", raw, p)
        out["location"] = {"fix": bool(fix), "sats": sats}
        if fix:
            out["location"].update(lat_e6=lat, lon_e6=lon, alt_m=alt, age_s=None if age == 0xFFFF else age)
        p += 14
    if len(raw) != p + 64:
        raise ValueError("length does not match its fields")
    return out, DOMAIN + raw[:p], raw[p:]
