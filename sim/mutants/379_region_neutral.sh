# Region-neutral forwarding on the Repeater: unknown scopes only, explicit denies win, Repeater only: examples/simple_repeater/MyMesh.cpp, src/helpers/iface/ConsoleIface.cpp
T=sim/tests/test_region_neutral.py
# An explicit deny is a hard exclusion: a code that matches any configured region is not "unknown"
mutate neutral_ignores_deny examples/simple_repeater/MyMesh.cpp '    if (recv_pkt_region == NULL && _prefs.region_neutral && region_map.findMatch(pkt, 0) == NULL) {' '    if (recv_pkt_region == NULL && _prefs.region_neutral) {' $T::test_an_explicit_deny_beats_neutral_and_an_allowed_region_is_not_counted
# Off is stock: the mode applies only when set
mutate neutral_applies_when_off examples/simple_repeater/MyMesh.cpp '    if (recv_pkt_region == NULL && _prefs.region_neutral && region_map.findMatch(pkt, 0) == NULL) {' '    if (recv_pkt_region == NULL && region_map.findMatch(pkt, 0) == NULL) {' $T::test_off_by_default_an_unknown_scope_is_not_forwarded_on_it_is_unchanged
# Unscoped floods keep the wildcard rule: the gate in allowPacketForward lets only the neutral flag through
mutate neutral_lets_unscoped_through examples/simple_repeater/MyMesh.cpp '    if (!recv_pkt_neutral) {' '    if (!recv_pkt_neutral && !_prefs.region_neutral) {' $T::test_an_unscoped_flood_keeps_the_wildcard_rule
# A forward the region itself allowed is not a neutral forward
mutate neutral_counts_native_forwards examples/simple_repeater/MyMesh.cpp '  if (neutral_pass) n_neutral_forwarded++;' '  if (packet->isRouteFlood()) n_neutral_forwarded++;' $T::test_an_explicit_deny_beats_neutral_and_an_allowed_region_is_not_counted
# The setting is kept across a reboot
mutate neutral_not_persisted src/helpers/CommonCLI.h '      def("neutral", _parent->region_neutral);' '      (void)_parent;' $T::test_the_setting_survives_a_reboot
# Only the Repeater lists the key: the Room server's table stops before the last two entries (battery.guard and region.neutral)
mutate neutral_listed_on_room src/helpers/iface/ConsoleIface.cpp '  const size_t nkeys = strcmp(host().role, "repeater") == 0 ? N_SERVER_KEYS : N_SERVER_KEYS - 2;' '  const size_t nkeys = N_SERVER_KEYS;' $T::test_a_room_server_has_no_such_key_or_line
