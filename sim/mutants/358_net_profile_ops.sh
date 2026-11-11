# net.profile.clear and mqtt.profile.anchor (#490)
T=sim/tests/test_net_profile_ops.py
F=src/helpers/iface/NetProfileOps.cpp
mutate net_in_use_ignored $F '  if (!h.cable && in_use != NET_NONE && (unsigned)n == in_use) { c.error = "in-use"; return ST_REFUSED; }' '' "$T"
mutate net_allow_skipped $F '  if (!h.allow || !h.allow(h.user, line)) { c.error = "not-permitted"; return ST_REFUSED; }' '  if (!h.allow) { c.error = "not-permitted"; return ST_REFUSED; }' "$T"
mutate net_ca_end_skipped $F '    if ((st = runLine(c, h, line)) != ST_OK) return st;
    anchor = "ca";' '    anchor = "ca";' "$T"
mutate net_anchor_one_of_loose $F '  if ((fp >= 0) + (ca >= 0) + (cl >= 0) != 1) { c.error = "one-of"; return ST_INVALID; }' '  if ((fp >= 0) + (ca >= 0) + (cl >= 0) == 0) { c.error = "one-of"; return ST_INVALID; }' "$T"
