# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# the job table (Jobs.h, OPERATIONS.md section 3): one open job, eviction order, never twice, deadlines, boot rules, image
mutate jobs_second_open_allowed src/helpers/iface/Jobs.h '    if (openJob()) return nullptr;' '' unit:test_jobs
mutate jobs_unacked_evicted_first src/helpers/iface/Jobs.h '        if (!jobTerminal(j.state) || (pass == 0 && !j.acked)) continue;' '        if (!jobTerminal(j.state) || (pass == 0 && j.acked)) continue;' unit:test_jobs
mutate jobs_evicted_id_forgotten src/helpers/iface/Jobs.h '      _h[_hnext] = hash(pick->id);' '' unit:test_jobs
mutate jobs_terminal_moves src/helpers/iface/Jobs.h '    if (!j || !j->state || jobTerminal(j->state) || state < JOB_ACCEPTED || state > JOB_CANCELLED || state == JOB_AWAITING)' '    if (!j || !j->state || state < JOB_ACCEPTED || state > JOB_CANCELLED || state == JOB_AWAITING)' unit:test_jobs
mutate jobs_deadline_never_fires src/helpers/iface/Jobs.h '    if (!j->confirm_left_s) expired(j);' '' unit:test_jobs
mutate jobs_window_not_restarted_at_activation src/helpers/iface/Jobs.h '      j->confirm_left_s = j->within_s;' '' unit:test_jobs
mutate jobs_cut_job_left_open src/helpers/iface/Jobs.h '    if (j->state == JOB_ACCEPTED || j->state == JOB_PROGRESS) {' '    if (false) {' unit:test_jobs
mutate jobs_any_boot_reverts_an_update src/helpers/iface/Jobs.h '      if (abnormal) revert(j);' '      revert(j);' unit:test_jobs
mutate jobs_image_crc_unchecked src/helpers/iface/Jobs.h '    if (get32(in, c) != crc32(in, crc_at)) return false;' '' unit:test_jobs
# the job operations (JobService.h) and their wiring in the Companion and the console
mutate svc_damaged_table_kept src/helpers/iface/JobService.cpp '    if (!_t->deserialize(buf, n)) persist();' '    _t->deserialize(buf, n);' unit:test_job_service
mutate svc_revert_failure_left_pending src/helpers/iface/JobService.cpp '      if (!_revert || !_revert(_user, *j)) _t->setState(j, JOB_FAILED, REVERT_FAILED, sizeof(REVERT_FAILED) - 1);' '      if (!_revert || !_revert(_user, *j)) {}' unit:test_job_service
mutate svc_ack_save_failure_ignored src/helpers/iface/JobService.cpp '    if (!persist()) { c.error = "storage"; return ST_BUSY; }' '    persist();' unit:test_job_service
mutate svc_confirm_without_the_role src/helpers/iface/JobService.cpp '  if (!_confirm || !_confirm(_user, *j)) { c.error = "confirm-failed"; return ST_FAILED; }' '' unit:test_job_service
mutate svc_cancel_never_reverts src/helpers/iface/JobService.cpp '  if (j->state == JOB_AWAITING) {   // applied: cancelling is the revert' '  if (false) {' unit:test_job_service
mutate svc_deadline_too_fast src/helpers/iface/JobService.cpp '  const uint32_t secs = (now_ms - _tick_ms) / 1000;' '  const uint32_t secs = (now_ms - _tick_ms) / 100;' unit:test_job_service
mutate jobs_status_missing_companion examples/companion_radio/MeconIface.cpp '  m->jobs().writeStatus(w);' '' sim/tests/test_jobs.py::test_the_companion_answers_an_empty_table_and_keeps_it_over_a_reboot
mutate jobs_status_missing_console src/helpers/iface/ConsoleIface.cpp '  m->jobs().writeStatus(w);' '' sim/tests/test_jobs.py::test_the_console_answers_an_empty_table_and_keeps_it_over_a_reboot
mutate jobs_unsigned_ack_once_enrolled src/helpers/iface/DeployAuth.cpp '  const bool plain_read = op.priv == PRIV_READ && strcmp(op.name, "job.ack") != 0;' '  const bool plain_read = op.priv == PRIV_READ;' sim/tests/test_jobs.py::test_once_enrolled_an_unsigned_caller_may_read_jobs_but_not_release_or_change_them
mutate svc_boot_count_unsaved src/helpers/iface/JobService.cpp '    bool changed = _t->openJob() != nullptr;' '    bool changed = false;' unit:test_job_service
mutate svc_time_left_unsaved src/helpers/iface/JobService.cpp '  if (awaiting && _unsaved_s >= SAVE_EVERY_S) changed = true;' '' unit:test_job_service
mutate svc_signed_job_unaddressable src/helpers/iface/JobService.cpp '  return t >= 0 && jsonGetString(*c.doc, t, id, cap, CS_JOB, 1);' '  return t >= 0 && jsonGetString(*c.doc, t, id, cap, CS_ID, 1);' unit:test_job_service
# commit-or-revert of config.apply (OPERATIONS.md 3.4)
mutate cc_remote_needs_no_confirm src/helpers/iface/IfaceConfig.cpp '  if (needs && conf < 0 && !cable) { c.error = "confirm-required"; return ST_INVALID; }' '' unit:test_iface_config
mutate cc_confirm_without_a_key_accepted src/helpers/iface/IfaceConfig.cpp '  if (conf >= 0 && !needs) { c.error = "confirm-not-needed"; return ST_INVALID; }' '' unit:test_iface_config
mutate cc_value_bytes_unchecked src/helpers/iface/ConfigSnap.h '    if (vl > VALUE_MAX || !valueOk(value, vl) || _len + 4 + vl > CAP || !_job[0]) return false;' '    if (vl > VALUE_MAX || _len + 4 + vl > CAP || !_job[0]) return false;' unit:test_iface_config
mutate cc_restore_any_key src/helpers/iface/IfaceConfig.cpp '  if (!(k.flags & KF_CONFIRM) || !(k.flags & KF_WRITABLE) || r.idx > k.idx_max) return false;' '  if (r.idx > k.idx_max) return false;' unit:test_iface_config
mutate cc_snapshot_not_bound_to_its_job src/helpers/iface/IfaceConfig.cpp '    ok = n && s->parse(buf, n) && strcmp(s->job(), j.id) == 0;' '    ok = n && s->parse(buf, n);' unit:test_iface_config
mutate cc_new_profile_not_cleared src/helpers/iface/IfaceConfig.cpp '    else if (strstr(reply, ": (not set)")) ok = s.add(ConfigSnap::R_CLEAR, f, ix, nullptr);' '    else if (false) ok = false;' unit:test_iface_config
mutate cc_secret_not_captured src/helpers/iface/IfaceConfig.cpp '          ok = ch->read_secret && ch->read_secret(h->user, kk, idx, val, sizeof(val));' '          is_null = true;' unit:test_iface_config
mutate cc_failed_apply_kept src/helpers/iface/IfaceConfig.cpp '    if (!all_ok) {   // a tentative change is all or nothing: what did apply goes back at once' '    if (false) {' unit:test_iface_config
mutate cc_snapshot_save_ignored src/helpers/iface/IfaceConfig.cpp '    ok = n && ch->snap->save(img, n);' '    ok = n != 0; ch->snap->save(img, n);' unit:test_iface_config
mutate cc_orphan_snapshot_kept src/helpers/iface/IfaceConfig.cpp '  if (!keep) ch->snap->discard();   // an orphan of a cut apply, or of a job that ended: it holds old secrets' '' unit:test_iface_config
mutate cc_classes_not_recorded src/helpers/iface/IfaceConfig.cpp '      job->cls = cls;' '' unit:test_iface_config
mutate cc_retry_runs_again src/helpers/iface/IfaceConfig.cpp '    if (l == JobTable::FOUND) { JobService::writeSummary(*c.out, *known); return ST_OK; }   // never a second run' '' unit:test_iface_config
mutate cc_snapshot_image_unchecked src/helpers/iface/ConfigSnap.h '    if (n < 4 + 1 + 1 + 2 + 4 || get32(in, 0) != MAGIC || get32(in, n - 4) != crc32(in, n - 4)) return false;' '    if (n < 4 + 1 + 1 + 2 + 4) return false;' unit:test_iface_config
mutate svc_cancel_any_class src/helpers/iface/JobService.cpp '  if (_cls_check && !_cls_check(_cls_user, j->cls)) { c.error = "not-permitted"; return ST_REFUSED; }   // cancel' '' unit:test_job_service
mutate svc_confirm_any_class src/helpers/iface/JobService.cpp '  if (_cls_check && !_cls_check(_cls_user, j->cls)) { c.error = "not-permitted"; return ST_REFUSED; }   // confirm' '' unit:test_job_service
mutate cc_cable_by_transport examples/companion_radio/MeconDeploy.cpp '  return Cli::principalOf(static_cast<MeconIface*>(user)->_ctx) == Cli::P_CABLE;' '  return static_cast<MeconIface*>(user)->_ctx.transport == Cli::T_USB;' sim/tests/test_jobs.py::test_a_signed_caller_on_the_cable_is_not_the_cable
mutate cc_companion_no_revert_hook examples/companion_radio/MeconDeploy.cpp '  _jobs.begin(&g_job_file, abnormalReset(board) == 1, jobRevert, jobConfirm, this);' '  _jobs.begin(&g_job_file, abnormalReset(board) == 1, nullptr, jobConfirm, this);' sim/tests/test_jobs.py::test_a_cancel_reverts_and_a_reboot_while_pending_reverts_at_boot
mutate cc_console_no_revert src/helpers/iface/ConsoleIface.cpp '  _jobs.begin(_host.job_store, _host.board && abnormalReset(*_host.board) == 1, jobRevert, jobConfirm, this);' '  _jobs.begin(_host.job_store, _host.board && abnormalReset(*_host.board) == 1, nullptr, jobConfirm, this);' sim/tests/test_jobs.py::test_the_console_reverts_an_unconfirmed_tx_at_boot
mutate svc_change_never_told src/helpers/iface/JobService.cpp '    if (tell && _on_change) _on_change(_change_user, *j);' '' unit:test_job_service
mutate svc_change_told_twice src/helpers/iface/JobService.cpp '    if (j->seq == _seen_seq[i] && j->state == _seen_state[i]) continue;' '' unit:test_job_service
mutate job_event_only_to_the_requester examples/companion_radio/MeconDeploy.cpp '      if (!l.src || !l.may_read || !l.src->isConnected() || !m->ensureQueue(l)) continue;' '      if (&l != m->_cur_link || !m->ensureQueue(l)) continue;' sim/tests/test_jobs.py::test_every_transport_that_may_read_hears_a_job_change
# the boot checks only ask whether a job is open: they never allocate the table (about 4.4 KB of heap, issue 100)
mutate jobs_config_boot_allocates_the_table src/helpers/iface/IfaceConfig.cpp '  JobTable* t = ch->jobs ? ch->jobs->peek() : nullptr;   // at boot: no table, no job to keep' '  JobTable* t = ch->jobs ? ch->jobs->table() : nullptr;' unit:test_iface_config
mutate jobs_mgmt_boot_allocates_the_table src/helpers/iface/MgmtChannel.cpp '  JobTable* t = h.jobs ? h.jobs->peek() : nullptr;   // at boot: no table, no job to keep' '  JobTable* t = h.jobs ? h.jobs->table() : nullptr;' unit:test_mgmt_channel
