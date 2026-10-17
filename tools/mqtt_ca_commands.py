#!/usr/bin/env python3
"""Print the console commands that set a CA certificate as an MQTT profile's trust anchor.

The device's serial line is short (79 characters), and a CA certificate is about 600 to 1,900 characters of base64,
so it is sent in pieces: `ca begin`, many `ca add <piece>`, and `ca end`. Nothing is stored until `ca end`, and a
certificate that arrived damaged or truncated is refused there with the profile unchanged.

usage: mqtt_ca_commands.py <profile 0-5> <ca.pem> [--piece N]

Paste the output into the device console (or send it line by line with any serial tool), then check with
`get mqtt.profile <n>`, which shows anchor=ca. The certificate is public material, not a secret.
"""
import base64
import re
import sys

MAX_B64 = 1920    # MqttProfile::CA_B64_MAX
MAX_LINE = 79     # the serial line buffer of the Companion console


def commands(profile, pem_text, piece=48):
    blocks = re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem_text, re.S)
    if len(blocks) != 1:
        raise SystemExit("expected exactly one certificate in the file, found %d" % len(blocks))
    b64 = "".join(blocks[0].split())
    der = base64.b64decode(b64, validate=True)
    if len(b64) > MAX_B64:
        raise SystemExit("this certificate is %d base64 characters; the device holds at most %d" % (len(b64), MAX_B64))
    if der[0] != 0x30:
        raise SystemExit("not a DER certificate")
    out = ["set mqtt.profile %d ca begin" % profile]
    out += ["set mqtt.profile %d ca add %s" % (profile, b64[i:i + piece]) for i in range(0, len(b64), piece)]
    out.append("set mqtt.profile %d ca end" % profile)
    longest = max(len(c) for c in out)
    if longest > MAX_LINE:
        raise SystemExit("a command is %d characters, over the %d the console takes; use a smaller --piece" % (longest, MAX_LINE))
    return out


def main(argv):
    piece = 48
    if "--piece" in argv:
        k = argv.index("--piece")
        piece = int(argv[k + 1])
        del argv[k:k + 2]
    if len(argv) != 3 or not argv[1].isdigit() or int(argv[1]) > 9:
        raise SystemExit(__doc__)
    with open(argv[2]) as f:
        print("\n".join(commands(int(argv[1]), f.read(), piece)))


if __name__ == "__main__":
    main(sys.argv)
