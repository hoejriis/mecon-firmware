# The Companion's contact auto-add, telemetry and advert-location settings as config keys (#507)
T=sim/tests/test_contact_policy_keys.py
F=examples/companion_radio/MyMesh.cpp
mutate cpk_bit_read_inverted $F '        if (get) { sprintf(reply, "> %s", (*bits & l.arg) ? "on" : "off"); return true; }' '        if (get) { sprintf(reply, "> %s", (*bits & l.arg) ? "off" : "on"); return true; }' "$T"
mutate cpk_on_clears_others $F '        if (strcmp(val, "on") == 0) *bits |= l.arg;' '        if (strcmp(val, "on") == 0) *bits = l.arg;' "$T"
mutate cpk_off_ignored $F '        else if (strcmp(val, "off") == 0) *bits &= ~l.arg;' '        else if (strcmp(val, "off") == 0) {}' "$T"
mutate cpk_max_hops_unbounded $F '        if (!*val || *end || v < 0 || v > 64) { strcpy(reply, "Error, 0 to 64"); return true; }' '        if (!*val || *end || v < 0 || v > 255) { strcpy(reply, "Error, 0 to 64"); return true; }' "$T"
mutate cpk_telemetry_loc_is_base $F 'l.arg == 1 ? &_prefs.telemetry_mode_loc : &_prefs.telemetry_mode_env;' 'l.arg == 1 ? &_prefs.telemetry_mode_base : &_prefs.telemetry_mode_env;' "$T"
mutate cpk_advert_share_lost $F '        if (strcmp(val, "share") == 0) _prefs.advert_loc_policy = ADVERT_LOC_SHARE;' '        if (strcmp(val, "share") == 0) _prefs.advert_loc_policy = ADVERT_LOC_NONE;' "$T"
mutate cpk_overwrite_bit_wrong $F '    {"get contacts.autoadd.overwrite", "set contacts.autoadd.overwrite ", AUTOADD, AUTO_ADD_OVERWRITE_OLDEST},' '    {"get contacts.autoadd.overwrite", "set contacts.autoadd.overwrite ", AUTOADD, AUTO_ADD_SENSOR},' "$T"
mutate cpk_manual_is_autoadd $F '    uint8_t* bits = l.kind == MANUAL ? &_prefs.manual_add_contacts : &_prefs.autoadd_config;' '    uint8_t* bits = &_prefs.autoadd_config;' "$T"
mutate cpk_key_range_open src/helpers/iface/IfaceContactPolicyKeys.inc '  {"contacts.autoadd.max_hops", K_INT, 0, 64,' '  {"contacts.autoadd.max_hops", K_INT, 0, 65,' "$T"
