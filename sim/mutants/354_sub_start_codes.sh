# sub.start: an out-of-range bound answers invalid with the field it missed (#408)
T=sim/tests/test_telemetry_streams.py::test_a_bound_out_of_range_names_its_field
mutate sub_period_no_code src/helpers/iface/Streams.cpp '  if (pt >= 0 && !jsonGetInt(*c.doc, pt, def.min_s, def.max_s, &period)) { c.error = "bad-period"; return ST_INVALID; }' '  if (pt >= 0 && !jsonGetInt(*c.doc, pt, def.min_s, def.max_s, &period)) return ST_INVALID;' "$T"
mutate sub_lifetime_no_code src/helpers/iface/Streams.cpp '  if (lt >= 0 && !jsonGetInt(*c.doc, lt, LIFE_MIN_S, LIFE_MAX_S, &life)) { c.error = "bad-lifetime"; return ST_INVALID; }' '  if (lt >= 0 && !jsonGetInt(*c.doc, lt, LIFE_MIN_S, LIFE_MAX_S, &life)) return ST_INVALID;' "$T"
