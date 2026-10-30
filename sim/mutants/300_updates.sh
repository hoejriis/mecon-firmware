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
