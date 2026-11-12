# The Companion's contact auto-add, telemetry and advert-location settings as config keys (#507)
T=sim/tests/test_contact_policy_keys.py
F=examples/companion_radio/MyMesh.cpp
mutate cpk_manual_add_inverted $F '    if (get) { sprintf(reply, "> %s", (_prefs.manual_add_contacts & 1) ? "on" : "off"); return true; }' '    if (get) { sprintf(reply, "> %s", (_prefs.manual_add_contacts & 1) ? "off" : "on"); return true; }' "$T"
mutate cpk_manual_add_not_saved $F '    _prefs.manual_add_contacts = on ? (_prefs.manual_add_contacts | 1) : (_prefs.manual_add_contacts & ~1);' '' "$T"
mutate cpk_autoadd_clears_others $F '      _prefs.autoadd_config = on ? (_prefs.autoadd_config | a.bit) : (_prefs.autoadd_config & ~a.bit);' '      _prefs.autoadd_config = on ? a.bit : 0;' "$T"
mutate cpk_max_hops_unbounded $F '      if (!*val || *end || v < 0 || v > 64) { strcpy(reply, "Error, 0 to 64"); return true; }' '      if (!*val || *end || v < 0 || v > 255) { strcpy(reply, "Error, 0 to 64"); return true; }' "$T"
mutate cpk_telemetry_loc_is_base $F '                  : strcmp(name, "telemetry.location") == 0  ? &_prefs.telemetry_mode_loc' '                  : strcmp(name, "telemetry.location") == 0  ? &_prefs.telemetry_mode_base' "$T"
mutate cpk_advert_share_lost $F '    if (strcmp(val, "share") == 0) _prefs.advert_loc_policy = ADVERT_LOC_SHARE;' '    if (strcmp(val, "share") == 0) _prefs.advert_loc_policy = ADVERT_LOC_NONE;' "$T"
mutate cpk_room_bit_wrong $F '    {"sensor", AUTO_ADD_SENSOR}, {"overwrite", AUTO_ADD_OVERWRITE_OLDEST}};' '    {"sensor", AUTO_ADD_SENSOR}, {"overwrite", AUTO_ADD_SENSOR}};' "$T"
mutate cpk_key_range_open src/helpers/iface/IfaceContactPolicyKeys.inc '  {"contacts.autoadd.max_hops", K_INT, 0, 64,' '  {"contacts.autoadd.max_hops", K_INT, 0, 65,' "$T"
