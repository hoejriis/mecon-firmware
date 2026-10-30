# Mutants for sim/mutate.sh, sourced in file-name order into its shell (variables set here are seen by later files).
# signed updates, staging and preflight (UPDATES.md sections 3, 4 and 8; src/helpers/updates/)
UP=sim/tests/test_updates.py
mutate upd_unknown_key_allowed src/helpers/updates/UpdManifest.cpp '    if (!known) return false;' '' unit:test_updates
mutate upd_uppercase_hex_accepted src/helpers/updates/UpdManifest.cpp "  if (c >= 'a' && c <= 'f') return c - 'a' + 10;" "  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;" unit:test_updates
mutate upd_signature_unchecked src/helpers/updates/UpdManifest.cpp '  const bool ok = Dep::verifySig(sig, msg, dl + 1 + len, key);' '  const bool ok = true; (void)sig; (void)key;' unit:test_updates "$UP::test_staging_refuses_a_wrong_signature_an_unknown_key_and_a_wrong_target"
mutate upd_any_key_trusted src/helpers/updates/UpdPreflight.h '  if (!a || strcmp(a->line, m.line) != 0) return refuse("untrusted-release-key");' '  if (!a) a = findAnchor(TEST_ANCHORS[0].key_id);' unit:test_updates
mutate upd_target_variant_ignored src/helpers/updates/UpdPreflight.h '      strcmp(m.variant, d.variant) != 0 || m.layout != d.layout' '      m.layout != d.layout' unit:test_updates "$UP::test_staging_refuses_a_wrong_signature_an_unknown_key_and_a_wrong_target"
mutate upd_floor_ignored src/helpers/updates/UpdPreflight.h '  if (m.security_version < d.security_floor) return refuse("below-security-floor");   // 8' '' unit:test_updates "$UP::test_the_rest_of_preflight_is_reported_not_enforced_at_staging"
mutate upd_min_from_ignored src/helpers/updates/UpdPreflight.h '  if (!d.running_known || !parseVersion(m.min_from, &min_from) || compareVersion(d.running, min_from) < 0)' '  if (false)' unit:test_updates
mutate upd_downgrade_without_a_signed_offer src/helpers/updates/UpdPreflight.h '!(m.downgrade && deployment_signed)' '!m.downgrade' unit:test_updates
mutate upd_dev_build_unordered src/helpers/updates/UpdManifest.cpp "  return *p == 0 || *p == '+';   // a development suffix" "  return *p == 0;   // a development suffix" unit:test_updates
mutate upd_prerelease_above_release src/helpers/updates/UpdManifest.cpp '  if (a.pre != b.pre) return a.pre ? -1 : 1;' '  if (a.pre != b.pre) return a.pre ? 1 : -1;' unit:test_updates
mutate upd_slot_size_unchecked src/helpers/updates/UpdPreflight.h '  if (m.kind == KIND_APP && m.image_size > d.slot_size) return refuse("too-large");   // 11' '' unit:test_updates
mutate upd_stage_not_rate_limited src/helpers/updates/UpdService.cpp '  if (_staged_once && (uint32_t)(now_ms - _last_stage_ms) < STAGE_GAP_MS) { c.error = "rate"; return ST_BUSY; }' '' "$UP::test_one_stage_a_second_and_two_slots_the_older_evicted"
mutate upd_stage_evicts_the_newer src/helpers/updates/UpdService.cpp '<= 0 ? &_s[0] : &_s[1];   // evict the older' '<= 0 ? &_s[1] : &_s[0];   // evict the older' "$UP::test_one_stage_a_second_and_two_slots_the_older_evicted"
mutate upd_unreadable_floor_reads_as_none src/helpers/updates/UpdService.cpp '  d.security_floor = floorRead(&floor) ? floor : 0xFFFFFFFFu;   // unreadable: no install passes (fail closed)' '  floorRead(&floor); d.security_floor = floor;' "$UP::test_an_unreadable_floor_fails_closed"

# slice 2: the offer, the push transfer, the install and its confirmation (UPDATES.md sections 5 to 7)
UI=sim/tests/test_update_install.py
U=src/helpers/updates/UpdService.cpp
mutate upd_chunk_token_unchecked $U '  if (!ours || !match) { c.error = "bad-token"; return ST_REFUSED; }' '  if (!ours) { c.error = "bad-token"; return ST_REFUSED; }' "$UI::test_a_chunk_needs_the_jobs_token"
mutate upd_chunk_overlong_written $U '  if ((uint32_t)n > _size - _received) { free(bytes); c.error = "too-large"; return ST_REFUSED; }' '' "$UI::test_a_chunk_needs_the_jobs_token"
mutate upd_chunk_offset_ignored $U '  if ((uint32_t)offset != _received) { w.key("next"); w.unum(_received); return ST_OK; }   // resume where it stands' '' "$UI::test_a_pushed_image_installs_attests_and_is_confirmed"
mutate upd_digest_unchecked $U '  if (!Dep::constTimeEqual(got, _image_sha, 32)) { failJob("digest-mismatch"); return; }' '' "$UI::test_a_wrong_digest_fails_and_activates_nothing"
mutate upd_image_format_unchecked $U '  if (!slotFinish()) { failJob("bad-image"); return; }   // the platform'"'"'s own check of the image format' '  slotFinish();' "$UI::test_an_image_the_platform_does_not_accept_fails"
mutate upd_offer_rate_unlimited $U '  if (_offered_once && (uint32_t)(now_ms - _last_offer_ms) < OFFER_GAP_MS) { c.error = "rate"; return ST_BUSY; }   // 2' '' "$UI::test_offers_that_preflight_refuses"
mutate upd_stall_ignored $U '    if ((uint32_t)(now_ms - _last_chunk_ms) >= STALL_MS) failJob("stalled");' '' "$UI::test_a_transfer_with_no_chunk_for_ten_minutes_fails"
mutate upd_any_reboot_reverts $U '  t->awaitConfirm(j, _within_s, false, BOOT_ABNORMAL);' '  t->awaitConfirm(j, _within_s, false, BOOT_SECOND);' "$UI::test_a_normal_reboot_does_not_revert"
mutate upd_never_marked_valid $U '    if (runningState() == RS_PENDING) markProven();
  }' '  }' "$UI::test_an_abnormal_reset_reverts_at_once"
mutate upd_confirm_leaves_the_rollback_window $U '  markProven();   // a reset must not take a confirmed image back' '' "$UI::test_a_pushed_image_installs_attests_and_is_confirmed"
mutate upd_rollback_not_noticed $U '    if (run && run == r.prev_id) {   // the bootloader returned to the previous slot (a reset before it was marked valid)' '    if (false) {' "$UI::test_a_reset_before_the_image_is_marked_valid_rolls_back"
mutate upd_revert_keeps_the_new_image $U '    if (run == r->new_id) return slotSetBoot(r->prev_id) ? BACK_REBOOT : BACK_FAILED;' '    if (run == r->new_id) return BACK_REBOOT;' "$UI::test_the_deadline_reverts_to_the_previous_image"
mutate upd_confirm_without_attestation $U '  if (!_attested || strcmp(_attested_job, j.id) != 0) return false;' '' "$UI::test_no_confirmation_before_the_new_image_runs"
mutate upd_offer_read_class examples/companion_radio/MeconIface.cpp '  {"update.offer", opUpdOffer, PRIV_UPDATE, true},' '  {"update.offer", opUpdOffer, PRIV_READ, true},' "$UI::test_an_enrolled_device_installs_only_on_a_signed_offer"
mutate upd_chunk_needs_a_class src/helpers/iface/DeployAuth.cpp '  if (strcmp(op.name, "update.chunk") == 0) return PRE_ALLOW;' '' "$UI::test_the_token_not_the_link_authorises_a_chunk"
mutate upd_class_open_to_ble src/helpers/cli/CliPolicy.h '    /* paired BLE     N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW, ALLOW, NOT_PERMITTED),' '    /* paired BLE     N */ { CLI_ROW(ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW,           ALLOW, ALLOW, ALLOW),' unit:test_cli_engine
mutate upd_base64_leftover_bits_ignored src/helpers/updates/UpdB64.h '    if (last && pad == 2 && (g & 0xFFFF) != 0) return -1;   // the leftover bits of "xx==" are zero' '' unit:test_updates "$UI::test_a_chunk_needs_the_jobs_token"
# the security review's findings (revert by slot identity, never by a hash alone; confirmation written down first)
mutate upd_cancel_before_reboot_keeps_the_new_image $U '    if (run == r->prev_id) return slotBooting() == run || slotSetBoot(run) ? BACK_RUNS : BACK_FAILED;' '    if (run == r->prev_id) return BACK_RUNS;' "$UI::test_a_cancel_before_the_reboot_keeps_the_previous_image"
mutate upd_lost_record_keeps_the_new_image $U '  if (runningState() == RS_PENDING) return slotSetBoot(slotWritten()) ? BACK_REBOOT : BACK_FAILED;' '' "$UI::test_an_image_whose_record_is_lost_goes_back_to_the_previous_one"
mutate upd_attest_without_the_digest $U '    if (run && run == r.new_id && _running_known && memcmp(_running_sha, r.image_sha, 32) == 0) {' '    if (run && run == r.new_id) {' "$UI::test_an_image_that_is_not_the_installed_one_goes_back"
mutate upd_lost_job_table_keeps_the_new_image $U '  if (backToPrevious(&r) == BACK_REBOOT && _host.reboot) _host.reboot(_host.user);' '' "$UI::test_a_lost_job_table_does_not_leave_an_unconfirmed_image"
mutate upd_confirmed_image_reverted_when_the_table_is_lost $U '  if (!r.confirmed && run && run == r.new_id) {' '  if (run && run == r.new_id) {' "$UI::test_a_confirmed_image_stays_when_the_job_table_is_lost"
mutate upd_recorded_confirmation_ignored $U '    if (r.confirmed && run && run == r.new_id) { confirmFromRecord(*j); persist(); return; }' '' "$UI::test_a_confirmation_the_job_table_never_saved_still_counts"
mutate upd_offer_during_the_trial $U '  if (runningState() == RS_PENDING) { c.error = "verifying"; return ST_BUSY; }' '' "$UI::test_no_offer_while_the_new_image_is_on_trial"
mutate upd_record_cleared_during_the_trial $U '  if (!recordLoad(&r) || runningState() != RS_VALID) return;   // on trial, or the state cannot be read: not settled' '  if (!recordLoad(&r)) return;' "$UI::test_a_reset_during_the_previous_images_trial_does_not_bring_the_new_one_back"
mutate upd_confirmation_trusted_after_a_rollback $U '    if (r.confirmed && run && run == r.new_id) { confirmFromRecord(*j); persist(); return; }' '    if (r.confirmed) { confirmFromRecord(*j); persist(); return; }' "$UI::test_a_confirmation_that_did_not_survive_its_trial_is_a_rollback"
mutate upd_send_back_unbounded $U '    if (r.attempts >= MAX_SEND_BACK) {   // the previous image failed its trial each time: stay, and say so' '    if (false) {' "$UI::test_after_three_send_backs_the_unconfirmed_image_stays_and_says_so"
mutate upd_stranded_image_not_reported $U '      _stranded = true;' '' "$UI::test_after_three_send_backs_the_unconfirmed_image_stays_and_says_so"
