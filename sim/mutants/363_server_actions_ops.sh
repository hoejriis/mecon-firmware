# clock.reset, stats.clear, the packet log, radio.temp, the neighbours and room.post (#492)
T=sim/tests/test_server_actions_ops.py
F=src/helpers/iface/ConsoleIface.cpp
mutate act_clock_reset_no_guess $F '  m->host().clock_guess(m->host().user);
  m->rebootSoon();' '  m->rebootSoon();' "$T"
mutate act_clock_reset_no_reboot $F '  m->host().clock_guess(m->host().user);
  m->rebootSoon();' '  m->host().clock_guess(m->host().user);' "$T"
mutate act_log_read_next_lost $F '  w.key("next"); if ((uint32_t)offset + got < size) w.unum((uint32_t)offset + got); else w.null();' '  w.key("next"); w.null();' "$T"
mutate act_log_read_offset_ignored $F '  const int n = h.log_read(h.user, (uint32_t)offset, buf, sizeof(buf), &size);' '  const int n = h.log_read(h.user, 0, buf, sizeof(buf), &size);' "$T"
mutate act_log_read_read_class $F '  {"log.read", opLogRead, PRIV_WRITE, false},' '  {"log.read", opLogRead, PRIV_READ, false},' "$T"
mutate act_log_erase_wrong_line $F 'Status opLogErase(OpContext& c) { return actionLine(c, "log erase", "   log erased"); }' 'Status opLogErase(OpContext& c) { return actionLine(c, "log stop", "   logging off"); }' "$T"
mutate act_radio_temp_minutes_loose $F '!jsonGetInt(*c.doc, jsonFind(*c.doc, c.req->args, "minutes"), 1, 1440, &mins))' '!jsonGetInt(*c.doc, jsonFind(*c.doc, c.req->args, "minutes"), 0, 100000, &mins))' "$T"
mutate act_neighbour_newest_dropped examples/simple_repeater/MyMesh.cpp '  memcpy(key, sorted[idx]->id.pub_key, PUB_KEY_SIZE);' '  memset(key, 0, PUB_KEY_SIZE);' "$T"
mutate act_neighbour_remove_upper_ok $F '    if (!((*p >= '"'"'0'"'"' && *p <= '"'"'9'"'"') || (*p >= '"'"'a'"'"' && *p <= '"'"'f'"'"'))) return ST_INVALID;' '    ;' "$T"
mutate act_neighbour_remove_noop $F '  snprintf(line, sizeof(line), "neighbor.remove %s", hex);' '  snprintf(line, sizeof(line), "neighbors");' "$T"
mutate act_neighbour_caps_on_room $F '  if (m->host().neighbour) for (const char* ncap : NEIGHBOUR_CAPABILITIES) add(ncap);' '  for (const char* ncap : NEIGHBOUR_CAPABILITIES) add(ncap);' "$T"
mutate act_room_post_any_role $F '  if (strcmp(self(c)->host().role, "room") != 0) { c.error = "not-on-this-device"; return ST_FAILED; }' '' "$T"
mutate act_discover_noop $F '  return actionLine(c, "discover.neighbors", "OK - Discover sent");' '  return ST_OK;' "$T"
mutate act_log_read_over_broker $F '  if (m->onBroker()) {   // as `log`, the console only' '  if (false) {' "$T"
