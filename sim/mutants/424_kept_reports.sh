# Reports kept for later (docs/contract/TELEMETRY.md, Issue #610): src/helpers/iface/KeptReports.cpp, StandingOps.cpp
KU=unit:test_kept_reports
SU=unit:test_standing
KS=sim/tests/test_kept_reports.py
# (a) an ack releases a report its caller could not list, or one that is not the caller's
mutate kept_ack_releases_the_unlisted src/helpers/iface/StandingOps.cpp '  if (st.withheld) { c.error = "location-withheld"; return ST_REFUSED; }' '  if (false) { c.error = "location-withheld"; return ST_REFUSED; }' $SU
mutate kept_ack_releases_another_members src/helpers/iface/StandingOps.cpp '  if (st.foreign) { c.error = "not-owner"; return ST_REFUSED; }' '  if (false) { c.error = "not-owner"; return ST_REFUSED; }' $SU
mutate kept_ack_range_ignores_withheld src/helpers/iface/StandingOps.cpp '  if (isPrivate(e) && !st.priv) st.withheld = true;   // it was not listed' '  if (false) st.withheld = true;   // it was not listed' $SU
# (b) the ring overwrites without counting the overflow
mutate kept_overflow_not_counted src/helpers/iface/KeptReports.cpp '  return (wf > _base ? wf - _base : 0) + _unkept;' '  return _unkept;' $KU
mutate kept_overflow_not_counted_in_a_list src/helpers/iface/KeptReports.cpp '  return (wf > _base ? wf - _base : 0) + _unkept;' '  return _unkept;' $KS::test_a_full_ring_keeps_the_last_64_and_counts_what_it_pushed_out
mutate kept_unwritten_report_not_counted src/helpers/iface/KeptReports.cpp '{ _unkept++; return false; }
    _next++;' '{ return false; }
    _next++;' $KU
mutate kept_unwritten_batch_not_counted src/helpers/iface/KeptReports.cpp '      _unkept += (uint32_t)(_pend_n - done);' '' $KU
mutate kept_dropped_not_restarted_by_an_ack src/helpers/iface/KeptReports.cpp '  const uint32_t base = mark > wf ? mark : wf;' '  const uint32_t base = _base;' $KU
mutate kept_ring_holds_more_than_64 src/helpers/iface/KeptReports.h '  static const uint32_t RING = 64;' '  static const uint32_t RING = 65;' $KU
# (c) a listed report's bytes differ from the signed original
mutate kept_listed_bytes_cut src/helpers/iface/StandingOps.cpp '  b64(text, e.bytes, e.len);' '  b64(text, e.bytes, e.len - 1);' $SU
mutate kept_listed_bytes_cut_on_the_air src/helpers/iface/StandingOps.cpp '  b64(text, e.bytes, e.len);' '  b64(text, e.bytes, e.len - 1);' $KS::test_the_listed_bytes_are_the_ones_that_went_on_the_air_and_the_ack_is_the_members
mutate kept_stored_bytes_changed src/helpers/iface/KeptReports.cpp '  memcpy(rec + 9, bytes, n);' '  memcpy(rec + 9, bytes, n); rec[9 + n - 1] ^= 1;' $KU
mutate kept_report_resigned_not_kept_as_sent src/helpers/iface/StandingOps.cpp '    if (s.keep && built_len) _kept.add(built, built_len, KeptReports::ownerOf(s.issuer), now);' '    if (s.keep && built_len) { built[built_len - 1] ^= 1; _kept.add(built, built_len, KeptReports::ownerOf(s.issuer), now); }' $SU
# (d) a location-class report listed to a caller who may not read it
mutate kept_location_listed_to_any_caller src/helpers/iface/StandingOps.cpp '  if (isPrivate(e) && !st.priv) { st.withheld++; return true; }' '  if (false) { st.withheld++; return true; }' $SU
mutate kept_location_listed_to_a_cable_caller src/helpers/iface/StandingOps.cpp '  if (isPrivate(e) && !st.priv) { st.withheld++; return true; }' '  if (false) { st.withheld++; return true; }' $KS::test_a_report_with_location_is_withheld_from_a_link_that_may_not_carry_it
mutate kept_withheld_not_counted src/helpers/iface/StandingOps.cpp '  if (isPrivate(e) && !st.priv) { st.withheld++; return true; }' '  if (isPrivate(e) && !st.priv) { return true; }' $SU
mutate kept_report_list_carried_on_the_channel src/helpers/iface/LoraLink.h '  static const char* const OPS[] = {"config.apply", "job.get", "job.ack", "job.cancel", "job.confirm", "posture.set", "report.now",
                                    "region.put"' '  static const char* const OPS[] = {"config.apply", "job.get", "job.ack", "job.cancel", "job.confirm", "posture.set", "report.now", "report.list",
                                    "region.put"' $KS::test_report_list_answers_point_to_point_over_lora_within_a_message_and_not_on_the_channel
# the rest of the contract
mutate kept_every_instruction_keeps src/helpers/iface/StandingOps.cpp 's.slot, &seq, s.keep ? built : nullptr, &built_len);
    if (sent) s.runs++;
    else s.failed++;
    // Kept whether or not it went: a datagram has no acknowledgement, so the device cannot know it was heard. One append.
    if (s.keep && built_len)' 's.slot, &seq, built, &built_len);
    if (sent) s.runs++;
    else s.failed++;
    // Kept whether or not it went: a datagram has no acknowledgement, so the device cannot know it was heard. One append.
    if (built_len)' $SU
mutate kept_report_now_is_kept src/helpers/iface/StandingOps.cpp '  if (!_h.send(_h.user, fields, has_to ? to : nullptr, slot, &seq, nullptr, nullptr)) { c.error = "not-sent"; return ST_BUSY; }' '  uint8_t kb[REPORT_MAX]; size_t kn = 0;
  if (!_h.send(_h.user, fields, has_to ? to : nullptr, slot, &seq, kb, &kn)) { c.error = "not-sent"; return ST_BUSY; }
  if (kn) _kept.add(kb, kn, 0);' $SU
mutate kept_flag_not_stored src/helpers/iface/StandingOps.cpp '    *p++ = (uint8_t)(s.fields | (s.keep ? KEEP_BIT : 0));' '    *p++ = (uint8_t)s.fields;' $SU
mutate kept_unsupported_board_accepts_keep src/helpers/iface/StandingOps.cpp '  if (n.keep && !_kept.available()) { c.error = "unsupported"; return ST_FAILED; }' '' $SU
mutate kept_page_ignores_the_link src/helpers/iface/StandingOps.cpp '  if (st.listed >= st.limit || (st.listed > 0 && st.w->size() + est > st.budget)) { st.more = true; return true; }' '  if (st.listed >= st.limit) { st.more = true; return true; }' $SU
mutate kept_ring_reads_released src/helpers/iface/KeptReports.cpp '  uint32_t lo = after;
  if (_mark > lo) lo = _mark;' '  uint32_t lo = after;' $KU
mutate kept_cursor_reused_after_a_reboot src/helpers/iface/KeptReports.cpp '  _next = top + 1 > meta_next ? top + 1 : meta_next;' '  _next = top + 1;' $KU
mutate kept_torn_record_trusted src/helpers/iface/KeptReports.cpp '  if (crc16(buf, 9 + len) != got || buf[9] != REPORT_MARKER) return false;' '  if (buf[9] != REPORT_MARKER) return false;' $KU
# nRF52 batching (KeptReports.h, Issue #610): eight reports to a write, flushed by an hour, a list, an ack and the shutdown path
mutate kept_nrf52_batching_off src/helpers/iface/KeptReports.h 'inline int keptBatchFor(bool nrf52) { return nrf52 ? 8 : 1; }' 'inline int keptBatchFor(bool nrf52) { return 1; }' $SU
mutate kept_batch_setting_ignored src/helpers/iface/StandingOps.cpp 'h.kept_batch ? h.kept_batch : keptBatchFor(nrf52)' 'keptBatchFor(nrf52)' $SU
mutate kept_batch_written_at_once src/helpers/iface/KeptReports.cpp '  if (_pend_n >= _batch) flush();' '  if (true) flush();' $KU
mutate kept_list_does_not_flush src/helpers/iface/KeptReports.cpp '  flush();   // a list shows everything the device holds' '' $KU
mutate kept_hour_bound_ignored src/helpers/iface/KeptReports.cpp '  if (_pend_n && (uint32_t)(now_ms - _pend_ms) >= HOLD_MS) flush();' '  if (false) flush();' $KU
mutate kept_shutdown_does_not_flush src/helpers/iface/KeptReports.cpp 'void KeptReports::flushAll() { for (KeptReports* k = head(); k; k = k->_next_inst) k->flush(); }' 'void KeptReports::flushAll() {}' $KU
