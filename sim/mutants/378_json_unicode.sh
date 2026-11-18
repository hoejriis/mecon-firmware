# \uXXXX escapes decode to UTF-8 (#579): src/helpers/iface/IfaceJson.cpp. A lone surrogate is never a character.
mutate json_lone_low_surrogate src/helpers/iface/IfaceJson.cpp '  if (cp >= 0xDC00 && cp <= 0xDFFF) return false;' '  if (false) return false;' unit:test_iface
mutate json_high_without_low src/helpers/iface/IfaceJson.cpp "    if (e - p < 2 || p[0] != '\\\\' || p[1] != 'u') return false;" "    if (e - p < 2) return false;" unit:test_iface
