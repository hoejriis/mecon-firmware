"""Time-on-air: the node's C++ implementation against the Semtech formula and known values."""
import math
import subprocess

import pytest

from sim.harness import BIN


def semtech_toa_ms(sf, bw_khz, cr_den, preamble, payload):
    """Independent restatement of the datasheet formula (SX127x AN1200.13), explicit header, CRC on."""
    tsym = 2 ** sf / bw_khz                                      # ms
    de = 1 if (sf >= 11 and bw_khz <= 125) or tsym > 16 else 0
    t_pre = (preamble + 4.25) * tsym
    n_pay = 8 + max(math.ceil((8 * payload - 4 * sf + 28 + 16) / (4 * (sf - 2 * de))) * cr_den, 0)
    return t_pre + n_pay * tsym


def node_toa_ms(sf, bw, cr, pre, ln):
    out = subprocess.check_output([BIN["repeater"], "--toa", str(sf), str(bw), str(cr), str(pre), str(ln)])
    return float(out) / 1000.0


CASES = [  # sf, bw kHz, cr (5..8), preamble, payload bytes
    (7, 125, 5, 8, 10),
    (8, 62.5, 5, 32, 60),
    (10, 250, 6, 16, 100),
    (12, 125, 5, 8, 10),
    (11, 500, 8, 16, 255),
]


@pytest.mark.parametrize("sf,bw,cr,pre,ln", CASES)
def test_matches_semtech_formula_within_1ms(sf, bw, cr, pre, ln):
    assert node_toa_ms(sf, bw, cr, pre, ln) == pytest.approx(semtech_toa_ms(sf, bw, cr, pre, ln), abs=1.0)


def test_known_published_values():
    # Values every LoRa airtime calculator agrees on (125 kHz, CR 4/5, 8-symbol preamble, 10 bytes).
    assert node_toa_ms(7, 125, 5, 8, 10) == pytest.approx(41.216, abs=1.0)
    assert node_toa_ms(12, 125, 5, 8, 10) == pytest.approx(991.232, abs=1.0)


def test_airtime_grows_with_payload():
    assert node_toa_ms(8, 62.5, 5, 32, 100) > node_toa_ms(8, 62.5, 5, 32, 10)
